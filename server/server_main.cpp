// server/server_main.cpp - UDP-сервер: отвечает PONG на PING, логирует команды ПР №1
// и отбрасывает некорректные датаграммы (с записью причины в журнал).
//
// Здесь только склейка модулей: transport принимает байты, protocol их разбирает,
// сервер решает, что ответить. Никакой искусственной задержки сервер не добавляет -
// условия сети создаёт отдельный эмулятор netem_proxy.
//
//   server [--port 27015] [--bind 0.0.0.0] [--log server.log] [--verbose] [--idle-exit SEC]
//
// --idle-exit SEC: завершиться, если SEC секунд не было датаграмм (после первой);
// нужно скрипту экспериментов, чтобы сервер успел записать итоговую сводку в журнал.

#include "common/cli.hpp"
#include "common/clock.hpp"
#include "common/logger.hpp"
#include "protocol/packet.hpp"
#include "transport/udp_socket.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <map>
#include <string>

namespace {

std::atomic<bool> g_stop{false};

void OnSignal(int) {
    g_stop = true;
}

struct ServerStats {
    std::size_t datagrams = 0;
    std::size_t pings = 0;
    std::size_t commands = 0;
    std::map<std::string, std::size_t> dropped;  // причина -> количество
};

std::string FormatFloat(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
    return buffer;
}

class LatencyServer {
public:
    LatencyServer(transport::UdpSocket& socket, common::Logger& log) : socket_(socket), log_(log) {}

    void Handle(const transport::Datagram& datagram, std::uint64_t receivedUs) {
        ++stats_.datagrams;
        const std::string from = datagram.from.ToString();
        if (datagram.truncated) {
            Drop(from, datagram.data.size(), "датаграмма больше буфера приёма");
            return;
        }

        protocol::ParseError error = protocol::ParseError::None;
        const auto header = protocol::ParseHeader(datagram.data.data(), datagram.data.size(), &error);
        if (!header) {
            Drop(from, datagram.data.size(), protocol::ToString(error));
            return;
        }

        switch (header->type) {
            case protocol::PacketType::Ping:     HandlePing(datagram, from, receivedUs); break;
            case protocol::PacketType::Movement: HandleMovement(datagram, from); break;
            case protocol::PacketType::Shoot:    HandleShoot(datagram, from); break;
            case protocol::PacketType::Pong:
                Drop(from, datagram.data.size(), "PONG не принимается сервером (направление S->C)");
                break;
        }
    }

    const ServerStats& Stats() const { return stats_; }

private:
    void HandlePing(const transport::Datagram& datagram, const std::string& from, std::uint64_t receivedUs) {
        protocol::PacketHeader header;
        protocol::ParseError error = protocol::ParseError::None;
        const auto ping = protocol::ParsePing(datagram.data.data(), datagram.data.size(), header, &error);
        if (!ping) {
            Drop(from, datagram.data.size(), std::string("PING: ") + protocol::ToString(error));
            return;
        }

        protocol::PongPayload pong;
        pong.clientSendTimeUs = ping->clientSendTimeUs;
        pong.serverReceiveTimeUs = receivedUs;
        pong.serverSendTimeUs = common::NowUs();  // непосредственно перед отправкой
        const auto bytes = protocol::SerializePong(header.sequenceNumber, pong);
        const bool sent = socket_.SendTo(bytes, datagram.from);
        ++stats_.pings;

        const auto processingUs = pong.serverSendTimeUs - pong.serverReceiveTimeUs;
        if (sent) {
            log_.Detail(from + " PING seq=" + std::to_string(header.sequenceNumber) + " -> PONG (" +
                        std::to_string(bytes.size()) + " байт, обработка " + std::to_string(processingUs) +
                        " мкс)");
        } else {
            log_.Warn(from + " PING seq=" + std::to_string(header.sequenceNumber) +
                      " -> не удалось отправить PONG: " + transport::LastErrorText());
        }
    }

    void HandleMovement(const transport::Datagram& datagram, const std::string& from) {
        protocol::PacketHeader header;
        protocol::ParseError error = protocol::ParseError::None;
        const auto movement = protocol::ParseMovement(datagram.data.data(), datagram.data.size(), header, &error);
        if (!movement) {
            Drop(from, datagram.data.size(), std::string("MOVEMENT: ") + protocol::ToString(error));
            return;
        }
        ++stats_.commands;
        log_.Info(from + " seq=" + std::to_string(header.sequenceNumber) + " MOVEMENT (" +
                  FormatFloat(movement->x) + ", " + FormatFloat(movement->y) + ", " + FormatFloat(movement->z) + ")");
    }

