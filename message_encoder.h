#pragma once
#include "proto_desc.h"
#include <cstdint>

enum WireType { VARINT = 0, I64 = 1, LEN = 2 };

std::vector<uint8_t> encodeMessage(const Message &);

std::pair<std::optional<Message>, int>
decodeMessage(const std::vector<uint8_t> &, std::shared_ptr<const ProtoDesc>);