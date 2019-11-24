CXX ?= c++

test: test.cc nickle.h doctest/doctest/doctest.h
	$(CXX) -std=c++17 -Idoctest/doctest -o $@ $<
