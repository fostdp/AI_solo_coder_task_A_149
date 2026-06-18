cmake_minimum_required(VERSION 3.18)
project(trebuchet_sim_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /GL)
    endif()
else()
    add_compile_options(-Wall -Wextra)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        link_libraries(pthread)
    endif()
endif()

option(USE_BOOST_LOCKFREE "Use Boost.Lockfree queues for inter-module communication" ON)
option(USE_SPDLOG "Use spdlog for structured logging" ON)
option(USE_PROMETHEUS "Use Prometheus for metrics exposition" ON)
option(BUILD_FRONTEND "Build frontend with gzip compression" ON)

include(FetchContent)

if(USE_BOOST_LOCKFREE)
    find_package(Boost QUIET)
    if(Boost_FOUND)
        add_compile_definitions(USE_BOOST_LOCKFREE=1)
        if(NOT TARGET Boost::boost)
            add_library(Boost::boost INTERFACE IMPORTED)
            set_target_properties(Boost::boost PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}")
        endif()
        message(STATUS "Boost.Lockfree enabled: inter-module queues are lock-free")
    else()
        set(USE_BOOST_LOCKFREE OFF)
        message(WARNING "Boost not found, falling back to mutex-based queues (pass-through stubs)")
    endif()
else()
    message(STATUS "Boost.Lockfree disabled, using stub message bus")
endif()

if(USE_SPDLOG)
    find_package(spdlog QUIET)
    if(NOT spdlog_FOUND)
        message(STATUS "Fetching spdlog v1.12.0...")
        FetchContent_Declare(
            spdlog
            GIT_REPOSITORY https://github.com/gabime/spdlog.git
            GIT_TAG v1.12.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(spdlog)
    endif()
    add_compile_definitions(USE_SPDLOG=1)
    message(STATUS "spdlog enabled: structured logging active")
else()
    message(STATUS "spdlog disabled: using std::cout fallback")
endif()

if(USE_PROMETHEUS)
    find_package(prometheus-cpp QUIET)
    if(NOT prometheus-cpp_FOUND)
        message(STATUS "Fetching prometheus-cpp v1.2.4...")
        set(PROMETHEUS_CPP_ENABLE_PULL ON CACHE BOOL "" FORCE)
        set(PROMETHEUS_CPP_ENABLE_PUSH OFF CACHE BOOL "" FORCE)
        set(PROMETHEUS_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(PROMETHEUS_CPP_BUILD_COMPRESSION OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            prometheus-cpp
            GIT_REPOSITORY https://github.com/jupp0r/prometheus-cpp.git
            GIT_TAG v1.2.4
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(prometheus-cpp)
    endif()
    add_compile_definitions(USE_PROMETHEUS=1)
    message(STATUS "Prometheus enabled: metrics on /metrics endpoint active")
else()
    message(STATUS "Prometheus disabled: /metrics stubbed")
endif()

set(BACKEND_SOURCES
    src/trebuchet_physics.cpp
    src/clickhouse_client.cpp
    src/mqtt_alert_manager.cpp
    src/udp_sensor_receiver.cpp
    src/http_api_server.cpp
    src/message_bus.cpp
    src/udp_receiver_module.cpp
    src/spring_simulator_module.cpp
    src/range_predictor_module.cpp
    src/alarm_mqtt_module.cpp
    src/metrics_collector.cpp
    src/logger.cpp
    src/main.cpp
)

set(BACKEND_HEADERS
    include/trebuchet_physics.h
    include/clickhouse_client.h
    include/mqtt_alert_manager.h
    include/udp_sensor_receiver.h
    include/http_api_server.h
    include/message_bus.h
    include/udp_receiver_module.h
    include/spring_simulator_module.h
    include/range_predictor_module.h
    include/alarm_mqtt_module.h
    include/metrics_collector.h
    include/logger.h
)

add_executable(trebuchet_backend ${BACKEND_SOURCES} ${BACKEND_HEADERS})
target_include_directories(trebuchet_backend PRIVATE include)

if(USE_BOOST_LOCKFREE AND Boost_FOUND)
    target_link_libraries(trebuchet_backend PRIVATE Boost::boost)
endif()

if(USE_SPDLOG)
    target_link_libraries(trebuchet_backend PRIVATE spdlog::spdlog)
endif()

if(USE_PROMETHEUS)
    target_link_libraries(trebuchet_backend PRIVATE prometheus-cpp::pull)
endif()

if(WIN32)
    target_link_libraries(trebuchet_backend PRIVATE ws2_32)
endif()

if(UNIX)
    target_link_libraries(trebuchet_backend PRIVATE dl)
endif()

if(BUILD_FRONTEND)
    find_program(GZIP gzip)
    if(GZIP)
        add_custom_target(frontend_gzip ALL
            COMMAND ${CMAKE_COMMAND} -E echo "Compressing frontend static assets with gzip..."
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/gzip_frontend.cmake
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
            COMMENT "Gzipping frontend JS/CSS/HTML assets"
        )
        add_dependencies(trebuchet_backend frontend_gzip)
    else()
        message(WARNING "gzip not found, skipping frontend static compression")
    endif()
endif()

install(TARGETS trebuchet_backend DESTINATION bin)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../frontend DESTINATION share/trebuchet
        FILES_MATCHING
        PATTERN "*.html"
        PATTERN "*.js"
        PATTERN "*.css"
        PATTERN "*.json"
        PATTERN "*.gz"
        PATTERN "*.png"
        PATTERN "*.jpg"
        PATTERN "node_modules" EXCLUDE)
