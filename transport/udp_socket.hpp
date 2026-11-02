// transport/udp_socket.hpp - сетевой ввод-вывод: UDP-сокет и адрес узла.
//
// Модуль ничего не знает о формате пакетов и метриках: он отправляет и принимает
// байты. Платформенные различия (Winsock / POSIX) скрыты в udp_socket.cpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace transport {

// IPv4-адрес и порт в сетевом порядке байт (как в sockaddr_in).
struct Endpoint {
    std::uint32_t addressBe = 0;
    std::uint16_t portBe = 0;

    static std::optional<Endpoint> Resolve(const std::string& host, std::uint16_t port);
    std::string ToString() const;
    std::uint16_t Port() const;
    bool operator==(const Endpoint& other) const {
        return addressBe == other.addressBe && portBe == other.portBe;
    }
    bool operator!=(const Endpoint& other) const { return !(*this == other); }
};

struct Datagram {
    std::vector<std::uint8_t> data;
    Endpoint from;
    bool truncated = false;  // датаграмма не поместилась в буфер приёма
};

// RAII-инициализация сетевой подсистемы (WSAStartup/WSACleanup на Windows).
class NetworkContext {
public:
    NetworkContext();
    ~NetworkContext();
    NetworkContext(const NetworkContext&) = delete;
    NetworkContext& operator=(const NetworkContext&) = delete;
    bool Ok() const { return ok_; }

private:
    bool ok_ = false;
};

class UdpSocket {
public:
    UdpSocket();
    ~UdpSocket();
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool Open();
    bool IsOpen() const;
    void Close();

    bool Bind(const std::string& host, std::uint16_t port);
    std::optional<Endpoint> LocalEndpoint() const;

    bool SendTo(const std::vector<std::uint8_t>& data, const Endpoint& to);

    // Ждёт входящую датаграмму не дольше timeoutUs. true - можно читать.
    bool WaitReadable(std::uint64_t timeoutUs);

    // Неблокирующее чтение одной датаграммы; nullopt - очередь пуста или ошибка.
    std::optional<Datagram> TryReceive();

    std::intptr_t NativeHandle() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Ожидание готовности к чтению любого из сокетов. Возвращает флаги по порядку.
std::vector<bool> WaitAnyReadable(const std::vector<UdpSocket*>& sockets, std::uint64_t timeoutUs);

std::string LastErrorText();

}  // namespace transport
