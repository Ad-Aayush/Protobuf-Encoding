#include "proto_desc.h"
#include <iostream>
#include <stdexcept>

ProtoDesc::ProtoDesc(std::vector<FieldDesc> flds) : fields(std::move(flds)) {
  nameToIndex.reserve(fields.size());
  numberToIndex.reserve(fields.size());

  for (size_t i = 0; i < fields.size(); ++i) {
    const auto &fd = fields[i];
    if (fd.number == 0)
      throw std::runtime_error("field number cannot be 0");
    if (!nameToIndex.emplace(fd.name, i).second)
      throw std::runtime_error("duplicate field name: " + fd.name);
    if (!numberToIndex.emplace(fd.number, i).second)
      throw std::runtime_error("duplicate field number: " +
                               std::to_string(fd.number));
  }
}

const FieldDesc *ProtoDesc::findByName(const std::string &name) const {
  auto it = nameToIndex.find(name);
  if (it == nameToIndex.end())
    return nullptr;
  return &fields[it->second];
}

std::optional<size_t> ProtoDesc::indexByName(const std::string &name) const {
  auto it = nameToIndex.find(name);
  if (it == nameToIndex.end())
    return std::nullopt;
  return it->second;
}

std::optional<size_t> ProtoDesc::indexByNumber(uint32_t number) const {
  auto it = numberToIndex.find(number);
  if (it == numberToIndex.end()) {
    std::cout << "NUM: " << number << "\n";
    return std::nullopt;
  }
  return it->second;
}

Message::Message(std::shared_ptr<const ProtoDesc> d)
    : desc(std::move(d)), vals(desc->fields.size()) {}

std::optional<std::reference_wrapper<const Value>>
Message::get(const std::string &fieldName) const {
  std::optional<size_t> maybeIdx = desc->indexByName(fieldName);
  if (!maybeIdx.has_value())
    return std::nullopt;
  size_t idx = *maybeIdx;
  if (!vals[idx].has_value())
    return std::nullopt;
  return std::cref(*vals[idx]);
}

bool Message::set(const std::string &fieldName, Value v) {
  std::optional<size_t> maybeIdx = desc->indexByName(fieldName);
  if (!maybeIdx.has_value())
    return false;
  size_t idx = *maybeIdx;
  const FieldDesc &fd = desc->fields[idx];

  // Type check
  switch (fd.type) {
  case FieldType::Int:
    if (!std::holds_alternative<std::int64_t>(v))
      return false;
    break;
  case FieldType::Double:
    if (!std::holds_alternative<double>(v))
      return false;
    break;
  case FieldType::String:
    if (!std::holds_alternative<std::string>(v))
      return false;
    break;
  case FieldType::UInt:
    if (!std::holds_alternative<std::uint64_t>(v))
      return false;
    break;
  default:
    return false;
  }

  vals[idx] = std::move(v);
  return true;
}
