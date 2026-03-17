cmake_minimum_required(VERSION 3.14)
project(Platform)

set(CMAKE_CXX_STANDARD 20)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

FetchContent_MakeAvailable(nlohmann_json)

# ── Platform (Supervisor) ────────────────────────────────────────────────────
add_executable(Platform
    src/supervisor/main.cpp
    src/supervisor/ManifestLoader.cpp
    src/supervisor/ProcessSupervisor.cpp
    src/supervisor/ProcessSupervisorKill.cpp
    src/supervisor/ProcessSupervisorFeature.cpp
    src/supervisor/Watchdog.cpp
    src/sdk/ShmManager.cpp
)
target_include_directories(Platform PRIVATE
    src/supervisor
    src/sdk
)
target_link_libraries(Platform PRIVATE nlohmann_json::nlohmann_json rt)

add_custom_command(TARGET Platform POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/manifest.json
        $<TARGET_FILE_DIR:Platform>/manifest.json
)

# ── SDK 라이브러리 (앱들이 링크함) ─────────────────────────────────────────
add_library(sdv_sdk STATIC
    src/sdk/SdvAppFrame.cpp
    src/sdk/ShmManager.cpp
    src/sdk/Feature.cpp
)
target_include_directories(sdv_sdk PUBLIC src/sdk)
target_link_libraries(sdv_sdk PUBLIC rt)

# ── 앱 바이너리 ─────────────────────────────────────────────────────────────
set(APP_OUTPUT_DIR ${CMAKE_SOURCE_DIR}/application/bin)

add_executable(adas
    application/adas/main.cpp
    application/adas/collision_avoidance.cpp
    application/adas/blind_spot_detection.cpp
)
target_link_libraries(adas PRIVATE sdv_sdk)
set_target_properties(adas PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${APP_OUTPUT_DIR})

add_executable(navigation
    application/navigation/main.cpp
    application/navigation/route_guidance.cpp
    application/navigation/map_rendering.cpp
)
target_link_libraries(navigation PRIVATE sdv_sdk)
set_target_properties(navigation PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${APP_OUTPUT_DIR})
