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

TEST(Varint, ZeroIsSingleByte) {
  auto enc = encodeVarint(0);
  ASSERT_EQ(enc.size(), 1u);
  EXPECT_EQ(enc[0], 0x00);
}

TEST(Varint, RejectTooLongVarint) {
  // 11 bytes is invalid for uint64 varint (max 10)
  std::vector<uint8_t> bad(11, 0x80);
  bad.back() = 0x00;
  auto [dec, next] = decodeVarint(bad, 0);
  EXPECT_FALSE(dec.has_value());
  EXPECT_EQ(next, 0);
}

TEST(Varint, DecodesFromOffsetAndAdvances) {
  std::vector<uint8_t> buf;
  auto a = encodeVarint(150);
  auto b = encodeVarint(300);

  buf.insert(buf.end(), a.begin(), a.end());
  buf.insert(buf.end(), b.begin(), b.end());

  auto [da, i] = decodeVarint(buf, 0);
  ASSERT_TRUE(da.has_value());
  EXPECT_EQ(*da, static_cast<uint64_t>(150));

  auto [db, j] = decodeVarint(buf, i);
  ASSERT_TRUE(db.has_value());
  EXPECT_EQ(*db, static_cast<uint64_t>(300));
  EXPECT_EQ(j, (int)buf.size());
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

TEST(Double, RoundTripSpecial) {
  std::vector<double> vals = {std::numeric_limits<double>::infinity(),
                              -std::numeric_limits<double>::infinity(),
                              std::numeric_limits<double>::quiet_NaN()};

  for (double v : vals) {
    auto enc = encodeDouble(v);
    auto dec = decodeDouble(enc, 0);
    ASSERT_TRUE(dec.has_value());
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

TEST(String, RejectTruncatedPayload) {
  auto enc = encodeStr("abc");
  enc.pop_back(); // remove one byte
  auto [dec, next] = decodeStr(enc, 0);
  EXPECT_FALSE(dec.has_value());
  EXPECT_EQ(next, 0);
}

TEST(String, EmptyIsJustLengthZero) {
  auto enc = encodeStr("");
  ASSERT_GE(enc.size(), 1u);
  EXPECT_EQ(enc[0], 0x00);
  auto [dec, next] = decodeStr(enc, 0);
  ASSERT_TRUE(dec.has_value());
  EXPECT_EQ(*dec, "");
  EXPECT_EQ(next, (int)enc.size());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
