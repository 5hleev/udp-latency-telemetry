#include "transport/udp_socket.hpp"

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <mswsock.h>
using socket_t = SOCKET;
using socklen_t = int;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <cerrno>
    #include <cstring>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

#include <algorithm>
#include <utility>

namespace transport {

namespace {

constexpr std::size_t kReceiveBufferSize = 65536;  // больше любой UDP-датаграммы

int LastError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

void CloseNative(socket_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

timeval ToTimeval(std::uint64_t timeoutUs) {
    timeval tv{};
    tv.tv_sec = static_cast<decltype(tv.tv_sec)>(timeoutUs / 1'000'000);
    tv.tv_usec = static_cast<decltype(tv.tv_usec)>(timeoutUs % 1'000'000);
    return tv;
}

sockaddr_in ToSockaddr(const Endpoint& endpoint) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = endpoint.addressBe;
    addr.sin_port = endpoint.portBe;
    return addr;
}

Endpoint FromSockaddr(const sockaddr_in& addr) {
    return Endpoint{addr.sin_addr.s_addr, addr.sin_port};
}

}  // namespace

// ---------------------------------------------------------------- Endpoint

std::optional<Endpoint> Endpoint::Resolve(const std::string& host, std::uint16_t port) {
    in_addr address{};
    if (inet_pton(AF_INET, host.c_str(), &address) != 1) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
            return std::nullopt;
        }
        address = reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr;
        freeaddrinfo(result);
    }
    return Endpoint{address.s_addr, htons(port)};
}

std::string Endpoint::ToString() const {
    in_addr address{};
    address.s_addr = addressBe;
    char text[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &address, text, sizeof(text));
    return std::string(text) + ":" + std::to_string(Port());
}

std::uint16_t Endpoint::Port() const {
    return ntohs(portBe);
}

// ---------------------------------------------------------------- NetworkContext

NetworkContext::NetworkContext() {
#ifdef _WIN32
    WSADATA data;
    ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    ok_ = true;
#endif
}

NetworkContext::~NetworkContext() {
#ifdef _WIN32
    if (ok_) WSACleanup();
#endif
}

// ---------------------------------------------------------------- UdpSocket

struct UdpSocket::Impl {
    socket_t handle = kInvalidSocket;
    std::vector<std::uint8_t> buffer = std::vector<std::uint8_t>(kReceiveBufferSize);
};

UdpSocket::UdpSocket() : impl_(std::make_unique<Impl>()) {}

UdpSocket::~UdpSocket() {
    Close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept = default;
UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        Close();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool UdpSocket::Open() {
    Close();
    impl_->handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->handle == kInvalidSocket) return false;

#ifdef _WIN32
    // Без этого Windows возвращает WSAECONNRESET из recvfrom, если ранее отправленная
    // датаграмма получила ICMP Port Unreachable (например, сервер ещё не запущен).
    BOOL reportReset = FALSE;
    DWORD bytesReturned = 0;
    WSAIoctl(impl_->handle, SIO_UDP_CONNRESET, &reportReset, sizeof(reportReset), nullptr, 0,
             &bytesReturned, nullptr, nullptr);
    u_long nonBlocking = 1;
    ioctlsocket(impl_->handle, FIONBIO, &nonBlocking);
#else
    const int flags = fcntl(impl_->handle, F_GETFL, 0);
    fcntl(impl_->handle, F_SETFL, flags | O_NONBLOCK);
#endif
    return true;
}

bool UdpSocket::IsOpen() const {
    return impl_ && impl_->handle != kInvalidSocket;
}

void UdpSocket::Close() {
    if (IsOpen()) {
        CloseNative(impl_->handle);
        impl_->handle = kInvalidSocket;
    }
}

bool UdpSocket::Bind(const std::string& host, std::uint16_t port) {
    if (!IsOpen()) return false;
    auto endpoint = Endpoint::Resolve(host, port);
    if (!endpoint) return false;
    sockaddr_in addr = ToSockaddr(*endpoint);
    return bind(impl_->handle, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
}

std::optional<Endpoint> UdpSocket::LocalEndpoint() const {
    if (!IsOpen()) return std::nullopt;
    sockaddr_in addr{};
    socklen_t length = sizeof(addr);
    if (getsockname(impl_->handle, reinterpret_cast<sockaddr*>(&addr), &length) != 0) return std::nullopt;
    return FromSockaddr(addr);
}

bool UdpSocket::SendTo(const std::vector<std::uint8_t>& data, const Endpoint& to) {
    if (!IsOpen()) return false;
    sockaddr_in addr = ToSockaddr(to);
    const auto sent = sendto(impl_->handle, reinterpret_cast<const char*>(data.data()),
                             static_cast<int>(data.size()), 0,
                             reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    return sent >= 0 && static_cast<std::size_t>(sent) == data.size();
}

bool UdpSocket::WaitReadable(std::uint64_t timeoutUs) {
    std::vector<UdpSocket*> sockets{this};
    return WaitAnyReadable(sockets, timeoutUs).front();
}

std::optional<Datagram> UdpSocket::TryReceive() {
    if (!IsOpen()) return std::nullopt;
    sockaddr_in from{};
    socklen_t fromLength = sizeof(from);
    const auto received = recvfrom(impl_->handle, reinterpret_cast<char*>(impl_->buffer.data()),
                                   static_cast<int>(impl_->buffer.size()), 0,
                                   reinterpret_cast<sockaddr*>(&from), &fromLength);
    Datagram datagram;
    if (received < 0) {
        const int error = LastError();
#ifdef _WIN32
        if (error == WSAEMSGSIZE) {  // не поместилась: данные обрезаны до размера буфера
            datagram.data.assign(impl_->buffer.begin(), impl_->buffer.end());
            datagram.from = FromSockaddr(from);
            datagram.truncated = true;
            return datagram;
        }
#endif
        (void)error;  // WOULDBLOCK (очередь пуста) или ошибка сокета - читать нечего
        return std::nullopt;
    }
    datagram.data.assign(impl_->buffer.begin(), impl_->buffer.begin() + received);
    datagram.from = FromSockaddr(from);
    return datagram;
}

std::intptr_t UdpSocket::NativeHandle() const {
    return IsOpen() ? static_cast<std::intptr_t>(impl_->handle) : -1;
}

std::vector<bool> WaitAnyReadable(const std::vector<UdpSocket*>& sockets, std::uint64_t timeoutUs) {
    std::vector<bool> ready(sockets.size(), false);
    fd_set readSet;
    FD_ZERO(&readSet);
    socket_t maxHandle = 0;
    for (const auto* socket : sockets) {
        if (socket == nullptr || !socket->IsOpen()) continue;
        const auto handle = static_cast<socket_t>(socket->NativeHandle());
        FD_SET(handle, &readSet);
        maxHandle = std::max(maxHandle, handle);
    }
    timeval tv = ToTimeval(timeoutUs);
    const int result = select(static_cast<int>(maxHandle + 1), &readSet, nullptr, nullptr, &tv);
    if (result <= 0) return ready;
    for (std::size_t i = 0; i < sockets.size(); ++i) {
        if (sockets[i] != nullptr && sockets[i]->IsOpen() &&
            FD_ISSET(static_cast<socket_t>(sockets[i]->NativeHandle()), &readSet)) {
            ready[i] = true;
        }
    }
    return ready;
}

std::string LastErrorText() {
    return "код ошибки " + std::to_string(LastError());
}

}  // namespace transport
