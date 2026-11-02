// netem/netem_main.cpp - UDP-прокси, эмулирующий задержку, джиттер и потери.
//
//   client  ->  [listen-порт прокси]  --up-профиль-->   сервер
//   client  <-  [listen-порт прокси]  <--down-профиль-- сервер
//
// Используется вместо Clumsy: параметры и seed задаются в командной строке, поэтому
// эксперимент повторяется побайтно одинаково (см. docs/Experiment_Config.md).
//
//   netem_proxy --listen 27016 --server 127.0.0.1:27015 --up "delay=50" [--down "..."]
//               [--seed 42] [--log netem.log] [--idle-exit 5] [--verbose]

#include "common/cli.hpp"
#include "common/clock.hpp"
#include "common/logger.hpp"
#include "netem/link_emulator.hpp"
#include "transport/udp_socket.hpp"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <optional>
#include <queue>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void OnSignal(int) {
    g_stop = true;
}

enum class Direction { Up, Down };

struct Pending {
    std::uint64_t releaseAtUs;
    std::uint64_t order;  // для стабильного порядка при равном времени
    Direction direction;
    std::vector<std::uint8_t> data;

    bool operator>(const Pending& other) const {
        return releaseAtUs != other.releaseAtUs ? releaseAtUs > other.releaseAtUs : order > other.order;
    }
};

std::string StatsText(const netem::EmulatorStats& s) {
    return "пакетов " + std::to_string(s.packets) + ", потеряно " + std::to_string(s.dropped) + ", дублей " +
           std::to_string(s.duplicated) + ", пиков задержки " + std::to_string(s.spiked) + ", испорчено " +
           std::to_string(s.corrupted) + ", с чужим seq " + std::to_string(s.resequenced);
}

std::optional<transport::Endpoint> ParseHostPort(const std::string& text) {
    const auto colon = text.rfind(':');
    if (colon == std::string::npos) return std::nullopt;
    const auto port = common::ParseInt(text.substr(colon + 1));
    if (!port || *port < 1 || *port > 65535) return std::nullopt;
    return transport::Endpoint::Resolve(text.substr(0, colon), static_cast<std::uint16_t>(*port));
}

}  // namespace

