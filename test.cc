// Copyright 2019 Endless Mobile, Inc.
// Portions copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "nickle.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <experimental/array>

template <typename T, size_t N>
static nickle::MutableByteSpan ArrayToByteSpan(std::array<T, N>* ar) {
  return {reinterpret_cast<std::byte*>(ar->data()), static_cast<nickle::PickleSize>(N)};
}

template <typename T, size_t N>
static nickle::ConstByteSpan ArrayToByteSpan(const std::array<T, N>& ar) {
  return {reinterpret_cast<const std::byte*>(ar.data()), static_cast<nickle::PickleSize>(N)};
}

TEST_CASE("Basic encode / decode tests") {
  std::vector<std::byte> target;

  constexpr bool kBool1 = false;
  constexpr bool kBool2 = true;
  constexpr int kInt = 2'093'847'192;
  constexpr long kLong = 1'093'847'192;
  const std::uint16_t kUInt16 = 32123;
  const std::uint32_t kUInt32 = 1593847192;
  const std::int64_t kInt64 = -0x7E8CA925'3104BDFCLL;
  const std::uint64_t kUInt64 = 0xCE8CA925'3104BDF7ULL;
  const float kFloat = 3.1415926935f;
  const double kDouble = 2.71828182845904523;

  const std::string kString("Hello world");

  constexpr char kDataArray[] = "AAA\0BBB\0";
  constexpr auto kData = std::experimental::to_array(kDataArray);

  nickle::buffers::ContainerBuffer wbuf(&target);
  nickle::Writer writer(&wbuf);

  REQUIRE(writer.Write<nickle::codecs::Bool>(kBool1));
  REQUIRE(writer.Write<nickle::codecs::Bool>(kBool2));
  REQUIRE(writer.Write<nickle::codecs::UInt16>(kUInt16));
  REQUIRE(writer.Write<nickle::codecs::UInt32>(kUInt32));
  REQUIRE(writer.Write<nickle::codecs::Int64>(kInt64));
  REQUIRE(writer.Write<nickle::codecs::UInt64>(kUInt64));
  REQUIRE(writer.Write<nickle::codecs::Float>(kFloat));
  REQUIRE(writer.Write<nickle::codecs::Double>(kDouble));

  REQUIRE(writer.Write<nickle::codecs::StringView>(kString));
  REQUIRE(writer.Write<nickle::codecs::UnsizedSpan>(ArrayToByteSpan(kData)));

  nickle::buffers::ReadOnlyContainerBuffer rbuf(target);
  nickle::Reader reader(&rbuf);

  bool out_bool;
  REQUIRE(reader.Read<nickle::codecs::Bool>(&out_bool));
  REQUIRE(!out_bool);
  REQUIRE(reader.Read<nickle::codecs::Bool>(&out_bool));
  REQUIRE(out_bool);

  std::uint16_t out_uint16;
  std::uint32_t out_uint32;
  std::int64_t out_int64;
  std::uint64_t out_uint64;
  REQUIRE(reader.Read<nickle::codecs::UInt16>(&out_uint16));
  REQUIRE(out_uint16 == kUInt16);
  REQUIRE(reader.Read<nickle::codecs::UInt32>(&out_uint32));
  REQUIRE(out_uint32 == kUInt32);
  REQUIRE(reader.Read<nickle::codecs::Int64>(&out_int64));
  REQUIRE(out_int64 == kInt64);
  REQUIRE(reader.Read<nickle::codecs::UInt64>(&out_uint64));
  REQUIRE(out_uint64 == kUInt64);

  float out_float;
  double out_double;
  REQUIRE(reader.Read<nickle::codecs::Float>(&out_float));
  REQUIRE(out_float == kFloat);
  REQUIRE(reader.Read<nickle::codecs::Double>(&out_double));
  REQUIRE(out_double == kDouble);

  std::string out_string;
  REQUIRE(reader.Read<nickle::codecs::String>(&out_string));
  REQUIRE(out_string == kString);

  std::array<char, kData.size()> out_data;
  auto out_data_span = ArrayToByteSpan(&out_data);
  REQUIRE(reader.Read<nickle::codecs::UnsizedSpan>(&out_data_span));
  REQUIRE(kData == out_data);
}
