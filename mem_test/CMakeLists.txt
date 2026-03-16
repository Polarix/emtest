# ==================== CMakeLists.txt ====================
cmake_minimum_required(VERSION 3.10)
project(mem_test LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_compile_definitions(__USE_MINGW_ANSI_STDIO)
endif()

# 构建库
if(BUILD_SHARED_LIBS)
    add_library(memtest SHARED src/mem_test.c)
    target_compile_definitions(memtest PRIVATE MEMTEST_COMPILING MEMTEST_EXPORTS)
else()
    add_library(memtest STATIC src/mem_test.c)
    target_compile_definitions(memtest PRIVATE MEMTEST_COMPILING)
endif()
target_include_directories(memtest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc)

# 测试可执行文件
add_executable(memtest_demo demo/mem_test_demo.c)
target_link_libraries(memtest_demo memtest)
target_include_directories(memtest_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/inc)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(memtest_demo PRIVATE MEMTEST_STATIC)
endif()

# 安装（可选）
install(TARGETS memtest memtest_demo
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

message(STATUS "Project ${PROJECT_NAME} configured for C compiler: ${CMAKE_C_COMPILER_ID}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}, Shared libraries: ${BUILD_SHARED_LIBS}")