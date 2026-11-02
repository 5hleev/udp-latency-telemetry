// protocol/packet.hpp - формат пакетов и явная сериализация.
//
// Модуль не знает ни о сокетах, ни о часах, ни о метриках: на вход - байты,
// на выход - структуры (и наоборот). Поэтому он полностью покрывается модульными
// тестами без сети.
//
// Формат заголовка (7 байт), все многобайтовые поля - big-endian (сетевой порядок):
//
//   +------------+----------------+-------------+-----------------+----------------+
//   | packetType | sequenceNumber | payloadSize | protocolVersion | payload ...    |
//   | uint8      | uint16         | uint16      | uint16          | payloadSize Б  |
//   +------------+----------------+-------------+-----------------+----------------+
//   0            1                3             5                 7
//
// Сериализация выполняется поле за полем (WriteU16/WriteU64/ReadU16/ReadU64), а не
// копированием struct целиком: результат не зависит от выравнивания, порядка байт
// процессора и компилятора. Подробности - docs/Protocol_Specification.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace protocol {

constexpr std::uint16_t kProtocolVersion = 1;

constexpr std::size_t kHeaderSize          = 7;    // 1 + 2 + 2 + 2
constexpr std::size_t kPingPayloadSize     = 8;    // clientSendTimeUs
constexpr std::size_t kPongPayloadSize     = 24;   // 3 x uint64
constexpr std::size_t kMovementPayloadSize = 12;   // 3 x float32
constexpr std::size_t kShootPayloadSize    = 1;    // weaponId
constexpr std::size_t kMaxDatagramSize     = 512;  // с запасом меньше типичного MTU

enum class PacketType : std::uint8_t {
    Ping     = 1,  // C->S  запрос измерения RTT
    Pong     = 2,  // S->C  ответ на PING
    Movement = 3,  // C->S  из ПР №1: желаемая позиция игрока
    Shoot    = 4,  // C->S  из ПР №1: выстрел
};

// Причина, по которой датаграмма отброшена. Нужна для журнала сервера/клиента:
// некорректный пакет не должен ронять процесс, но должен быть виден в логе.
enum class ParseError {
    None,
    TooShort,             // датаграмма короче заголовка
    TooLarge,             // датаграмма больше kMaxDatagramSize
    UnknownType,          // packetType вне enum PacketType
    UnexpectedType,       // тип корректный, но не тот, что ожидает парсер
    BadVersion,           // protocolVersion != kProtocolVersion
    PayloadSizeMismatch,  // payloadSize != фактическая длина данных после заголовка
    BadPayloadLength,     // payloadSize не равен размеру, предписанному для типа
    BadPayloadValue,      // поле payload вне допустимого диапазона (NaN и т.п.)
};

const char* ToString(PacketType type);
const char* ToString(ParseError error);
bool IsKnownPacketType(std::uint8_t raw);

struct PacketHeader {
    PacketType    type            = PacketType::Ping;
    std::uint16_t sequenceNumber  = 0;
    std::uint16_t payloadSize     = 0;
    std::uint16_t protocolVersion = kProtocolVersion;
};

struct PingPayload {
    std::uint64_t clientSendTimeUs = 0;  // монотонные часы клиента, мкс
};

struct PongPayload {
    std::uint64_t clientSendTimeUs    = 0;  // копия из PING (эхо)
    std::uint64_t serverReceiveTimeUs = 0;  // монотонные часы сервера, мкс (диагностика)
    std::uint64_t serverSendTimeUs    = 0;  // монотонные часы сервера, мкс (диагностика)
};

struct MovementPayload {
    float x = 0, y = 0, z = 0;
};

struct ShootPayload {
    std::uint8_t weaponId = 0;
};

// ---------------------------------------------------------------- примитивы
// Запись добавляет байты в конец out, чтение сдвигает cursor и проверяет, что
// не выходит за end. При нехватке байт чтение возвращает nullopt и cursor не меняет.

void WriteU8(std::vector<std::uint8_t>& out, std::uint8_t value);
void WriteU16(std::vector<std::uint8_t>& out, std::uint16_t value);
void WriteU32(std::vector<std::uint8_t>& out, std::uint32_t value);
void WriteU64(std::vector<std::uint8_t>& out, std::uint64_t value);

std::optional<std::uint8_t>  ReadU8(const std::uint8_t*& cursor, const std::uint8_t* end);
std::optional<std::uint16_t> ReadU16(const std::uint8_t*& cursor, const std::uint8_t* end);
std::optional<std::uint32_t> ReadU32(const std::uint8_t*& cursor, const std::uint8_t* end);
std::optional<std::uint64_t> ReadU64(const std::uint8_t*& cursor, const std::uint8_t* end);

// ---------------------------------------------------------------- заголовок

void WriteHeader(std::vector<std::uint8_t>& out, const PacketHeader& header);

// Проверяет всё, что можно проверить без знания типа payload: длину заголовка,
// максимальный размер, packetType, версию и совпадение payloadSize с длиной датаграммы.
std::optional<PacketHeader> ParseHeader(const std::uint8_t* data, std::size_t size,
                                        ParseError* error = nullptr);

// ---------------------------------------------------------------- пакеты
// Parse* возвращают nullopt при любой ошибке (причина - в *error, если передан),
// никогда не бросают исключений и не читают за пределами [data, data + size).

std::vector<std::uint8_t> SerializePing(std::uint16_t sequenceNumber, std::uint64_t clientSendTimeUs);
std::optional<PingPayload> ParsePing(const std::uint8_t* data, std::size_t size,
                                     PacketHeader& outHeader, ParseError* error = nullptr);

std::vector<std::uint8_t> SerializePong(std::uint16_t sequenceNumber, const PongPayload& payload);
std::optional<PongPayload> ParsePong(const std::uint8_t* data, std::size_t size,
                                     PacketHeader& outHeader, ParseError* error = nullptr);

std::vector<std::uint8_t> SerializeMovement(std::uint16_t sequenceNumber, const MovementPayload& payload);
std::optional<MovementPayload> ParseMovement(const std::uint8_t* data, std::size_t size,
                                             PacketHeader& outHeader, ParseError* error = nullptr);

std::vector<std::uint8_t> SerializeShoot(std::uint16_t sequenceNumber, const ShootPayload& payload);
std::optional<ShootPayload> ParseShoot(const std::uint8_t* data, std::size_t size,
                                       PacketHeader& outHeader, ParseError* error = nullptr);

}  // namespace protocol
