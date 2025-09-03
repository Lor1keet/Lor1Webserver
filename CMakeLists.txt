cmake_minimum_required(VERSION 3.10)
project(EpollWebServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/spdlog) 

find_path(MYSQL_INCLUDE_DIR mysql.h 
    PATHS /usr/include/mysql /usr/local/include/mysql)
find_library(MYSQL_LIB mysqlclient  
    PATHS /usr/lib/x86_64-linux-gnu /usr/local/lib)

if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIB)
    message(FATAL_ERROR "MySQL development files not found! Install libmysqlclient-dev.")
endif()

include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/http
    ${PROJECT_SOURCE_DIR}/server
    ${PROJECT_SOURCE_DIR}/mysql
    ${PROJECT_SOURCE_DIR}/threadpool
    ${PROJECT_SOURCE_DIR}/third_party/spdlog/include
    ${MYSQL_INCLUDE_DIR} 
)

set(SRC_FILES
    main.cpp
    http/http_conn.cpp
    server/webserver.cpp
    mysql/mysqlpool.cpp
)

add_executable(${PROJECT_NAME} ${SRC_FILES})

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    spdlog::spdlog  
    ${MYSQL_LIB}    
    pthread         
)
