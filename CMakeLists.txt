cmake_minimum_required(VERSION 3.16)
project(Kaleidoscope)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 查找 LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# 设置 LLVM 包含目录
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# LLVM 组件列表
# core: 核心库
# support: 支持库
# native: 本地代码生成
# irreader: IR 读取器（可选）
# x86asmparser, x86codegen: x86 支持（可选，用于生成目标代码）
llvm_map_components_to_libnames(llvm_libs
    core
    support
    native
    irreader
)

# 头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 源文件列表
set(SOURCES
    src/main.cpp
    src/lexer.cpp
    src/parser.cpp
    src/codegen.cpp
)

# 创建可执行文件
add_executable(kaleidoscope ${SOURCES})

# 链接 LLVM 库
target_link_libraries(kaleidoscope ${llvm_libs})

# 在 Linux 上可能需要链接其他库
if(NOT WIN32)
    find_library(DL_LIBRARY dl)
    if(DL_LIBRARY)
        target_link_libraries(kaleidoscope ${DL_LIBRARY})
    endif()
    find_library(PTHREAD_LIBRARY pthread)
    if(PTHREAD_LIBRARY)
        target_link_libraries(kaleidoscope ${PTHREAD_LIBRARY})
    endif()
endif()

# 安装规则
install(TARGETS kaleidoscope DESTINATION bin)

# 打印配置信息
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "LLVM libraries: ${llvm_libs}")
