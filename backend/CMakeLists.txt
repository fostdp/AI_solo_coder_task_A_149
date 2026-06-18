cmake_minimum_required(VERSION 3.15)
project(trebuchet_sim_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        link_libraries(pthread)
    endif()
endif()

set(BACKEND_SOURCES
    src/trebuchet_physics.cpp
    src/clickhouse_client.cpp
    src/mqtt_alert_manager.cpp
    src/udp_sensor_receiver.cpp
    src/http_api_server.cpp
    src/main.cpp
)

add_executable(trebuchet_backend ${BACKEND_SOURCES})
target_include_directories(trebuchet_backend PRIVATE include)

if(WIN32)
    target_link_libraries(trebuchet_backend PRIVATE ws2_32)
endif()

install(TARGETS trebuchet_backend DESTINATION bin)