int main(int argc, char** argv) {
    common::SetupConsole();
    common::ArgParser args(argc, argv, {"verbose", "help"});
    if (args.Has("help")) {
        std::printf("netem_proxy --listen 27016 --server 127.0.0.1:27015 --up \"delay=50,jitter=0,loss=0\"\n"
                    "            [--down \"...\"] [--seed 42] [--log FILE] [--idle-exit SEC] [--verbose]\n"
                    "Параметры профиля: delay, jitter, loss, dup, dupgap, spike, spikems, corrupt, reseq\n");
        return 0;
    }
    const auto listenPort = static_cast<std::uint16_t>(args.GetInt("listen", 27016, 1, 65535));
    const std::string serverText = args.GetString("server", "127.0.0.1:27015");
    const std::string upText = args.GetString("up", "");
    const std::string downText = args.GetString("down", "");
    const auto seed = static_cast<std::uint64_t>(args.GetInt("seed", 42, 0, INT64_MAX));
    const std::string logPath = args.GetString("log", "");
    const auto idleExitUs = static_cast<std::uint64_t>(args.GetInt("idle-exit", 0, 0, 86400)) * 1'000'000;
    args.RejectUnknown({"listen", "server", "up", "down", "seed", "log", "idle-exit", "verbose", "help"});

    std::string error;
    const auto upProfile = netem::ParseProfile(upText, &error);
    if (!upProfile) std::fprintf(stderr, "Ошибка в --up: %s\n", error.c_str());
    const auto downProfile = netem::ParseProfile(downText, &error);
    if (!downProfile) std::fprintf(stderr, "Ошибка в --down: %s\n", error.c_str());
    const auto server = ParseHostPort(serverText);
    if (!server) std::fprintf(stderr, "Ошибка: --server должен иметь вид host:port\n");
    for (const auto& e : args.Errors()) std::fprintf(stderr, "Ошибка: %s\n", e.c_str());
    if (!args.Ok() || !upProfile || !downProfile || !server) return 2;

    common::Logger log(logPath);
    log.SetConsoleDetails(args.Has("verbose"));

    transport::NetworkContext network;
    transport::UdpSocket clientSide;  // слушает клиента
    transport::UdpSocket serverSide;  // общается с сервером с эфемерного порта
    if (!network.Ok() || !clientSide.Open() || !clientSide.Bind("0.0.0.0", listenPort) || !serverSide.Open()) {
        log.Warn("Не удалось открыть сокеты прокси: " + transport::LastErrorText());
        return 1;
    }

    // Разные seed для направлений: потери вверх и вниз независимы.
    netem::LinkEmulator up(*upProfile, seed);
    netem::LinkEmulator down(*downProfile, seed ^ 0x9E3779B97F4A7C15ULL);
    log.Info("netem_proxy: :" + std::to_string(listenPort) + " -> " + server->ToString() + ", seed=" +
             std::to_string(seed));
    log.Info("  C->S: " + upProfile->ToString());
    log.Info("  S->C: " + downProfile->ToString());

    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
    common::HighResolutionTimerScope timerResolution;

    std::priority_queue<Pending, std::vector<Pending>, std::greater<Pending>> queue;
    std::optional<transport::Endpoint> client;
    std::uint64_t order = 0;
    std::uint64_t lastActivityUs = 0;

    auto enqueue = [&](netem::LinkEmulator& link, Direction direction, const transport::Datagram& datagram,
                       std::uint64_t now) {
        const char* arrow = direction == Direction::Up ? "C->S" : "S->C";
        auto emissions = link.Process(datagram.data, now);
        if (emissions.empty()) {
            log.Detail(std::string(arrow) + " потерян (" + std::to_string(datagram.data.size()) + " байт)");
        }
        for (auto& emission : emissions) {
            if (emission.kind != netem::EmissionKind::Original || emission.corrupted || emission.spiked) {
                log.Detail(std::string(arrow) + (emission.kind == netem::EmissionKind::Duplicate     ? " дубль"
                                                 : emission.kind == netem::EmissionKind::Resequenced ? " копия с чужим seq"
                                                                                                      : "") +
                           (emission.corrupted ? " испорчен" : "") + (emission.spiked ? " пик задержки" : ""));
            }
            queue.push(Pending{emission.releaseAtUs, order++, direction, std::move(emission.data)});
        }
    };

    while (!g_stop) {
        std::uint64_t now = common::NowUs();
        while (!queue.empty() && queue.top().releaseAtUs <= now) {
            const Pending& item = queue.top();
            if (item.direction == Direction::Up) {
                serverSide.SendTo(item.data, *server);
            } else if (client) {
                clientSide.SendTo(item.data, *client);
            }
            queue.pop();
        }
        if (idleExitUs > 0 && lastActivityUs > 0 && queue.empty() && now - lastActivityUs > idleExitUs) {
            log.Info("Нет трафика " + std::to_string(idleExitUs / 1'000'000) + " с - завершение");
            break;
        }

        std::uint64_t waitUs = 200'000;
        if (!queue.empty()) waitUs = queue.top().releaseAtUs > now ? queue.top().releaseAtUs - now : 0;
        const auto ready = transport::WaitAnyReadable({&clientSide, &serverSide}, waitUs);
        if (ready[0]) {
            while (auto datagram = clientSide.TryReceive()) {
                now = common::NowUs();
                if (!client || *client != datagram->from) {
                    client = datagram->from;
                    log.Info("Клиент: " + client->ToString());
                }
                enqueue(up, Direction::Up, *datagram, now);
                lastActivityUs = now;
            }
        }
        if (ready[1]) {
            while (auto datagram = serverSide.TryReceive()) {
                now = common::NowUs();
                if (datagram->from != *server) continue;  // только ответы нашего сервера
                enqueue(down, Direction::Down, *datagram, now);
                lastActivityUs = now;
            }
        }
    }

    log.Info("Итого C->S: " + StatsText(up.Stats()));
    log.Info("Итого S->C: " + StatsText(down.Stats()));
    return 0;
}
