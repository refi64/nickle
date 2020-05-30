// Copyright 2019 Endless Mobile, Inc.
// Portions copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NICKLE_H
#define NICKLE_H

#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

static_assert(!std::is_unsigned_v<int>);
static_assert(sizeof(int) == 4);

namespace nickle {

using PickleSize = uint32_t;
static_assert(sizeof(PickleSize) <= sizeof(std::size_t));

struct ConstByteSpan {
  const std::byte* data;
  PickleSize len;
};

struct MutableByteSpan {
  std::byte* data;
  PickleSize len;

  operator ConstByteSpan() { return {data, len}; }
};

namespace safe_math {

  template <typename T>
  inline bool Add(T* target, T a, T b) {
    return !__builtin_add_overflow(a, b, target);
  }

}  // namespace safe_math

struct Header {
  PickleSize payload_size;
};

inline PickleSize PickleSizePadding(PickleSize size) {
  return (sizeof(PickleSize) - (size % sizeof(PickleSize))) % 4;
}

namespace buffers {

  class SizedBuffer {
   public:
    virtual PickleSize payload_size() const = 0;
  };

  class WritableBuffer {
   public:
    virtual bool Write(ConstByteSpan span) = 0;
  };

  class ReadableBuffer {
   public:
    virtual bool Read(MutableByteSpan span) = 0;
  };

  template <typename ContainerType>
  class ReadOnlyContainerBuffer : public SizedBuffer, public ReadableBuffer {
    static_assert(sizeof(typename ContainerType::value_type) == 1);

   public:
    ReadOnlyContainerBuffer(const ContainerType& container) : container_(container) {}

    PickleSize offset() const { return offs_; }

    PickleSize payload_size() const final {
      return reinterpret_cast<const Header*>(container_.data())->payload_size;
    }

    bool Read(MutableByteSpan span) final override {
      PickleSize padded_size = 0;
      PickleSize padding = PickleSizePadding(span.len);
      if (!safe_math::Add(&padded_size, span.len, padding)) {
        return false;
      }

      PickleSize new_offs = 0;
      if (!safe_math::Add(&new_offs, offs_, padded_size)) {
        return false;
      }

      if (new_offs > container_.size() || new_offs > sizeof(Header) + payload_size()) {
        return false;
      }

      std::memcpy(span.data, reinterpret_cast<const std::byte*>(container_.data() + offs_),
                  span.len);
      offs_ += padded_size;

      return true;
    }

   protected:
    PickleSize offs_ = sizeof(Header);

   private:
    const ContainerType& container_;
  };

  template <typename ContainerType>
  class ContainerBuffer final : public ReadOnlyContainerBuffer<ContainerType>,
                                public WritableBuffer {
    static_assert(sizeof(typename ContainerType::value_type) == 1);

   public:
    ContainerBuffer(ContainerType* target)
        : ReadOnlyContainerBuffer<ContainerType>(*target), target_(target) {
      if (target_->size() < sizeof(Header)) {
        target_->resize(sizeof(Header));
      }
    }

    bool Write(ConstByteSpan span) override {
      PickleSize padded_size = 0;
      PickleSize padding = PickleSizePadding(span.len);
      if (!safe_math::Add(&padded_size, span.len, padding)) {
        return false;
      }

      PickleSize expanded = 0;
      if (!safe_math::Add(&expanded, this->offs_, padded_size)) {
        return false;
      }

      if (!safe_math::Add(&reinterpret_cast<Header*>(target_->data())->payload_size,
                          this->payload_size(), padded_size)) {
        return false;
      }

      if (expanded > target_->size()) {
        target_->resize(expanded);
      }

      std::memcpy(reinterpret_cast<std::byte*>(target_->data() + this->offs_), span.data, span.len);
      std::memset(reinterpret_cast<std::byte*>(target_->data() + this->offs_ + span.len), 0,
                  padding);
      this->offs_ = expanded;
      return true;
    }

   private:
    ContainerType* target_;
  };

}  // namespace buffers

namespace codecs {

  template <typename T, typename U = T>
  struct Base {
    using ItemType = T;
    using ConstArgType = U;
  };

  template <typename T>
  struct Integral : Base<T> {
    static bool WriteToBuffer(buffers::WritableBuffer* buffer, T value) {
      return buffer->Write({reinterpret_cast<std::byte*>(&value), sizeof(T)});
    }

    static bool ReadFromBuffer(buffers::ReadableBuffer* buffer, T* value) {
      return buffer->Read({reinterpret_cast<std::byte*>(value), sizeof(T)});
    }
  };

  struct Int16 : Integral<std::int16_t> {};
  struct UInt16 : Integral<std::uint16_t> {};
  struct Int32 : Integral<std::int32_t> {};
  struct UInt32 : Integral<std::uint32_t> {};
  struct Int64 : Integral<std::int64_t> {};
  struct UInt64 : Integral<std::uint64_t> {};
  struct Float32 : Integral<float> {};
  struct Float64 : Integral<double> {};

  using Int = Int32;
  using Long = Int64;
  using Float = Float32;
  using Double = Float64;

