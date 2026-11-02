include(FetchContent)
FetchContent_Declare(
    catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(catch2)

add_executable(unit_tests
    test_protocol.cpp
    test_telemetry.cpp
    test_sample_log.cpp
    test_series_stats.cpp
    test_link_emulator.cpp)
target_link_libraries(unit_tests PRIVATE protocol telemetry netem Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(unit_tests)
