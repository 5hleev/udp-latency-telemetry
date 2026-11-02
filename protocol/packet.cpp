#include "protocol/packet.hpp"

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    // TODO(student): добавить два байта value в big-endian порядке.
}

void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    // TODO(student): добавить восемь байт value в big-endian порядке.
}

std::optional<std::uint16_t> ReadU16(const std::uint8_t*& cursor, const std::uint8_t* end) {
    // TODO(student): прочитать 2 байта, проверив границы буфера.
    return std::nullopt;
}

std::optional<std::uint64_t> ReadU64(const std::uint8_t*& cursor, const std::uint8_t* end) {
    // TODO(student): прочитать 8 байт, проверив границы буфера.
    return std::nullopt;
}

std::vector<std::uint8_t> SerializePing(std::uint16_t sequenceNumber, std::uint64_t clientSendTimeUs) {
    // TODO(student): собрать буфер [type=Ping][sequence][payloadSize=8][version][clientSendTimeUs].
    return {};
}

std::optional<PingPayload> ParsePing(const std::uint8_t* data, std::size_t size, PacketHeader& outHeader) {
    // TODO(student): распарсить заголовок и полезную нагрузку PING с полной валидацией.
    return std::nullopt;
}

std::vector<std::uint8_t> SerializePong(std::uint16_t sequenceNumber, const PongPayload& payload) {
    // TODO(student): собрать буфер PONG (payloadSize = 24: три поля uint64_t).
    return {};
}

std::optional<PongPayload> ParsePong(const std::uint8_t* data, std::size_t size, PacketHeader& outHeader) {
    // TODO(student): распарсить заголовок и полезную нагрузку PONG с полной валидацией.
    return std::nullopt;
}
