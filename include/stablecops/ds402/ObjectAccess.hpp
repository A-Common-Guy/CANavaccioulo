#pragma once

#include <cstdint>

namespace stablecops::ds402 {

// Integer width/sign of an object dictionary entry, used to drive a raw SDO
// write with the correct CANopen data type.
enum class ObjectWidth : uint8_t { U8, U16, U32, I8, I16, I32 };

// A single raw object-dictionary write requested at boot (over SDO, while the
// node is pre-operational). Used for ad-hoc drive configuration/experimentation
// without a dedicated field per object.
struct ObjectWrite {
    uint16_t index{0};
    uint8_t subindex{0};
    ObjectWidth width{ObjectWidth::U32};
    int64_t value{0};
};

class ObjectAccess {
public:
    virtual ~ObjectAccess() = default;

    virtual uint8_t readU8(uint16_t index, uint8_t subindex) = 0;
    virtual uint16_t readU16(uint16_t index, uint8_t subindex) = 0;
    virtual uint32_t readU32(uint16_t index, uint8_t subindex) = 0;
    virtual int32_t readI32(uint16_t index, uint8_t subindex) = 0;

    virtual void writeU8(uint16_t index, uint8_t subindex, uint8_t value) = 0;
    virtual void writeU16(uint16_t index, uint8_t subindex, uint16_t value) = 0;
    virtual void writeU32(uint16_t index, uint8_t subindex, uint32_t value) = 0;
    virtual void writeI32(uint16_t index, uint8_t subindex, int32_t value) = 0;
};

}  // namespace stablecops::ds402
