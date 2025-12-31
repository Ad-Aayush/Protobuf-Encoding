#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

struct RepeatedVal;
enum class FieldType { Int, Double, String, UInt, Bool };
using Value =
    std::variant<int64_t, double, std::string, uint64_t, bool, RepeatedVal>;

struct RepeatedVal {
  FieldType elemType;
  std::vector<Value> values;
};

struct FieldDesc {
  std::string name;
  uint32_t number; // protobuf field number
  FieldType type;

  bool isRepeated;
  bool isPacked; // for repeated fields with packed encoding

  FieldDesc(std::string n, uint32_t num, FieldType t, bool repeated = false,
            bool packed = true)
      : name(std::move(n)), number(num), type(t), isRepeated(repeated),
        isPacked(packed) {}
};

class ProtoDesc {
  std::unordered_map<std::string, size_t> nameToIndex;
  std::unordered_map<uint32_t, size_t> numberToIndex;

public:
  std::vector<FieldDesc> fields;
  explicit ProtoDesc(std::vector<FieldDesc> flds);
  const FieldDesc *findByName(const std::string &name) const;
  std::optional<size_t> indexByName(const std::string &name) const;
  std::optional<size_t> indexByNumber(uint32_t number) const;
};

class Message {
public:
  std::shared_ptr<const ProtoDesc> desc;
  std::vector<std::optional<Value>> vals; // per-field slot

  explicit Message(std::shared_ptr<const ProtoDesc> d);
  std::optional<std::reference_wrapper<const Value>>
  get(const std::string &fieldName) const;
  bool set(const std::string &fieldName, Value v);
  std::optional<std::reference_wrapper<const Value>>
  getByIndex(const std::string &fieldName, size_t idx) const;
  bool setByIndex(const std::string &fieldName, size_t idx, Value v);
  bool push(const std::string &fieldName, Value v);
};
