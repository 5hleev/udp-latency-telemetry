#include "protocol/packet.hpp"

#include <cmath>
#include <cstring>

namespace protocol {

const char* ToString(PacketType type) {
    switch (type) {
        case PacketType::Ping:     return "PING";
        case PacketType::Pong:     return "PONG";
        case PacketType::Movement: return "MOVEMENT";
        case PacketType::Shoot:    return "SHOOT";
    }
    return "UNKNOWN";
}

const char* ToString(ParseError error) {
    switch (error) {
        case ParseError::None:                return "ok";
        case ParseError::TooShort:            return "датаграмма короче заголовка";
        case ParseError::TooLarge:            return "датаграмма больше максимального размера";
        case ParseError::UnknownType:         return "неизвестный packetType";
        case ParseError::UnexpectedType:      return "неожиданный packetType";
        case ParseError::BadVersion:          return "неподдерживаемая protocolVersion";
        case ParseError::PayloadSizeMismatch: return "payloadSize не совпадает с длиной датаграммы";
        case ParseError::BadPayloadLength:    return "payloadSize не соответствует типу пакета";
        case ParseError::BadPayloadValue:     return "недопустимое значение в payload";
    }
    return "?";
}

bool IsKnownPacketType(std::uint8_t raw) {
    return raw >= static_cast<std::uint8_t>(PacketType::Ping) &&
           raw <= static_cast<std::uint8_t>(PacketType::Shoot);
}

// ---------------------------------------------------------------- примитивы

void WriteU8(std::vector<std::uint8_t>& out, std::uint8_t value) {
    out.push_back(value);
}

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

namespace {

// Общая реализация чтения N байт big-endian. Сначала проверяем границу,
// только потом трогаем память - выход за буфер невозможен.
template <typename T>
std::optional<T> ReadBigEndian(const std::uint8_t*& cursor, const std::uint8_t* end) {
    if (cursor == nullptr || end == nullptr || end < cursor ||
        static_cast<std::size_t>(end - cursor) < sizeof(T)) {
        return std::nullopt;
    }
    T value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value = static_cast<T>((value << 8) | cursor[i]);
    }
    cursor += sizeof(T);
    return value;
}

void SetError(ParseError* out, ParseError value) {
    if (out != nullptr) *out = value;
}

std::uint32_t FloatBits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));  // одно поле IEEE-754, не struct
    return bits;
}

float FloatFromBits(std::uint32_t bits) {
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::vector<std::uint8_t> BeginPacket(PacketType type, std::uint16_t sequenceNumber,
                                      std::size_t payloadSize) {
    std::vector<std::uint8_t> out;
    out.reserve(kHeaderSize + payloadSize);
    PacketHeader header;
    header.type = type;
    header.sequenceNumber = sequenceNumber;
    header.payloadSize = static_cast<std::uint16_t>(payloadSize);
    header.protocolVersion = kProtocolVersion;
    WriteHeader(out, header);
    return out;
}

// Разбор заголовка + проверка, что это ожидаемый тип с ожидаемым размером payload.
// При успехе cursor указывает на начало payload.
std::optional<PacketHeader> ParseTyped(const std::uint8_t* data, std::size_t size,
                                       PacketType expected, std::size_t expectedPayload,
                                       const std::uint8_t*& cursor, ParseError* error) {
    auto header = ParseHeader(data, size, error);
    if (!header) return std::nullopt;
    if (header->type != expected) {
        SetError(error, ParseError::UnexpectedType);
        return std::nullopt;
    }
    if (header->payloadSize != expectedPayload) {
        SetError(error, ParseError::BadPayloadLength);
        return std::nullopt;
    }
    cursor = data + kHeaderSize;
    return header;
}

}  // namespace

std::optional<std::uint8_t> ReadU8(const std::uint8_t*& cursor, const std::uint8_t* end) {
    return ReadBigEndian<std::uint8_t>(cursor, end);
}

std::optional<std::uint16_t> ReadU16(const std::uint8_t*& cursor, const std::uint8_t* end) {
    return ReadBigEndian<std::uint16_t>(cursor, end);
}

std::optional<std::uint32_t> ReadU32(const std::uint8_t*& cursor, const std::uint8_t* end) {
    return ReadBigEndian<std::uint32_t>(cursor, end);
}

std::optional<std::uint64_t> ReadU64(const std::uint8_t*& cursor, const std::uint8_t* end) {
    return ReadBigEndian<std::uint64_t>(cursor, end);
}

// ---------------------------------------------------------------- заголовок

void WriteHeader(std::vector<std::uint8_t>& out, const PacketHeader& header) {
    WriteU8(out, static_cast<std::uint8_t>(header.type));
    WriteU16(out, header.sequenceNumber);
    WriteU16(out, header.payloadSize);
    WriteU16(out, header.protocolVersion);
}

std::optional<PacketHeader> ParseHeader(const std::uint8_t* data, std::size_t size,
                                        ParseError* error) {
    SetError(error, ParseError::None);
    if (data == nullptr || size < kHeaderSize) {
        SetError(error, ParseError::TooShort);
        return std::nullopt;
    }
    if (size > kMaxDatagramSize) {
        SetError(error, ParseError::TooLarge);
        return std::nullopt;
    }

    const std::uint8_t* cursor = data;
    const std::uint8_t* end = data + size;
    const auto rawType = ReadU8(cursor, end);
    const auto sequence = ReadU16(cursor, end);
    const auto payloadSize = ReadU16(cursor, end);
    const auto version = ReadU16(cursor, end);
    if (!rawType || !sequence || !payloadSize || !version) {  // недостижимо после проверки size,
        SetError(error, ParseError::TooShort);                  // но не полагаемся на это
        return std::nullopt;
    }
    if (!IsKnownPacketType(*rawType)) {
        SetError(error, ParseError::UnknownType);
        return std::nullopt;
    }
    if (*version != kProtocolVersion) {
        SetError(error, ParseError::BadVersion);
        return std::nullopt;
    }
    if (*payloadSize != size - kHeaderSize) {
        SetError(error, ParseError::PayloadSizeMismatch);
        return std::nullopt;
    }

    PacketHeader header;
    header.type = static_cast<PacketType>(*rawType);
    header.sequenceNumber = *sequence;
    header.payloadSize = *payloadSize;
    header.protocolVersion = *version;
    return header;
}

