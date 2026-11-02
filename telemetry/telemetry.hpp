// telemetry/telemetry.hpp - учёт измерений RTT на стороне клиента.
//
// Класс не работает с сокетами и не читает часы сам: всё время передаётся
// аргументом nowUs (микросекунды монотонных часов). Благодаря этому логику
// тайм-аутов, дублей и запоздалых ответов можно проверить в тестах детерминированно,
// "прокручивая" время вручную.
//
// Жизненный цикл записи inFlight (см. диаграмму в docs/Protocol_Specification.md):
//
//   OnPingSent ──> InFlight ──(PONG, RTT <= timeout)──> Received ──(ещё PONG)──> duplicate_response
//                     │
//                     ├──(PONG, RTT > timeout)────────> TimedOut + late_response
//                     └──(ExpireOverdue, RTT > timeout)> TimedOut ──(PONG)──> late_response
//
// PONG с номером, которого нет в inFlight (или с чужой меткой clientSendTimeUs),
// получает статус unknown_response.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace telemetry {

constexpr std::uint64_t kDefaultTimeoutUs = 1'000'000;  // 1000 мс
constexpr std::size_t   kDefaultCapacity  = 256;        // размер окна inFlight
constexpr double        kSrttAlpha        = 0.125;      // RFC 6298

// Статус строки журнала / результата обработки ответа.
enum class ResponseStatus {
    Received,   // PONG пришёл в пределах тайм-аута, RTT засчитан
    Timeout,    // ответ не пришёл за отведённое время
    Late,       // PONG пришёл после тайм-аута
    Duplicate,  // повторный PONG на уже завершённое измерение
    Unknown,    // PONG с номером, которого нет в inFlight
};

const char* ToCsvStatus(ResponseStatus status);
std::optional<ResponseStatus> ParseCsvStatus(const std::string& text);

enum class FlightState { InFlight, Received, TimedOut };

struct FlightRecord {
    std::string   experimentId;      // идентификатор серии
    std::uint32_t sample = 0;        // порядковый номер измерения в серии, с 1
    std::uint16_t sequence = 0;      // sequenceNumber пакета
    std::uint64_t sentAtUs = 0;      // время отправки PING по часам клиента
    FlightState   state = FlightState::InFlight;
    std::uint32_t attempts = 1;      // число отправок; в ПР №2 повторов нет, всегда 1
    std::uint64_t rttUs = 0;         // заполнено для Received
};

struct SendResult {
    FlightRecord record;                  // только что зарегистрированная запись
    std::optional<FlightRecord> evicted;  // незавершённая запись, вытесненная из окна
};                                        // (засчитывается как timeout)

struct PongResult {
    ResponseStatus status = ResponseStatus::Unknown;
    std::optional<FlightRecord> record;  // пусто для Unknown
    double rttMs = 0.0;                  // фактическое время ответа (Received / Late)
    double srttMs = 0.0;                 // SRTT после обработки
};

struct Counters {
    std::size_t sent = 0;
    std::size_t received = 0;
    std::size_t timeouts = 0;
    std::size_t late = 0;
    std::size_t duplicates = 0;
    std::size_t unknown = 0;
};

class Telemetry {
public:
    explicit Telemetry(std::string experimentId,
                       std::uint64_t timeoutUs = kDefaultTimeoutUs,
                       std::size_t capacity = kDefaultCapacity);

    // Регистрирует отправленный PING. Окно inFlight ограничено: запись хранится в
    // слоте sequence % capacity. Если слот занят незавершённым измерением другого
    // номера, оно принудительно считается timeout и возвращается в evicted.
    SendResult OnPingSent(std::uint16_t sequence, std::uint64_t nowUs);

    // Обрабатывает PONG. echoedSendUs - метка clientSendTimeUs из пакета: если она
    // передана и не совпадает с временем отправки записи, ответ относится не к этому
    // измерению (например, номер пошёл на второй круг) и считается unknown_response.
    PongResult OnPong(std::uint16_t sequence, std::uint64_t nowUs,
                      std::optional<std::uint64_t> echoedSendUs = std::nullopt);

    // Переводит в TimedOut все записи InFlight, ожидающие дольше timeout.
    // Возвращает их в порядке отправки - для журнала.
    std::vector<FlightRecord> ExpireOverdue(std::uint64_t nowUs);

    // Момент, когда истечёт ближайший тайм-аут (для расчёта ожидания в recv).
    std::optional<std::uint64_t> NextDeadlineUs() const;
    std::size_t InFlightCount() const;

    double SrttMs() const { return srttMs_; }
    double LastRttMs() const { return lastRttMs_; }
    double MeanJitterMs() const;
    std::size_t SampleCount() const { return counters_.received; }
    const Counters& GetCounters() const { return counters_; }
    std::uint64_t TimeoutUs() const { return timeoutUs_; }
    std::size_t Capacity() const { return slots_.size(); }

private:
    FlightRecord* Find(std::uint16_t sequence);
    void MarkTimedOut(FlightRecord& record);

    std::string experimentId_;
    std::uint64_t timeoutUs_;
    std::vector<std::optional<FlightRecord>> slots_;  // ограниченный контейнер inFlight

    Counters counters_;
    std::uint32_t nextSample_ = 1;
    double srttMs_ = 0.0;
    double lastRttMs_ = 0.0;
    double jitterAccumulatorMs_ = 0.0;
    bool hasPreviousRtt_ = false;
};

}  // namespace telemetry
