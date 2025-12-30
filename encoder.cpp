#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>

std::string intToBits(int x) {
  std::string str;
  while (x) {
    if (x % 2 == 1) {
      str += '1';
    } else {
      str += '0';
    }
    x /= 2;
  }
  int sz = str.size();
  for (int i = 0; i < 8 - sz; i++) {
    str += '0';
  }

  std::reverse(str.begin(), str.end());
  return str;
}

std::string encodeVarint(long long num) {
  std::string enc;
  while (num != 0) {
    int rem = num % 128;
    if (num > rem) {
      enc += intToBits(rem | (1 << 7));
    } else {
      enc += intToBits(rem);
    }
    num /= 128;
  }
  return enc;
}

std::string encodeI64(double num) {
  std::string enc;
  uint64_t asInt;
  std::memcpy(&asInt, &num, sizeof(double));
  for (int i = 0; i < 64; i++) {
    if (asInt & (1ULL << (63 - i))) {
      enc += '1';
    } else {
      enc += '0';
    }
  }
  return enc;
}

std::string encodeStr(const std::string &str) {
  std::string enc;
  int sz = str.size();
  enc += intToBits(sz);
  for (char c : str) {
    enc += intToBits(static_cast<int>(c));
  }
  return enc;
}

std::pair<std::optional<long long>, int> decodeVarint(const std::string &str,
                                                      int index = 0) {
  int sz = str.size();
  long long res = 0;
  long long pow128 = 1;
  for (; index < sz; index += 8) {
    bool last = str[index] == '0';
    long long partInt = 0;
    long long pow2 = 64;
    if (index + 8 > sz) {
      return {std::nullopt, index};
    }
    for (int j = index + 1; j < index + 8; j++) {
      if (str[j] == '1') {
        partInt += pow2;
      }
      pow2 /= 2;
    }
    res += partInt * pow128;
    pow128 *= 128;
    if (last) {
      break;
    }
  }
  return {res, index};
}

std::optional<double> decodeI64(const std::string &str, int index = 0) {
  int sz = str.size();
  if (index + 64 > sz) {
    return std::nullopt;
  }
  uint64_t asInt = 0;
  uint64_t pow2 = 1ULL << 63;
  for (int i = index; i < index + 64; i++) {
    if (str[i] == '1') {
      asInt += pow2;
    }
    pow2 >>= 1;
  }
  double res;
  std::memcpy(&res, &asInt, sizeof(double));
  return res;
}

std::pair<std::optional<std::string>, int> decodeStr(const std::string &str,
                                                     int index = 0) {
  int sz = str.size();
  std::string res;
  auto [maybeRecSz, newIndex] = decodeVarint(str, index);
  if (!maybeRecSz.has_value()) {
    return {std::nullopt, index};
  }
  int recSz = *maybeRecSz;
  std::cout << recSz << "\n";
  index = newIndex;
  res.reserve(recSz);
  while (res.size() <= recSz) {
    if (index + 8 > sz) {
      return {std::nullopt, index};
    }
    int charInt = 0;
    int pow2 = 128;
    for (int j = index; j < index + 8; j++) {
      if (str[j] == '1') {
        charInt += pow2;
      }
      pow2 /= 2;
    }
    res += static_cast<char>(charInt);
    index += 8;
  }
  return {res, index};
}

int main() {
  std::string enc = encodeVarint(150);
  std::cout << enc << "\n";
  std::cout << decodeVarint(enc).first.value_or(-1) << "\n";

  enc = encodeI64(164.25);
  std::cout << enc << "\n";
  std::cout << decodeI64(enc).value_or(-1) << "\n";

  enc = encodeStr("testing");
  std::cout << enc << "\n";
  std::cout << decodeStr(enc).first.value_or("error") << "\n";
}