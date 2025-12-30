#include "encoder.h"
#include <cstring>
#include <iostream>

// Overload << for std::vector<uint8_t> printing space separated hex values
std::ostream &operator<<(std::ostream &os, const std::vector<uint8_t> &vec) {
  for (size_t i = 0; i < vec.size(); i++) {
    // Ensure 2 hex digits are printed
    if (vec[i] < 16) {
      os << "0";
    }
    os << std::hex << static_cast<int>(vec[i]);
    if (i != vec.size() - 1) {
      os << " ";
    }
  }
  os << std::dec;
  return os;
}

std::vector<uint8_t> encodeVarint(uint64_t num) {
  std::vector<uint8_t> enc;
  do {
    uint8_t rem = num & 0x7F;
    if (num > rem) {
      enc.push_back(rem | (1 << 7));
    } else {
      enc.push_back(rem);
    }
    num >>= 7;
  } while (num > 0);
  return enc;
}

std::vector<uint8_t> encodeSignedVarint(int64_t num) {
  return encodeVarint(static_cast<uint64_t>((num << 1) ^ (num >> 63)));
}

std::vector<uint8_t> encodeFixed64(uint64_t num) {
  std::vector<uint8_t> enc;
  for (int i = 0; i < 8; i++) {
    enc.push_back((num >> (8 * i)) & 0xFF);
  }
  return enc;
}

std::vector<uint8_t> encodeDouble(double num) {
  uint64_t asInt;
  std::memcpy(&asInt, &num, sizeof(double));
  return encodeFixed64(asInt);
}

std::vector<uint8_t> encodeStr(const std::string &str) {
  std::vector<uint8_t> enc;
  int sz = str.size();
  std::vector<uint8_t> lenEnc = encodeVarint(sz);
  enc.insert(enc.end(), lenEnc.begin(), lenEnc.end());
  for (char c : str) {
    enc.push_back(static_cast<uint8_t>(c));
  }
  return enc;
}

std::pair<std::optional<uint64_t>, int>
decodeVarint(const std::vector<uint8_t> &str, int index = 0) {
  uint64_t out = 0;
  int shift = 0;
  int sz = str.size();

  for (int i = index, count = 0; i < sz && count <= 10; ++i, ++count) {
    uint8_t b = str[i];
    out |= uint64_t(b & 0x7F) << shift;

    if ((b & 0x80) == 0) {
      return {out, i + 1};
    }
    shift += 7;
    if (shift >= 64)
      break;
  }
  return {std::nullopt, index};
}

std::pair<std::optional<int64_t>, int>
decodeSignedVarint(const std::vector<uint8_t> &str, int index = 0) {
  auto [unsignedValOpt, nextIndex] = decodeVarint(str, index);
  if (!unsignedValOpt.has_value()) {
    return {std::nullopt, index};
  }
  uint64_t unsignedVal = unsignedValOpt.value();
  int64_t signedVal;
  signedVal = (unsignedVal >> 1) ^ -(static_cast<int64_t>(unsignedVal & 1));
  return {signedVal, nextIndex};
}

std::optional<uint64_t> decodeFixed64(const std::vector<uint8_t> &str,
                                      int index = 0) {
  int sz = str.size();
  if (index + 8 > sz) {
    return std::nullopt;
  }
  uint64_t out = 0;
  for (int i = index; i < index + 8; i++) {
    out |= static_cast<uint64_t>(str[i]) << (8 * (i - index));
  }
  return out;
}

std::optional<double> decodeDouble(const std::vector<uint8_t> &str,
                                   int index = 0) {
  auto fixedOpt = decodeFixed64(str, index);
  if (!fixedOpt.has_value()) {
    return std::nullopt;
  }
  uint64_t asInt = fixedOpt.value();
  double out;
  std::memcpy(&out, &asInt, sizeof(double));
  return out;
}

std::pair<std::optional<std::string>, int>
decodeStr(const std::vector<uint8_t> &str, int index = 0) {
  int sz = str.size();
  std::string res;
  auto [lengthOpt, newIndex] = decodeVarint(str, index);
  if (!lengthOpt.has_value()) {
    return {std::nullopt, index};
  }
  int length = lengthOpt.value();
  if (newIndex + length > sz) {
    return {std::nullopt, index};
  }
  for (int i = newIndex; i < newIndex + length; i++) {
    res += static_cast<char>(str[i]);
  }
  return {res, static_cast<int>(newIndex + length)};
}
