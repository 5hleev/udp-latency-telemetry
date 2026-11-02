#pragma once
#include <cstdint>
#include <optional>
#include <vector>

// Формат заголовка пакета:
// [ packetType : 1 байт ][ sequenceNumber : 2 байта ][ payloadSize : 2 байта ][ protocolVersion : 2 байта ]
// Далее следует полезная нагрузка размером payloadSize байт.
// Все многобайтовые поля передаются в сетевом порядке байт (big-endian).

constexpr std::uint16_t kProtocolVersion = 1;

enum class PacketType : std::uint8_t {
    Ping = 1,
    Pong = 2,
    Movement = 3, // из ПР №1
    Shoot = 4     // из ПР №1
};

struct PacketHeader {
    PacketType type;
    std::uint16_t sequenceNumber;
    std::uint16_t payloadSize;
    std::uint16_t protocolVersion;
};

struct PingPayload {
    std::uint64_t clientSendTimeUs;
};

struct PongPayload {
    std::uint64_t clientSendTimeUs;
    std::uint64_t serverReceiveTimeUs;
    std::uint64_t serverSendTimeUs;
};

// --- Низкоуровневые помощники сериализации ---
// Реализуйте в src/protocol.cpp. Не используйте memcpy структуры целиком —
// порядок байт и выравнивание платформозависимы.

void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value);
void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t value);

std::optional<std::uint16_t> ReadU16(const std::uint8_t*& cursor, const std::uint8_t* end);
std::optional<std::uint64_t> ReadU64(const std::uint8_t*& cursor, const std::uint8_t* end);

// --- PING ---

// TODO(student): реализовать сериализацию PING в бинарный буфер.
// Ожидаемый размер результата: 7 (заголовок) + 8 (payload) = 15 байт.
std::vector<std::uint8_t> SerializePing(std::uint16_t sequenceNumber, std::uint64_t clientSendTimeUs);

// TODO(student): реализовать разбор PING с полной валидацией:
// - совпадение типа пакета,
// - совпадение protocolVersion,
// - совпадение payloadSize фактическому размеру данных,
// - отсутствие выхода за границы буфера.
// При любой ошибке функция должна возвращать std::nullopt, а не бросать исключение
// и не читать за пределами буфера.
std::optional<PingPayload> ParsePing(const std::uint8_t* data, std::size_t size, PacketHeader& outHeader);

// --- PONG ---

// TODO(student): реализовать сериализацию PONG (аналогично PING, но с тремя полями uint64_t).
std::vector<std::uint8_t> SerializePong(std::uint16_t sequenceNumber, const PongPayload& payload);

// TODO(student): реализовать разбор PONG с той же логикой валидации, что и ParsePing.
std::optional<PongPayload> ParsePong(const std::uint8_t* data, std::size_t size, PacketHeader& outHeader);
