# Шаблон практической работы №2 — измерение RTT в UDP-протоколе

Этот репозиторий — стартовый шаблон. Он **не является готовым решением**. Все функции с
комментарием `// TODO(student):` необходимо реализовать самостоятельно, иначе программа
не соберётся или не будет проходить тесты.

## Что уже есть

- Каркас проекта на CMake (`CMakeLists.txt`).
- Заголовки протокола `include/protocol/*.hpp` с описанием формата пакетов `PING`/`PONG`,
  `MOVEMENT`, `SHOOT` (из ПР №1) и заглушками функций сериализации.
- Заголовок `include/telemetry/telemetry.hpp` с описанием класса учёта RTT/SRTT/джиттера
  и заглушками методов.
- `src/server_main.cpp` и `src/client_main.cpp` — точки входа с TODO по сетевой части.
- `tests/` — заготовка модульных тестов на Catch2 (доступен через CMake FetchContent).
- `docs/` — шаблоны отчётных файлов `Protocol_Specification.md`, `Experiment_Config.md`,
  `Latency_Report.md`.

## Что нужно сделать

1. Реализовать функции в `src/protocol.cpp`: сериализацию/десериализацию `PING`, `PONG`
   и заголовка пакета с валидацией длины, версии и типа.
2. Реализовать класс `Telemetry` в `src/telemetry.cpp`: учёт `inFlight`, расчёт RTT, SRTT,
   джиттера, обработку тайм-аутов, дублей и запоздалых ответов.
3. Дописать сетевую часть в `src/server_main.cpp` (bind, recvfrom, обработка пакетов,
   логирование) и `src/client_main.cpp` (отправка PING с интервалом, приём PONG, запись
   в CSV).
4. Заполнить тесты в `tests/test_protocol.cpp` и `tests/test_telemetry.cpp` — часть тестов
   уже написана и должна проходить после реализации; часть помечена `TODO(student)`
   и должна быть дописана.
5. Заполнить документы в `docs/` по результатам экспериментов.

## Сборка

```bash
git clone <ссылка на форк вашего репозитория>
cd pr2-latency-template
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Структура

```text
pr2-latency-template/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── protocol/packet.hpp
│   └── telemetry/telemetry.hpp
├── src/
│   ├── protocol.cpp
│   ├── telemetry.cpp
│   ├── client_main.cpp
│   └── server_main.cpp
├── tests/
│   ├── CMakeLists.txt
│   ├── test_protocol.cpp
│   └── test_telemetry.cpp
└── docs/
    ├── Protocol_Specification.md
    ├── Experiment_Config.md
    └── Latency_Report.md
```

## Порядок работы в GitVerse

1. Сделайте fork/клон репозитория в свой GitVerse-namespace.
2. Создайте ветку `feature/latency-measurement`.
3. Коммитьте реализацию небольшими логическими шагами.
4. Откройте Pull Request, попросите ревью у участника команды.
5. После ревью — merge в `main`.

Не удаляйте комментарии `// TODO(student):` до тех пор, пока не реализуете соответствующую
логику — они используются семинаристом при быстрой проверке прогресса.
