cmake_minimum_required(VERSION 3.16)
project(trattoria C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(SYSTEM ${PROJECT_SOURCE_DIR}/include)

add_executable(trattoria_client src/trattoria_client.c)

target_link_libraries(trattoria_client PRIVATE pthread)

