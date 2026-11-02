#include <catch2/catch_test_macros.hpp>

#include "protocol/packet.hpp"

#include <cmath>
#include <limits>
#include <vector>

using namespace protocol;

namespace {

ParseError PingError(const std::vector<std::uint8_t>& bytes) {
    PacketHeader header{};
    ParseError error = ParseError::None;
    const auto payload = ParsePing(bytes.data(), bytes.size(), header, &error);
    REQUIRE_FALSE(payload.has_value());
    return error;
}

ParseError PongError(const std::vector<std::uint8_t>& bytes) {
    PacketHeader header{};
    ParseError error = ParseError::None;
    const auto payload = ParsePong(bytes.data(), bytes.size(), header, &error);
    REQUIRE_FALSE(payload.has_value());
    return error;
}

}  // namespace

// ---------------------------------------------------------------- примитивы

TEST_CASE("WriteU16/WriteU64 use network byte order") {
    std::vector<std::uint8_t> out;
    WriteU16(out, 0x1234);
    WriteU64(out, 0x0102030405060708ULL);
    REQUIRE(out == std::vector<std::uint8_t>{0x12, 0x34, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
}

TEST_CASE("ReadU16/ReadU64 round trip extreme values") {
    for (std::uint64_t value : {0ULL, 1ULL, 0xFFULL, 0x8000000000000000ULL, std::numeric_limits<std::uint64_t>::max()}) {
        std::vector<std::uint8_t> out;
        WriteU64(out, value);
        const std::uint8_t* cursor = out.data();
        REQUIRE(ReadU64(cursor, out.data() + out.size()) == value);
        REQUIRE(cursor == out.data() + out.size());
    }
    std::vector<std::uint8_t> out;
    WriteU16(out, 0xFFFF);
    const std::uint8_t* cursor = out.data();
    REQUIRE(ReadU16(cursor, out.data() + out.size()) == 0xFFFF);
}

TEST_CASE("Readers refuse to cross the buffer end and keep the cursor") {
    const std::vector<std::uint8_t> bytes{0x01, 0x02, 0x03};
    const std::uint8_t* cursor = bytes.data();
    REQUIRE_FALSE(ReadU64(cursor, bytes.data() + bytes.size()).has_value());
    REQUIRE(cursor == bytes.data());
    REQUIRE(ReadU16(cursor, bytes.data() + bytes.size()) == 0x0102);
    REQUIRE_FALSE(ReadU16(cursor, bytes.data() + bytes.size()).has_value());
    REQUIRE(ReadU8(cursor, bytes.data() + bytes.size()) == 0x03);
    REQUIRE_FALSE(ReadU8(cursor, bytes.data() + bytes.size()).has_value());
}

// ---------------------------------------------------------------- PING

TEST_CASE("SerializePing produces 15 bytes with exact layout") {
    const auto bytes = SerializePing(0x0102, 0x1122334455667788ULL);
    REQUIRE(bytes.size() == 15);
    REQUIRE(bytes == std::vector<std::uint8_t>{0x01,                     // Ping
                                               0x01, 0x02,               // sequence
                                               0x00, 0x08,               // payloadSize
                                               0x00, 0x01,               // version
                                               0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88});
}

TEST_CASE("PING round trip preserves sequence and timestamp") {
    const auto bytes = SerializePing(42, 987654321ULL);
    PacketHeader header{};
    const auto payload = ParsePing(bytes.data(), bytes.size(), header);
    REQUIRE(payload.has_value());
    REQUIRE(header.type == PacketType::Ping);
    REQUIRE(header.sequenceNumber == 42);
    REQUIRE(header.payloadSize == 8);
    REQUIRE(header.protocolVersion == kProtocolVersion);
    REQUIRE(payload->clientSendTimeUs == 987654321ULL);
}

TEST_CASE("Truncated PING is rejected") {
    auto bytes = SerializePing(1, 100);
    bytes.pop_back();
    REQUIRE(PingError(bytes) == ParseError::PayloadSizeMismatch);
}

TEST_CASE("Every truncation of PING is rejected without crashing") {
    const auto bytes = SerializePing(7, 123);
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        PacketHeader header{};
        REQUIRE_FALSE(ParsePing(bytes.data(), size, header).has_value());
    }
}

TEST_CASE("Datagram shorter than header is TooShort") {
    const auto bytes = SerializePing(1, 1);
    REQUIRE(PingError(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 6)) == ParseError::TooShort);
    PacketHeader header{};
    ParseError error = ParseError::None;
    REQUIRE_FALSE(ParsePing(nullptr, 0, header, &error).has_value());
    REQUIRE(error == ParseError::TooShort);
}

TEST_CASE("PING with wrong protocolVersion is rejected") {
    auto bytes = SerializePing(1, 100);
    bytes[6] = 2;
    REQUIRE(PingError(bytes) == ParseError::BadVersion);
}

TEST_CASE("PING with payloadSize not matching datagram length is rejected") {
    auto bytes = SerializePing(1, 100);
    bytes[4] = 0x01;  // payloadSize = 0x0108
    REQUIRE(PingError(bytes) == ParseError::PayloadSizeMismatch);

    auto extra = SerializePing(1, 100);
    extra.push_back(0);  // лишний байт: payloadSize 8, а данных 9
    REQUIRE(PingError(extra) == ParseError::PayloadSizeMismatch);
}

