cmake_minimum_required(VERSION 3.10)

project(connmon
    VERSION 0.0.1
    DESCRIPTION "connection monitor"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED True)

configure_file(config.hpp.in config.hpp)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../mikelibc dist)

add_executable(connmon_client
    connmon_client.c)

add_executable(connmon_server
    connmon_server.c)

target_include_directories(connmon_client
    PUBLIC ${PROJECT_SOURCE_DIR}/../mikelibc)

target_link_libraries(connmon_client
    PUBLIC mike)

target_include_directories(connmon_server
    PUBLIC ${PROJECT_SOURCE_DIR}/../mikelibc)

target_link_libraries(connmon_server
    PUBLIC mike)

set(CMAKE_C_COMPILER gcc)
