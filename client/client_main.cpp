// client/client_main.cpp - UDP-клиент измерения RTT.
//
// Отправляет серию PING с постоянным интервалом, принимает PONG, передаёт события
// в Telemetry и пишет каждое измерение в CSV-журнал. В конце печатает сводку серии.
//
//   client --experiment baseline [--host 127.0.0.1] [--port 27015] [--count 50]
//          [--interval 500] [--timeout 1000] [--drain 1500] [--csv docs/latency_samples.csv]
//          [--log client.log] [--seq-start N] [--send-invalid] [--verbose]
//
// Повторная отправка не выполняется (это задача ПР №3): attempts всегда равно 1.

#include "common/cli.hpp"
#include "common/clock.hpp"
#include "common/logger.hpp"
#include "protocol/packet.hpp"
#include "telemetry/sample_log.hpp"
#include "telemetry/series_stats.hpp"
#include "telemetry/telemetry.hpp"
#include "transport/udp_socket.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace {

struct ClientConfig {
    std::string host = "127.0.0.1";
    std::uint16_t port = 27015;
    std::string experimentId;
    std::uint32_t count = 50;
    std::uint64_t intervalUs = 500'000;
    std::uint64_t timeoutUs = telemetry::kDefaultTimeoutUs;
    std::uint64_t drainUs = 1'500'000;
    std::string csvPath = "docs/latency_samples.csv";
    std::string logPath;
    std::uint16_t seqStart = 0;
    bool sendInvalid = false;
    bool verbose = false;
};

bool IsValidExperimentId(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}

std::string Ms(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", value);
    return buffer;
}

// CSV-журнал: заголовок пишется, только если файл новый/пустой.
class CsvJournal {
public:
    explicit CsvJournal(const std::string& path) {
        bool needHeader = true;
        {
            std::ifstream existing(path, std::ios::binary | std::ios::ate);
            needHeader = !existing || existing.tellg() == 0;
        }
        out_.open(path, std::ios::app | std::ios::binary);
        if (out_ && needHeader) out_ << telemetry::CsvHeader() << '\n';
    }
    bool Ok() const { return static_cast<bool>(out_); }
    void Write(const telemetry::Sample& sample) {
        out_ << telemetry::FormatCsvRow(sample) << '\n';
        out_.flush();
    }

private:
    std::ofstream out_;
};

class LatencyClient {
public:
    LatencyClient(const ClientConfig& config, transport::UdpSocket& socket, const transport::Endpoint& server,
                  common::Logger& log, CsvJournal& csv)
        : config_(config),
          socket_(socket),
          server_(server),
          log_(log),
          csv_(csv),
          telemetry_(config.experimentId, config.timeoutUs) {}

    void Run() {
        startUs_ = common::NowUs();
        std::uint16_t sequence = config_.seqStart;
        std::uint32_t sent = 0;
        std::uint64_t nextSendUs = startUs_;
        std::uint64_t finishUs = 0;  // момент окончания приёма после последнего PING

        while (true) {
            std::uint64_t now = common::NowUs();
            if (sent < config_.count && now >= nextSendUs) {
                SendPing(sequence++, now);
                ++sent;
                // Расписание от начала серии, а не от предыдущей отправки: интервал не "плывёт".
                nextSendUs = startUs_ + static_cast<std::uint64_t>(sent) * config_.intervalUs;
                if (sent == config_.count) finishUs = now + config_.timeoutUs + config_.drainUs;
            }

            now = common::NowUs();
            for (const auto& record : telemetry_.ExpireOverdue(now)) WriteTimeout(record);

            if (sent == config_.count && now >= finishUs) break;

            std::uint64_t wakeUs = sent < config_.count ? nextSendUs : finishUs;
            if (auto deadline = telemetry_.NextDeadlineUs()) wakeUs = std::min(wakeUs, *deadline);
            const std::uint64_t waitUs = wakeUs > now ? wakeUs - now : 0;
            if (socket_.WaitReadable(waitUs)) ReceiveAll();
        }
        PrintSummary();
    }

private:
    void SendPing(std::uint16_t sequence, std::uint64_t now) {
        const auto bytes = protocol::SerializePing(sequence, now);
        auto result = telemetry_.OnPingSent(sequence, now);
        if (result.evicted) WriteTimeout(*result.evicted);
        if (!socket_.SendTo(bytes, server_)) {
            // Пакет не ушёл в сеть: запись остаётся в inFlight и закончится тайм-аутом,
            // так ошибка отправки честно попадает в долю потерь.
            log_.Warn("sendto() не удался для seq=" + std::to_string(sequence) + ": " + transport::LastErrorText());
        }
        log_.Detail("-> PING #" + std::to_string(result.record.sample) + " seq=" + std::to_string(sequence));
    }

