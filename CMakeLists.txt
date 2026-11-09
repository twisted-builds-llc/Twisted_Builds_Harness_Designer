# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.21)

project(TwistedHarnessDesigner
    VERSION 0.1.0
    DESCRIPTION "Twisted Builds Harness Designer"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 6.8 LTS is the minimum supported version (CI pins 6.8.x).
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Test LinguistTools)
# Source strings are English. To add a language, list it in I18N_TRANSLATED_LANGUAGES
# (e.g. "de fr") and run: cmake --build <build dir> --target update_translations
# (see translations/README.md).
qt_standard_project_setup(REQUIRES 6.8 I18N_SOURCE_LANGUAGE en)

# miniz (zip container for .tbh files). Downloaded at configure time only;
# the application itself never touches the network.
include(FetchContent)
FetchContent_Declare(miniz
    URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
    URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(miniz)
add_library(tbh_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(tbh_miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR})
set_target_properties(tbh_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(tbh_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_subdirectory(src/core)
add_subdirectory(src/app)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