  struct Bool : Base<bool> {
    static bool WriteToBuffer(buffers::WritableBuffer* buffer, bool value) {
      return Int32::WriteToBuffer(buffer, static_cast<int32_t>(value));
    }

    static bool ReadFromBuffer(buffers::ReadableBuffer* buffer, bool* value) {
      int32_t i32;
      if (!Int32::ReadFromBuffer(buffer, &i32)) {
        return false;
      }

      *value = static_cast<bool>(i32);
      return true;
    }
  };

  template <typename T, T min = std::numeric_limits<T>::min(),
            T max = std::numeric_limits<T>::max()>
  struct BoundedIntegral : Base<T> {
    static bool WriteToBuffer(buffers::WritableBuffer* buffer, T value) {
      return Integral<T>::WriteToBuffer(buffer, value);
    }

    static bool ReadFromBuffer(buffers::ReadableBuffer* buffer, T* value) {
      if (!Integral<T>::ReadFromBuffer(buffer, value)) {
        return false;
      }

      if (*value < min || *value > max) {
        return false;
      }

      return true;
    }
  };

  template <typename Enum, typename Underlying = std::underlying_type_t<Enum>,
            Enum min = static_cast<Enum>(std::numeric_limits<Underlying>::min()),
            Enum max = static_cast<Enum>(std::numeric_limits<Underlying>::max())>
  struct Enumerated : Base<Enum> {
    using UnderlyingCodec =
        BoundedIntegral<Underlying, static_cast<Underlying>(min), static_cast<Underlying>(max)>;

    static bool WriteToBuffer(buffers::WritableBuffer* buffer, Enum value) {
      return UnderlyingCodec::WriteToBuffer(buffer, static_cast<Underlying>(value));
    }

    static bool ReadFromBuffer(buffers::ReadableBuffer* buffer, Enum* value) {
      Underlying underlying;
      if (!UnderlyingCodec::ReadFromBuffer(buffer, &underlying)) {
        return false;
      }

      *value = static_cast<Enum>(underlying);
      return true;
    }
  };

  // Equivalent to WriteBytes/ReadBytes.
  struct UnsizedSpan : Base<MutableByteSpan, ConstByteSpan> {
    static bool WriteToBuffer(buffers::WritableBuffer* buffer, ConstByteSpan span) {
      return buffer->Write(span);
    }

    static bool ReadFromBuffer(buffers::ReadableBuffer* buffer, MutableByteSpan* span) {
      return buffer->Read(*span);
    }
  };

  // Equivalent to WriteData/ReadData.
  struct SizedSpanEncoder : Base<ConstByteSpan> {
    static bool WriteToBuffer(buffers::WritableBuffer* buffer, ConstByteSpan span) {
      if (!Int32::WriteToBuffer(buffer, static_cast<int32_t>(span.len))) {
        return false;
      }

      return UnsizedSpan::WriteToBuffer(buffer, std::move(span));
    }
  };

  template <typename ViewType>
  struct IntegralView : Base<ViewType> {
    static bool WriteToBuffer(buffers::WritableBuffer* buffer, ViewType value) {
      ConstByteSpan span{
          reinterpret_cast<const std::byte*>(value.data()),
          static_cast<PickleSize>(value.size() * sizeof(typename ViewType::value_type))};
      return SizedSpanEncoder::WriteToBuffer(buffer, std::move(span));
    }
  };

  template <typename ContainerType>
  struct IntegralContainer : Base<ContainerType, const ContainerType&> {
    static bool WriteToBuffer(buffers::WritableBuffer* buffer, const ContainerType& value) {
      ConstByteSpan span{reinterpret_cast<const std::byte*>(value.data()),
                         value.size() * sizeof(typename ContainerType::value_type)};

      return SizedSpanEncoder::WriteToBuffer(buffer, std::move(span));
    }

    static bool ReadFromBuffer(buffers::ReadableBuffer* buffer, ContainerType* value) {
      int32_t i32;
      if (!Int32::ReadFromBuffer(buffer, &i32)) {
        return false;
      }

      value->resize(i32);
      MutableByteSpan span{
          reinterpret_cast<std::byte*>(value->data()),
          static_cast<PickleSize>(value->size() * sizeof(typename ContainerType::value_type))};

      return UnsizedSpan::ReadFromBuffer(buffer, &span);
    }
  };

  using StringView = IntegralView<std::string_view>;
  using String = IntegralContainer<std::string>;

  using StringView16 = IntegralView<std::basic_string_view<std::uint16_t>>;
  using String16 = IntegralContainer<std::basic_string<std::uint16_t>>;

}  // namespace codecs

class Writer {
 public:
  Writer(buffers::WritableBuffer* buffer) : buffer_(buffer) {}

  template <typename Encoder>
  bool Write(typename Encoder::ConstArgType value) {
    return Encoder::WriteToBuffer(buffer_, value);
  }

 private:
  buffers::WritableBuffer* buffer_;
};

class Reader {
 public:
  Reader(buffers::ReadableBuffer* buffer) : buffer_(buffer) {}

  template <typename Decoder>
  bool Read(typename Decoder::ItemType* value) {
    return Decoder::ReadFromBuffer(buffer_, value);
  }

 private:
  buffers::ReadableBuffer* buffer_;
};

}  // namespace nickle

#endif