    void ReceiveAll() {
        while (auto datagram = socket_.TryReceive()) {
            const std::uint64_t receivedUs = common::NowUs();  // метка сразу после recvfrom
            HandleDatagram(*datagram, receivedUs);
        }
    }

    void HandleDatagram(const transport::Datagram& datagram, std::uint64_t receivedUs) {
        if (datagram.from != server_) {
            ++invalid_;
            log_.Warn("ОТБРОШЕНА датаграмма от постороннего узла " + datagram.from.ToString());
            return;
        }
        protocol::PacketHeader header;
        protocol::ParseError error = protocol::ParseError::None;
        const auto pong = protocol::ParsePong(datagram.data.data(), datagram.data.size(), header, &error);
        if (!pong || datagram.truncated) {
            ++invalid_;
            log_.Warn("ОТБРОШЕНА датаграмма (" + std::to_string(datagram.data.size()) +
                      " байт): " + protocol::ToString(error));
            return;
        }

        const auto result = telemetry_.OnPong(header.sequenceNumber, receivedUs, pong->clientSendTimeUs);
        telemetry::Sample row;
        row.experimentId = config_.experimentId;
        row.sequence = header.sequenceNumber;
        row.status = result.status;
        if (result.record) {
            row.sample = result.record->sample;
            row.sentAtMs = RelativeMs(result.record->sentAtUs);
        }

        switch (result.status) {
            case telemetry::ResponseStatus::Received:
                row.rttMs = result.rttMs;
                row.srttMs = result.srttMs;
                log_.Detail("<- PONG #" + std::to_string(*row.sample) + " seq=" + std::to_string(row.sequence) +
                            " RTT=" + Ms(result.rttMs) + " мс SRTT=" + Ms(result.srttMs) + " мс (сервер: " +
                            std::to_string(pong->serverSendTimeUs - pong->serverReceiveTimeUs) + " мкс)");
                break;
            case telemetry::ResponseStatus::Late:
                row.rttMs = result.rttMs;
                log_.Warn("late_response: seq=" + std::to_string(row.sequence) + " пришёл через " +
                          Ms(result.rttMs) + " мс (тайм-аут " + Ms(config_.timeoutUs / 1000.0) + " мс)");
                break;
            case telemetry::ResponseStatus::Duplicate:
                row.rttMs = result.rttMs;
                log_.Warn("duplicate_response: повторный PONG seq=" + std::to_string(row.sequence));
                break;
            case telemetry::ResponseStatus::Unknown:
                log_.Warn("unknown_response: PONG seq=" + std::to_string(row.sequence) +
                          " не соответствует ни одному измерению в inFlight");
                break;
            case telemetry::ResponseStatus::Timeout:
                break;
        }
        if (result.status == telemetry::ResponseStatus::Late && result.record) {
            // Если тайм-аут обнаружен в OnPong раньше, чем в ExpireOverdue, строка timeout
            // ещё не записана - пишем её перед late_response (повтор WriteTimeout отсекает).
            WriteTimeout(*result.record);
        }
        Record(row);
    }

    void WriteTimeout(const telemetry::FlightRecord& record) {
        auto pos = std::lower_bound(timeoutWritten_.begin(), timeoutWritten_.end(), record.sample);
        if (pos != timeoutWritten_.end() && *pos == record.sample) return;
        timeoutWritten_.insert(pos, record.sample);

        telemetry::Sample row;
        row.experimentId = record.experimentId;
        row.sample = record.sample;
        row.sequence = record.sequence;
        row.sentAtMs = RelativeMs(record.sentAtUs);
        row.status = telemetry::ResponseStatus::Timeout;
        log_.Detail("timeout: PING #" + std::to_string(record.sample) + " seq=" + std::to_string(record.sequence) +
                    " без ответа дольше " + Ms(config_.timeoutUs / 1000.0) + " мс");
        Record(row);
    }

    void Record(const telemetry::Sample& row) {
        csv_.Write(row);
        rows_.push_back(row);
    }

    double RelativeMs(std::uint64_t us) const { return static_cast<double>(us - startUs_) / 1000.0; }

