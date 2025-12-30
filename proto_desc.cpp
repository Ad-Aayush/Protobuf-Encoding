#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

enum class FieldType { Int, Double, String };
using Value = std::variant<std::int64_t, double, std::string>;

struct FieldDesc {
  std::string name;
  uint32_t number; // protobuf field number
  FieldType type;
};

class ProtoDesc {
  std::unordered_map<std::string, size_t> nameToIndex;
  std::unordered_map<uint32_t, size_t> numberToIndex;

public:
  std::vector<FieldDesc> fields;
  explicit ProtoDesc(std::vector<FieldDesc> flds) : fields(std::move(flds)) {
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

  const FieldDesc *findByName(const std::string &name) const {
    auto it = nameToIndex.find(name);
    if (it == nameToIndex.end())
      return nullptr;
    return &fields[it->second];
  }

  std::optional<size_t> indexByName(const std::string &name) const {
    auto it = nameToIndex.find(name);
    if (it == nameToIndex.end())
      return std::nullopt;
    return it->second;
  }
};

class Message {
public:
  std::shared_ptr<const ProtoDesc> desc;
  std::vector<std::optional<Value>> vals; // per-field slot

  explicit Message(std::shared_ptr<const ProtoDesc> d)
      : desc(std::move(d)), vals(desc->fields.size()) {}

  std::optional<std::reference_wrapper<const Value>>
  get(const std::string &fieldName) const {
    std::optional<size_t> maybeIdx = desc->indexByName(fieldName);
    if (!maybeIdx.has_value())
      return std::nullopt;
    size_t idx = *maybeIdx;
    if (!vals[idx].has_value())
      return std::nullopt;
    return std::cref(*vals[idx]);
  }

  bool set(const std::string &fieldName, Value v) {
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
    default:
      return false;
    }

    vals[idx] = std::move(v);
    return true;
  }
};

int main() {
  std::shared_ptr<const ProtoDesc> desc =
      std::make_shared<ProtoDesc>(std::vector<FieldDesc>{
          {"id", 1, FieldType::Int},
          {"value", 2, FieldType::Double},
          {"name", 3, FieldType::String},
      });

  Message msg(desc);
  msg.set("id", int64_t(123));
  msg.set("value", 45.67);
  msg.set("name", std::string("example"));

  std::cout << "Message contents:\n";
  auto id = msg.get("id");
  if (id.has_value())
    std::cout << std::get<std::int64_t>(id->get()) << "\n";
  auto value = msg.get("value");
  if (value.has_value())
    std::cout << std::get<double>(value->get()) << "\n";
  auto name = msg.get("name");
  if (name.has_value())
    std::cout << std::get<std::string>(name->get()) << "\n";
  return 0;
}