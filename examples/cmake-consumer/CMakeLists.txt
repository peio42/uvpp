cmake_minimum_required(VERSION 3.14)

project(uvpp_external_consumer LANGUAGES CXX)

find_package(uvpp CONFIG REQUIRED)

add_executable(uvpp_external_consumer main.cpp)
target_link_libraries(uvpp_external_consumer PRIVATE uvpp::uvpp)