TEST_CASE("Consistent header but wrong payloadSize for PING is BadPayloadLength") {
    auto bytes = SerializePing(1, 100);
    bytes.push_back(0);
    bytes[4] = 9;  // payloadSize = 9 и действительно 9 байт данных, но PING требует 8
    REQUIRE(PingError(bytes) == ParseError::BadPayloadLength);
}

TEST_CASE("Unknown and unexpected packet types are rejected") {
    auto bytes = SerializePing(1, 100);
    bytes[0] = 0;
    REQUIRE(PingError(bytes) == ParseError::UnknownType);
    bytes[0] = 0xFF;
    REQUIRE(PingError(bytes) == ParseError::UnknownType);

    const auto pong = SerializePong(1, PongPayload{1, 2, 3});
    REQUIRE(PingError(pong) == ParseError::UnexpectedType);
    REQUIRE(PongError(SerializePing(1, 1)) == ParseError::UnexpectedType);
}

TEST_CASE("Oversized datagram is rejected") {
    std::vector<std::uint8_t> huge(kMaxDatagramSize + 1, 0);
    REQUIRE(PingError(huge) == ParseError::TooLarge);
}

// ---------------------------------------------------------------- PONG

TEST_CASE("SerializePong produces 31 bytes") {
    REQUIRE(SerializePong(1, PongPayload{1, 2, 3}).size() == kHeaderSize + kPongPayloadSize);
}

TEST_CASE("PONG round trip preserves all timestamps") {
    const PongPayload sent{0xAABBCCDDEEFF0011ULL, 5'000'000ULL, 5'000'042ULL};
    const auto bytes = SerializePong(65535, sent);
    PacketHeader header{};
    const auto parsed = ParsePong(bytes.data(), bytes.size(), header);
    REQUIRE(parsed.has_value());
    REQUIRE(header.type == PacketType::Pong);
    REQUIRE(header.sequenceNumber == 65535);
    REQUIRE(parsed->clientSendTimeUs == sent.clientSendTimeUs);
    REQUIRE(parsed->serverReceiveTimeUs == sent.serverReceiveTimeUs);
    REQUIRE(parsed->serverSendTimeUs == sent.serverSendTimeUs);
}

TEST_CASE("Malformed PONG variants are rejected") {
    const auto good = SerializePong(9, PongPayload{1, 2, 3});

    auto truncated = good;
    truncated.pop_back();
    REQUIRE(PongError(truncated) == ParseError::PayloadSizeMismatch);

    auto badVersion = good;
    badVersion[5] = 0x01;  // версия 0x0101
    REQUIRE(PongError(badVersion) == ParseError::BadVersion);

    auto badSize = good;
    badSize[4] = 8;
    REQUIRE(PongError(badSize) == ParseError::PayloadSizeMismatch);

    // Сервер не мог отправить ответ раньше, чем получил запрос.
    REQUIRE(PongError(SerializePong(9, PongPayload{1, 100, 99})) == ParseError::BadPayloadValue);
}

TEST_CASE("Failed parse does not modify output header") {
    PacketHeader header{};
    header.sequenceNumber = 777;
    auto bytes = SerializePing(1, 1);
    bytes[6] = 9;
    REQUIRE_FALSE(ParsePing(bytes.data(), bytes.size(), header).has_value());
    REQUIRE(header.sequenceNumber == 777);
}

// ---------------------------------------------------------------- MOVEMENT / SHOOT

TEST_CASE("MOVEMENT and SHOOT round trip") {
    const auto movementBytes = SerializeMovement(3, MovementPayload{1.5f, -2.25f, 1000.0f});
    REQUIRE(movementBytes.size() == kHeaderSize + kMovementPayloadSize);
    PacketHeader header{};
    const auto movement = ParseMovement(movementBytes.data(), movementBytes.size(), header);
    REQUIRE(movement.has_value());
    REQUIRE(movement->x == 1.5f);
    REQUIRE(movement->y == -2.25f);
    REQUIRE(movement->z == 1000.0f);

    const auto shootBytes = SerializeShoot(4, ShootPayload{7});
    const auto shoot = ParseShoot(shootBytes.data(), shootBytes.size(), header);
    REQUIRE(shoot.has_value());
    REQUIRE(shoot->weaponId == 7);
    REQUIRE(header.type == PacketType::Shoot);
}

TEST_CASE("MOVEMENT with NaN coordinate is rejected") {
    auto bytes = SerializeMovement(3, MovementPayload{std::numeric_limits<float>::quiet_NaN(), 0, 0});
    PacketHeader header{};
    ParseError error = ParseError::None;
    REQUIRE_FALSE(ParseMovement(bytes.data(), bytes.size(), header, &error).has_value());
    REQUIRE(error == ParseError::BadPayloadValue);
}

TEST_CASE("Random garbage never crashes the parsers") {
    std::uint32_t state = 12345;
    auto next = [&]() {
        state = state * 1664525u + 1013904223u;
        return static_cast<std::uint8_t>(state >> 24);
    };
    for (int iteration = 0; iteration < 5000; ++iteration) {
        std::vector<std::uint8_t> bytes(next() % 40);
        for (auto& b : bytes) b = next();
        PacketHeader header{};
        (void)ParseHeader(bytes.data(), bytes.size());
        (void)ParsePing(bytes.data(), bytes.size(), header);
        (void)ParsePong(bytes.data(), bytes.size(), header);
        (void)ParseMovement(bytes.data(), bytes.size(), header);
        (void)ParseShoot(bytes.data(), bytes.size(), header);
    }
    SUCCEED();
}
