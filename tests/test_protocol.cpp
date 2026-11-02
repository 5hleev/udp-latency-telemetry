#include <catch2/catch_test_macros.hpp>
#include "protocol/packet.hpp"

TEST_CASE("SerializePing produces 15 bytes") {
    auto bytes = SerializePing(1, 123456789ULL);
    // TODO(student): после реализации SerializePing раскомментируйте проверку.
    // REQUIRE(bytes.size() == 15);
}

TEST_CASE("PING round trip preserves sequence and timestamp") {
    auto bytes = SerializePing(42, 987654321ULL);
    PacketHeader header{};
    auto payload = ParsePing(bytes.data(), bytes.size(), header);
    // TODO(student): после реализации ParsePing раскомментируйте проверки.
    // REQUIRE(payload.has_value());
    // REQUIRE(header.sequenceNumber == 42);
    // REQUIRE(payload->clientSendTimeUs == 987654321ULL);
}

TEST_CASE("Truncated PING is rejected") {
    auto bytes = SerializePing(1, 100);
    if (!bytes.empty()) bytes.pop_back();
    PacketHeader header{};
    auto payload = ParsePing(bytes.data(), bytes.size(), header);
    REQUIRE_FALSE(payload.has_value());
}

// TODO(student): добавить аналогичные тесты для SerializePong/ParsePong, а также тест
// на пакет с неверной protocolVersion и тест на несовпадение payloadSize.