    void HandleShoot(const transport::Datagram& datagram, const std::string& from) {
        protocol::PacketHeader header;
        protocol::ParseError error = protocol::ParseError::None;
        const auto shoot = protocol::ParseShoot(datagram.data.data(), datagram.data.size(), header, &error);
        if (!shoot) {
            Drop(from, datagram.data.size(), std::string("SHOOT: ") + protocol::ToString(error));
            return;
        }
        ++stats_.commands;
        log_.Info(from + " seq=" + std::to_string(header.sequenceNumber) +
                  " SHOOT weaponId=" + std::to_string(shoot->weaponId));
    }

    void Drop(const std::string& from, std::size_t size, const std::string& reason) {
        ++stats_.dropped[reason];
        log_.Warn("ОТБРОШЕНА датаграмма от " + from + " (" + std::to_string(size) + " байт): " + reason);
    }

    transport::UdpSocket& socket_;
    common::Logger& log_;
    ServerStats stats_;
};

}  // namespace

int main(int argc, char** argv) {
    common::SetupConsole();
    common::ArgParser args(argc, argv, {"verbose", "help"});
    if (args.Has("help")) {
        std::printf("server [--port 27015] [--bind 0.0.0.0] [--log server.log] [--verbose] [--idle-exit SEC]\n");
        return 0;
    }
    const auto port = static_cast<std::uint16_t>(args.GetInt("port", 27015, 1, 65535));
    const std::string bindHost = args.GetString("bind", "0.0.0.0");
    const std::string logPath = args.GetString("log", "server.log");
    const auto idleExitUs = static_cast<std::uint64_t>(args.GetInt("idle-exit", 0, 0, 86400)) * 1'000'000;
    args.RejectUnknown({"port", "bind", "log", "verbose", "help", "idle-exit"});
    if (!args.Ok()) {
        for (const auto& error : args.Errors()) std::fprintf(stderr, "Ошибка: %s\n", error.c_str());
        return 2;
    }

    common::Logger log(logPath);
    log.SetConsoleDetails(args.Has("verbose"));
    if (!log.FileOk()) std::fprintf(stderr, "Не удалось открыть журнал %s\n", logPath.c_str());

    transport::NetworkContext network;
    transport::UdpSocket socket;
    if (!network.Ok() || !socket.Open() || !socket.Bind(bindHost, port)) {
        log.Warn("Не удалось открыть UDP-сокет " + bindHost + ":" + std::to_string(port) + ", " +
                 transport::LastErrorText());
        return 1;
    }

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    log.Info("Сервер запущен на " + bindHost + ":" + std::to_string(port) + ", протокол v" +
             std::to_string(protocol::kProtocolVersion) + " (Ctrl+C - остановка)");

    LatencyServer server(socket, log);
    std::uint64_t lastActivityUs = 0;  // 0 - датаграмм ещё не было
    while (!g_stop) {
        if (!socket.WaitReadable(200'000)) {  // периодически проверяем флаг остановки
            if (idleExitUs > 0 && lastActivityUs > 0 && common::NowUs() - lastActivityUs > idleExitUs) {
                log.Info("Нет датаграмм " + std::to_string(idleExitUs / 1'000'000) + " с - завершение");
                break;
            }
            continue;
        }
        while (auto datagram = socket.TryReceive()) {
            const std::uint64_t receivedUs = common::NowUs();
            server.Handle(*datagram, receivedUs);
            lastActivityUs = receivedUs;
        }
    }

    const auto& stats = server.Stats();
    std::size_t droppedTotal = 0;
    for (const auto& item : stats.dropped) droppedTotal += item.second;
    log.Info("Остановка. Датаграмм: " + std::to_string(stats.datagrams) + ", PING: " + std::to_string(stats.pings) +
             ", команд: " + std::to_string(stats.commands) + ", отброшено: " + std::to_string(droppedTotal));
    for (const auto& [reason, count] : stats.dropped) {
        log.Info("  отброшено " + std::to_string(count) + ": " + reason);
    }
    return 0;
}
