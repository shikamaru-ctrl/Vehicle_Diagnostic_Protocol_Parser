cmake_minimum_required(VERSION 3.14)
project(VDPFrameParser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(
        catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.4.0            # or latest stable tag
)
# Prevent Catch2 from adding its own tests by default
set(CATCH2_INSTALL_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

add_library(vdp_parser
        VDPFrameParser/src/vdp_parser.cpp
)

target_include_directories(vdp_parser
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/VDPFrameParser/include
)

add_executable(vdp_app
        main.cpp
)
target_link_libraries(vdp_app
        PRIVATE vdp_parser
)

#find_package(Catch2 3.0 REQUIRED)   # assuming you used FetchContent or submodule

add_executable(vdp_tests
        VDPFrameParser/test/test_vdp_parser.cpp
)
target_link_libraries(vdp_tests
        PRIVATE
        vdp_parser
        Catch2::Catch2WithMain
)
# let CTest discover your Catch2 tests
include(CTest)
include(Catch)
catch_discover_tests(vdp_tests)