    void PrintSummary() {
        const auto stats = telemetry::ComputeSeriesStats(config_.experimentId, rows_);
        const auto& counters = telemetry_.GetCounters();
        log_.Info("Серия '" + config_.experimentId + "': отправлено " + std::to_string(stats.sent) + ", получено " +
                  std::to_string(stats.received) + ", тайм-аутов " + std::to_string(stats.timeouts) +
                  " (loss " + Ms(stats.lossRatePct) + " %)");
        log_.Info("  RTT, мс: min " + Ms(stats.minRttMs) + " / mean " + Ms(stats.meanRttMs) + " / median " +
                  Ms(stats.medianRttMs) + " / max " + Ms(stats.maxRttMs) + "; SRTT " + Ms(stats.finalSrttMs) +
                  "; jitter " + Ms(stats.jitterMs));
        log_.Info("  ответы: late " + std::to_string(stats.late) + ", duplicate " + std::to_string(stats.duplicates) +
                  ", unknown " + std::to_string(stats.unknown) + ", некорректных датаграмм " + std::to_string(invalid_));
        // Перекрёстная проверка: онлайн-оценки Telemetry должны совпадать с расчётом по журналу.
        log_.Info("  Telemetry (онлайн): SRTT " + Ms(telemetry_.SrttMs()) + ", jitter " +
                  Ms(telemetry_.MeanJitterMs()) + ", received " + std::to_string(counters.received) +
                  ", timeouts " + std::to_string(counters.timeouts));
    }

    const ClientConfig& config_;
    transport::UdpSocket& socket_;
    transport::Endpoint server_;
    common::Logger& log_;
    CsvJournal& csv_;
    telemetry::Telemetry telemetry_;
    std::uint64_t startUs_ = 0;
    std::vector<telemetry::Sample> rows_;
    std::vector<std::uint32_t> timeoutWritten_;  // отсортированные sample, для которых записан timeout
    std::size_t invalid_ = 0;
};

// Набор заведомо некорректных датаграмм для проверки валидации на сервере.
// Ни на одну из них сервер не должен отвечать, каждая должна попасть в его журнал.
std::vector<std::pair<std::string, std::vector<std::uint8_t>>> MakeInvalidDatagrams() {
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> list;
    const auto ping = protocol::SerializePing(0xBEEF, 123456789);

    list.emplace_back("пустая датаграмма", std::vector<std::uint8_t>{});
    list.emplace_back("обрезанный заголовок (4 байта)", std::vector<std::uint8_t>(ping.begin(), ping.begin() + 4));

    auto truncated = ping;
    truncated.pop_back();
    list.emplace_back("PING без последнего байта", truncated);

    auto badType = ping;
    badType[0] = 0x63;
    list.emplace_back("неизвестный packetType=0x63", badType);

    auto badVersion = ping;
    badVersion[6] = 0x02;
    list.emplace_back("protocolVersion=2", badVersion);

    auto badSize = ping;
    badSize[4] = 0x20;  // payloadSize = 32, а фактически 8
    list.emplace_back("payloadSize=32 при фактических 8 байтах", badSize);

    auto extra = ping;
    extra.push_back(0x00);
    list.emplace_back("лишний байт после payload", extra);

    list.emplace_back("PONG, отправленный серверу", protocol::SerializePong(0xBEEF, protocol::PongPayload{1, 2, 3}));

    std::vector<std::uint8_t> huge(600, 0xAA);
    list.emplace_back("датаграмма 600 байт", huge);

    auto nanMovement = protocol::SerializeMovement(0xBEF0, protocol::MovementPayload{0, 0, 0});
    for (std::size_t i = protocol::kHeaderSize; i < protocol::kHeaderSize + 4; ++i) nanMovement[i] = 0xFF;  // NaN
    list.emplace_back("MOVEMENT с x=NaN", nanMovement);
    return list;
}

void SendInvalidDatagrams(transport::UdpSocket& socket, const transport::Endpoint& server, common::Logger& log) {
    log.Info("Отправка некорректных датаграмм для проверки валидации на сервере:");
    for (const auto& [name, bytes] : MakeInvalidDatagrams()) {
        socket.SendTo(bytes, server);
        log.Info("  -> " + name + " (" + std::to_string(bytes.size()) + " байт)");
    }
    // Корректные команды ПР №1 - сервер должен их принять и залогировать.
    socket.SendTo(protocol::SerializeMovement(0xBEF1, protocol::MovementPayload{1.5f, 0.0f, -2.25f}), server);
    socket.SendTo(protocol::SerializeShoot(0xBEF2, protocol::ShootPayload{3}), server);
    log.Info("  -> корректные MOVEMENT и SHOOT");

    // Ответов быть не должно; всё, что придёт за 300 мс, - ошибка сервера.
    const std::uint64_t until = common::NowUs() + 300'000;
    std::size_t unexpected = 0;
    while (common::NowUs() < until) {
        if (socket.WaitReadable(until - common::NowUs())) {
            while (socket.TryReceive()) ++unexpected;
        }
    }
    log.Info("  ответов на некорректные датаграммы: " + std::to_string(unexpected) + " (ожидается 0)");
}

