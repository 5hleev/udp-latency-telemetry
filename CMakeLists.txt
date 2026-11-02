cmake_minimum_required(VERSION 3.16)
project(udp_latency_telemetry CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /utf-8 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------- модули
# protocol  - формат пакетов и сериализация (без сети и часов)
# telemetry - inFlight, RTT/SRTT/джиттер, журнал CSV, статистика серий (без сети)
# transport - UDP-сокеты (без знания формата пакетов)
# common    - монотонные часы, журнал, разбор аргументов
# netem     - модель ухудшения канала для эмулятора

add_library(protocol STATIC protocol/packet.cpp)
target_include_directories(protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(telemetry STATIC
    telemetry/telemetry.cpp
    telemetry/sample_log.cpp
    telemetry/series_stats.cpp)
target_include_directories(telemetry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(transport STATIC transport/udp_socket.cpp)
target_include_directories(transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    target_link_libraries(transport PUBLIC ws2_32)
endif()

add_library(common STATIC common/clock.cpp common/logger.cpp common/cli.cpp)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    target_link_libraries(common PUBLIC winmm)
endif()

add_library(netem STATIC netem/link_emulator.cpp)
target_include_directories(netem PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------- программы

add_executable(server server/server_main.cpp)
target_link_libraries(server PRIVATE protocol transport common)

add_executable(client client/client_main.cpp)
target_link_libraries(client PRIVATE protocol telemetry transport common)

add_executable(netem_proxy netem/netem_main.cpp)
target_link_libraries(netem_proxy PRIVATE netem transport common)

add_executable(latency_report tools/latency_report.cpp)
target_link_libraries(latency_report PRIVATE telemetry common)

# ---------------------------------------------------------------- тесты

option(BUILD_TESTING "Build unit tests (Catch2 via FetchContent)" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
