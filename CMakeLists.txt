cmake_minimum_required(VERSION 3.15)

# ============================================================
#   Platform dan toolchain setup
# ============================================================
set(VCPKG_TARGET_TRIPLET "x86-windows-static" CACHE STRING "Target triplet for vcpkg")
set(CMAKE_TOOLCHAIN_FILE "D:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "Vcpkg toolchain file")
set(CMAKE_GENERATOR_PLATFORM Win32 CACHE STRING "Target platform")

project(Valkyrie LANGUAGES CXX)

# ============================================================
#   Build options
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(PRODUCT_NAME "Vakyrie Datafeed")
set(PRODUCT_VERSION_MAJOR 0)
set(PRODUCT_VERSION_MINOR 1)
set(PRODUCT_VERSION_PATCH 0)
set(PRODUCT_VERSION "${PRODUCT_VERSION_MAJOR}.${PRODUCT_VERSION_MINOR}.${PRODUCT_VERSION_PATCH}")

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ============================================================
#   Dependencies via vcpkg
# ============================================================
find_package(ixwebsocket CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(simdjson CONFIG REQUIRED)

# ============================================================
#   Kumpulkan semua source files secara otomatis
# ============================================================

# 1. Ambil semua file .cpp dari root folder dan semua subfolder (core, data, net, bridge)
file(GLOB_RECURSE CPP_FILES 
    "core/*.cpp"
    "data/*.cpp"
    "net/*.cpp"
    "bridge/*.cpp"
    "plugin.cpp"
)

# 2. Ambil semua file .cc dari folder protobuf
file(GLOB_RECURSE PROTO_FILES "net/proto/compiled/*.pb.cc")

# 3. Ambil file resource
file(GLOB RC_FILE "core/resource.rc")

file(GLOB_RECURSE NANOPB
    "core/nanopb/*.c"
    "core/nanopb/*.h"
)

# 4. Gabungkan semua file ke dalam satu variabel
set(SOURCE_FILES ${CPP_FILES} ${PROTO_FILES} ${RC_FILE} ${NANOPB})

# ============================================================
#   Library definition
# ============================================================
# Sekarang add_library jadi lebih rapi, tinggal panggil variabel SOURCE_FILES
add_library(Valkyrie SHARED 
    ${SOURCE_FILES}
)

# ============================================================
#   Link libraries (static versions)
# ============================================================
target_link_libraries(Valkyrie PRIVATE
    ixwebsocket::ixwebsocket
    protobuf::libprotobuf
    simdjson::simdjson
    Winhttp
    ws2_32
    crypt32
    bcrypt
)

# Include directories tetap diperlukan agar #include "..." bisa ditemukan
target_include_directories(Valkyrie PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/core/nanopb
    ${CMAKE_SOURCE_DIR}/data
    ${CMAKE_SOURCE_DIR}/net
    ${CMAKE_SOURCE_DIR}/net/proto/compiled  # <-- PENTING: tambahkan ini untuk file .pb.h
    ${CMAKE_SOURCE_DIR}/bridge
)

# ============================================================
#   Compile flags
# ============================================================
target_compile_definitions(Valkyrie PRIVATE
    _AFXDLL
    _USRDLL
    _CRT_SECURE_NO_WARNINGS
    IXWEBSOCKET_USE_TLS=1
    
)

if (MSVC)
    target_compile_options(Valkyrie PRIVATE /W3 /EHsc)
endif()

# ============================================================
#   Info build
# ============================================================
message(STATUS "------------------------------------")
message(STATUS "Building Valkyrie for 32-bit AmiBroker (Static Link)")
message(STATUS "VCPKG triplet   : ${VCPKG_TARGET_TRIPLET}")
message(STATUS "Platform        : ${CMAKE_GENERATOR_PLATFORM}")
message(STATUS "Runtime         : ${CMAKE_MSVC_RUNTIME_LIBRARY}")
message(STATUS "Output directory: ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}")
message(STATUS "------------------------------------")