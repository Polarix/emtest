# ==================== CMakeLists.txt ====================
cmake_minimum_required(VERSION 3.10)
project(disk_test LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_compile_definitions(__USE_MINGW_ANSI_STDIO)
endif()

# 构建库
if(BUILD_SHARED_LIBS)
    add_library(disktest SHARED src/disk_test.c)
    target_compile_definitions(disktest PRIVATE DISKTEST_COMPILING DISKTEST_EXPORTS)
else()
    add_library(disktest STATIC src/disk_test.c)
    target_compile_definitions(disktest PRIVATE DISKTEST_COMPILING)
endif()
target_include_directories(disktest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

# 测试可执行文件
add_executable(disktest_demo demo/test_disk_test.c)
target_link_libraries(disktest_demo disktest)
target_include_directories(disktest_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(disktest_demo PRIVATE DISKTEST_STATIC)
endif()

# 安装（可选）
install(TARGETS disktest disktest_demo
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

message(STATUS "Project ${PROJECT_NAME} configured for C compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}, Shared libraries: ${BUILD_SHARED_LIBS}")