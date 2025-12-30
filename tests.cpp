#include "encoder.h"
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <string>
#include <vector>

TEST(Varint, RoundTripKeyValues) {
  std::vector<uint64_t> vals = {
      0ULL,         1ULL,
      2ULL,         10ULL,
      127ULL,       128ULL,
      129ULL,       150ULL,
      16383ULL,     16384ULL,
      (1ULL << 31), (1ULL << 32),
      (1ULL << 63), std::numeric_limits<uint64_t>::max()};

  for (auto v : vals) {
    auto enc = encodeVarint(v);
    auto [dec, next] = decodeVarint(enc, 0);
    ASSERT_TRUE(dec.has_value()) << "Failed to decode v=" << v;
    EXPECT_EQ(*dec, v);
    EXPECT_EQ(next, static_cast<int>(enc.size()));
  }
}

TEST(Varint, RejectTruncatedContinuation) {
  // 0x80 means "continuation follows" but we end immediately -> invalid.
  std::vector<uint8_t> bad = {0x80};
  auto [dec, next] = decodeVarint(bad, 0);
  EXPECT_FALSE(dec.has_value());
  EXPECT_EQ(next, 0);
}

TEST(Fixed64, RoundTrip) {
  std::vector<uint64_t> vals = {0ULL, 1ULL, 0x1122334455667788ULL,
                                std::numeric_limits<uint64_t>::max()};

  for (auto v : vals) {
    auto enc = encodeFixed64(v);
    ASSERT_EQ(enc.size(), 8u);
    auto dec = decodeFixed64(enc, 0);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(*dec, v);
  }
}

TEST(Double, RoundTripCommon) {
  std::vector<double> vals = {0.0, -0.0, 1.0, -1.0, 25.4, 164.25, 1e-9, 1e9};

  for (double v : vals) {
    auto enc = encodeDouble(v);
    ASSERT_EQ(enc.size(), 8u);
    auto dec = decodeDouble(enc, 0);
    ASSERT_TRUE(dec.has_value());
    // Exact bitwise roundtrip should hold because we memcpy bits.
    EXPECT_EQ(std::memcmp(&v, &(*dec), sizeof(double)), 0);
  }
}

TEST(String, RoundTrip) {
  std::vector<std::string> vals = {"", "a", "testing", std::string(200, 'x')};

  for (auto &s : vals) {
    auto enc = encodeStr(s);
    auto [dec, next] = decodeStr(enc, 0);
    ASSERT_TRUE(dec.has_value());
    EXPECT_EQ(*dec, s);
    EXPECT_EQ(next, static_cast<int>(enc.size()));
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
