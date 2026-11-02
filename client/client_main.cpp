#include <cstdio>
#include <cstdint>
#include <fstream>
#include <string>
#include "protocol/packet.hpp"
#include "telemetry/telemetry.hpp"

// TODO(student): подключить сокеты платформы (см. комментарий в server_main.cpp).

// Рекомендуемая структура: класс/функции для отправки PING с заданным интервалом,
// приёма PONG в отдельном потоке или через неблокирующий сокет с таймаутом,
// запись каждого измерения в CSV.

int main(int argc, char** argv) {
    std::printf("UDP-клиент: заготовка. Реализуйте отправку PING и приём PONG.\n");

    // Пример ожидаемых аргументов командной строки (реализуйте разбор самостоятельно):
    //   client <server_ip> <server_port> <experiment_id> <count> <interval_ms>
    // Пример: client 127.0.0.1 27015 baseline 50 500

    Telemetry telemetry;
    std::ofstream csv("docs/latency_samples.csv", std::ios::app);
    if (!csv) {
        std::fprintf(stderr, "Не удалось открыть файл для записи результатов.\n");
    }

    // TODO(student):
    // 1. Разобрать аргументы командной строки: адрес сервера, порт, experiment_id,
    //    число измерений, интервал отправки.
    // 2. Создать UDP-сокет (без bind — клиент использует случайный локальный порт).
    // 3. В цикле: сформировать PING через SerializePing(), сохранить время отправки
    //    в Telemetry::OnPingSent(), отправить через sendto().
    // 4. Принять ответ через recvfrom() с таймаутом (SO_RCVTIMEO/select/poll).
    // 5. При получении PONG — вызвать ParsePong(), затем Telemetry::OnPong() и
    //    записать результат (experiment_id, sample, sequence, sent_at_ms, rtt_ms,
    //    srtt_ms, status) в CSV.
    // 6. При истечении таймаута — вызвать Telemetry::ExpireOverdue() и записать
    //    строку со статусом timeout.
    // 7. Выдержать заданный интервал между отправками (std::this_thread::sleep_for).

    return 0;
}
