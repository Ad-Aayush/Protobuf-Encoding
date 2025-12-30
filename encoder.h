#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Varint (protobuf wire type 0 uses this for unsigned integers, keys, lengths)
std::vector<std::uint8_t> encodeVarint(std::uint64_t);
std::pair<std::optional<std::uint64_t>, int>
decodeVarint(const std::vector<std::uint8_t> &, int);

// Signed varint (zigzag encoding for signed integers)
std::vector<std::uint8_t> encodeSignedVarint(int64_t);
std::pair<std::optional<int64_t>, int>
decodeSignedVarint(const std::vector<uint8_t> &, int);

// Fixed-width 64-bit (protobuf wire type 1 uses little-endian fixed64/double)
std::vector<std::uint8_t> encodeFixed64(std::uint64_t);
std::optional<std::uint64_t> decodeFixed64(const std::vector<std::uint8_t> &,
                                           int);

// Double <-> fixed64 bitwise encoding (little-endian on the wire)
std::vector<std::uint8_t> encodeDouble(double);
std::optional<double> decodeDouble(const std::vector<std::uint8_t> &, int);

// Length-delimited string (protobuf wire type 2: varint length + raw bytes)
std::vector<std::uint8_t> encodeStr(const std::string &);
std::pair<std::optional<std::string>, int>
decodeStr(const std::vector<std::uint8_t> &, int);
