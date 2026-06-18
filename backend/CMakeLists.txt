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

option(USE_BOOST_LOCKFREE "Use Boost.Lockfree queues for inter-module communication" ON)

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
    src/main.cpp
)

add_executable(trebuchet_backend ${BACKEND_SOURCES})
target_include_directories(trebuchet_backend PRIVATE include)

if(USE_BOOST_LOCKFREE AND Boost_FOUND)
    target_link_libraries(trebuchet_backend PRIVATE Boost::boost)
endif()

if(WIN32)
    target_link_libraries(trebuchet_backend PRIVATE ws2_32)
endif()

install(TARGETS trebuchet_backend DESTINATION bin)
