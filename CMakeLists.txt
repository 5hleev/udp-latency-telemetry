cmake_minimum_required(VERSION 3.16)
project(pr2_latency_template CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_executable(server src/server_main.cpp src/protocol.cpp)
add_executable(client src/client_main.cpp src/protocol.cpp src/telemetry.cpp)

if(WIN32)
    target_link_libraries(server ws2_32)
    target_link_libraries(client ws2_32)
endif()

enable_testing()
add_subdirectory(tests)
