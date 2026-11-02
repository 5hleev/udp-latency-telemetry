#include <cstdio>
#include <cstdint>
#include "protocol/packet.hpp"

// TODO(student): подключить сокеты платформы:
//   - Windows: <winsock2.h>, <ws2tcpip.h>, WSAStartup/WSACleanup, Winsock2 API.
//   - Linux/macOS: <sys/socket.h>, <netinet/in.h>, <unistd.h>.
// Рекомендуемый порт по умолчанию: 27015 (можно изменить, отразив это в README и в
// docs/Protocol_Specification.md).

int main(int argc, char** argv) {
    std::printf("UDP-сервер: заготовка. Реализуйте bind/recvfrom/sendto.\n");

    // TODO(student):
    // 1. Создать UDP-сокет и выполнить bind на выбранный порт.
    // 2. В цикле принимать датаграммы через recvfrom().
    // 3. По первому байту определять PacketType.
    // 4. Для PING: получить время получения (steady_clock/монотонный источник в
    //    микросекундах), сформировать и отправить PONG с ParsePing()/SerializePong().
    // 5. Для MOVEMENT/SHOOT: обработать как в ПР №1 (лог команды).
    // 6. Логировать в консоль и/или файл каждый принятый пакет: адрес, тип, sequence,
    //    результат валидации (принят/отброшен и причина).
    // 7. Обеспечить корректное завершение по Ctrl+C (не обязательно, но желательно).

    return 0;
}