// ---------------------------------------------------------------- PING

std::vector<std::uint8_t> SerializePing(std::uint16_t sequenceNumber, std::uint64_t clientSendTimeUs) {
    auto out = BeginPacket(PacketType::Ping, sequenceNumber, kPingPayloadSize);
    WriteU64(out, clientSendTimeUs);
    return out;
}

std::optional<PingPayload> ParsePing(const std::uint8_t* data, std::size_t size,
                                     PacketHeader& outHeader, ParseError* error) {
    const std::uint8_t* cursor = nullptr;
    auto header = ParseTyped(data, size, PacketType::Ping, kPingPayloadSize, cursor, error);
    if (!header) return std::nullopt;

    const std::uint8_t* end = data + size;
    auto sent = ReadU64(cursor, end);
    if (!sent || cursor != end) {
        SetError(error, ParseError::PayloadSizeMismatch);
        return std::nullopt;
    }
    outHeader = *header;
    return PingPayload{*sent};
}

// ---------------------------------------------------------------- PONG

std::vector<std::uint8_t> SerializePong(std::uint16_t sequenceNumber, const PongPayload& payload) {
    auto out = BeginPacket(PacketType::Pong, sequenceNumber, kPongPayloadSize);
    WriteU64(out, payload.clientSendTimeUs);
    WriteU64(out, payload.serverReceiveTimeUs);
    WriteU64(out, payload.serverSendTimeUs);
    return out;
}

std::optional<PongPayload> ParsePong(const std::uint8_t* data, std::size_t size,
                                     PacketHeader& outHeader, ParseError* error) {
    const std::uint8_t* cursor = nullptr;
    auto header = ParseTyped(data, size, PacketType::Pong, kPongPayloadSize, cursor, error);
    if (!header) return std::nullopt;

    const std::uint8_t* end = data + size;
    auto clientSend = ReadU64(cursor, end);
    auto serverReceive = ReadU64(cursor, end);
    auto serverSend = ReadU64(cursor, end);
    if (!clientSend || !serverReceive || !serverSend || cursor != end) {
        SetError(error, ParseError::PayloadSizeMismatch);
        return std::nullopt;
    }
    // Серверные метки взяты с одних и тех же часов: отправка не может быть раньше приёма.
    if (*serverSend < *serverReceive) {
        SetError(error, ParseError::BadPayloadValue);
        return std::nullopt;
    }
    outHeader = *header;
    return PongPayload{*clientSend, *serverReceive, *serverSend};
}

// ---------------------------------------------------------------- MOVEMENT / SHOOT (ПР №1)

std::vector<std::uint8_t> SerializeMovement(std::uint16_t sequenceNumber, const MovementPayload& payload) {
    auto out = BeginPacket(PacketType::Movement, sequenceNumber, kMovementPayloadSize);
    WriteU32(out, FloatBits(payload.x));
    WriteU32(out, FloatBits(payload.y));
    WriteU32(out, FloatBits(payload.z));
    return out;
}

std::optional<MovementPayload> ParseMovement(const std::uint8_t* data, std::size_t size,
                                             PacketHeader& outHeader, ParseError* error) {
    const std::uint8_t* cursor = nullptr;
    auto header = ParseTyped(data, size, PacketType::Movement, kMovementPayloadSize, cursor, error);
    if (!header) return std::nullopt;

    const std::uint8_t* end = data + size;
    auto x = ReadU32(cursor, end);
    auto y = ReadU32(cursor, end);
    auto z = ReadU32(cursor, end);
    if (!x || !y || !z || cursor != end) {
        SetError(error, ParseError::PayloadSizeMismatch);
        return std::nullopt;
    }
    MovementPayload payload{FloatFromBits(*x), FloatFromBits(*y), FloatFromBits(*z)};
    if (!std::isfinite(payload.x) || !std::isfinite(payload.y) || !std::isfinite(payload.z)) {
        SetError(error, ParseError::BadPayloadValue);
        return std::nullopt;
    }
    outHeader = *header;
    return payload;
}

std::vector<std::uint8_t> SerializeShoot(std::uint16_t sequenceNumber, const ShootPayload& payload) {
    auto out = BeginPacket(PacketType::Shoot, sequenceNumber, kShootPayloadSize);
    WriteU8(out, payload.weaponId);
    return out;
}

std::optional<ShootPayload> ParseShoot(const std::uint8_t* data, std::size_t size,
                                       PacketHeader& outHeader, ParseError* error) {
    const std::uint8_t* cursor = nullptr;
    auto header = ParseTyped(data, size, PacketType::Shoot, kShootPayloadSize, cursor, error);
    if (!header) return std::nullopt;

    const std::uint8_t* end = data + size;
    auto weapon = ReadU8(cursor, end);
    if (!weapon || cursor != end) {
        SetError(error, ParseError::PayloadSizeMismatch);
        return std::nullopt;
    }
    outHeader = *header;
    return ShootPayload{*weapon};
}

}  // namespace protocol
