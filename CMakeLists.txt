cmake_minimum_required(VERSION 3.14)
project(Platform)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_MakeAvailable(nlohmann_json)

add_executable(Platform
    src/main.cpp
    src/ManifestLoader.cpp
    src/ProcessSupervisor.cpp
    src/ProcessSupervisorKill.cpp
    src/ShmManager.cpp
    src/Watchdog.cpp
)
target_link_libraries(Platform PRIVATE nlohmann_json::nlohmann_json rt)

add_custom_command(TARGET Platform POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/manifest.json
        $<TARGET_FILE_DIR:Platform>/manifest.json
)

# 테스트용 앱 - application/bin/ 에 빌드됨
set(APP_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/application/bin)

add_executable(adas application/adas.cpp src/ShmManager.cpp)
target_include_directories(adas PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(adas PRIVATE rt)
set_target_properties(adas PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${APP_OUTPUT_DIR})

add_executable(navigation application/navigation.cpp src/ShmManager.cpp)
target_include_directories(navigation PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(navigation PRIVATE rt)
set_target_properties(navigation PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${APP_OUTPUT_DIR})