void PrintUsage() {
    std::printf(
        "client --experiment ID [--host 127.0.0.1] [--port 27015] [--count 50] [--interval 500]\n"
        "       [--timeout 1000] [--drain 1500] [--csv docs/latency_samples.csv] [--log FILE]\n"
        "       [--seq-start N] [--send-invalid] [--verbose]\n");
}

}  // namespace

int main(int argc, char** argv) {
    common::SetupConsole();
    common::ArgParser args(argc, argv, {"send-invalid", "verbose", "help"});
    if (args.Has("help")) {
        PrintUsage();
        return 0;
    }

    ClientConfig config;
    config.host = args.GetString("host", config.host);
    config.port = static_cast<std::uint16_t>(args.GetInt("port", config.port, 1, 65535));
    config.experimentId = args.GetString("experiment", "");
    config.count = static_cast<std::uint32_t>(args.GetInt("count", config.count, 1, 10000));
    config.intervalUs = static_cast<std::uint64_t>(args.GetInt("interval", 500, 10, 60'000)) * 1000;
    config.timeoutUs = static_cast<std::uint64_t>(args.GetInt("timeout", 1000, 1, 60'000)) * 1000;
    config.drainUs = static_cast<std::uint64_t>(args.GetInt("drain", 1500, 0, 60'000)) * 1000;
    config.csvPath = args.GetString("csv", config.csvPath);
    config.logPath = args.GetString("log", "");
    config.sendInvalid = args.Has("send-invalid");
    config.verbose = args.Has("verbose");
    if (args.Has("seq-start")) {
        config.seqStart = static_cast<std::uint16_t>(args.GetInt("seq-start", 0, 0, 65535));
    } else {
        // Случайный начальный номер (как ISN в TCP): ответы прошлых запусков не совпадут с новыми.
        config.seqStart = static_cast<std::uint16_t>(std::random_device{}() & 0xFFFF);
    }
    args.RejectUnknown({"host", "port", "experiment", "count", "interval", "timeout", "drain", "csv", "log",
                        "seq-start", "send-invalid", "verbose", "help"});
    if (!IsValidExperimentId(config.experimentId)) {
        std::fprintf(stderr, "Ошибка: --experiment обязателен (латиница, цифры, '_' и '-', до 32 символов)\n");
        PrintUsage();
        return 2;
    }
    if (!args.Ok()) {
        for (const auto& error : args.Errors()) std::fprintf(stderr, "Ошибка: %s\n", error.c_str());
        return 2;
    }

    common::Logger log(config.logPath);
    log.SetConsoleDetails(config.verbose);
    if (config.intervalUs < 200'000 || config.intervalUs > 1'000'000) {
        log.Warn("Интервал вне рекомендованного диапазона 200-1000 мс");
    }

    transport::NetworkContext network;
    transport::UdpSocket socket;
    const auto server = transport::Endpoint::Resolve(config.host, config.port);
    if (!network.Ok() || !server || !socket.Open()) {
        log.Warn("Не удалось подготовить сокет или разрешить адрес " + config.host + ": " + transport::LastErrorText());
        return 1;
    }

    CsvJournal csv(config.csvPath);
    if (!csv.Ok()) {
        log.Warn("Не удалось открыть CSV-журнал " + config.csvPath);
        return 1;
    }

    common::HighResolutionTimerScope timerResolution;
    if (config.sendInvalid) SendInvalidDatagrams(socket, *server, log);

    log.Info("Серия '" + config.experimentId + "': " + std::to_string(config.count) + " PING на " +
             server->ToString() + ", интервал " + std::to_string(config.intervalUs / 1000) + " мс, тайм-аут " +
             std::to_string(config.timeoutUs / 1000) + " мс, начальный seq=" + std::to_string(config.seqStart));
    LatencyClient client(config, socket, *server, log, csv);
    client.Run();
    return 0;
}
