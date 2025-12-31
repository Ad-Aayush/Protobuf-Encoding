* **Finish wire-format coverage**

  * Add `WireType::I32` handling in `skipUnknown` (and optionally deprecated groups 3/4).
  * Add `FieldType::Bytes` (raw `std::vector<uint8_t>`) distinct from `String`.

* **Make decoding more protobuf-tolerant**

  * For repeated numeric fields, accept both **packed** (`LEN`) and **unpacked** (`VARINT/I64/I32`) on the wire regardless of `isPacked`.

* **Schema-evolution features**

  * Preserve **unknown fields** (store raw key+value bytes) and re-emit them on re-encode.

* **Message semantics**

  * Implement **merge** behavior for singular nested message fields when they appear multiple times (merge subfields vs “last wins”).
  * Add **`oneof`** groups (setting one clears the others).

* **Convenience / ergonomics**

  * Deterministic serialization (stable field ordering).
  * Replace `abort()` with structured `EncodeError/DecodeError` carrying index + reason.

* **Safety / robustness**

  * Add limits: max recursion depth, max length-delimited size, max repeated elements.
