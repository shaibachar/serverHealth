cmake_minimum_required(VERSION 3.14)
project(serverhealth VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch cpp-httplib (header-only, MIT licensed) ──────────────────────────
include(FetchContent)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.14.3
)

set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)   # keep it header-only
set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE    OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(httplib)

# ── Executable ─────────────────────────────────────────────────────────────
add_executable(serverhealth
    src/main.cpp
    src/health_collector.cpp
)

target_link_libraries(serverhealth PRIVATE httplib::httplib pthread)
