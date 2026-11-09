# SPDX-License-Identifier: GPL-3.0-or-later
# tbh_core: model, calculations, BOM and file I/O. Links Qt6::Core ONLY.
add_library(tbh_core STATIC
    model/Harness.h model/Harness.cpp
    model/Fragment.h model/Fragment.cpp
    model/HarnessModel.h model/HarnessModel.cpp
    model/PinLabels.h model/PinLabels.cpp
    model/Queries.h model/Queries.cpp
    calc/WireLength.h calc/WireLength.cpp
    io/FileFormat.h
    io/HarnessJson.h io/HarnessJson.cpp
    io/Migrations.h io/Migrations.cpp
    io/TbhFile.h io/TbhFile.cpp
    io/ZipArchive.h io/ZipArchive.cpp
)
target_include_directories(tbh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tbh_core PUBLIC Qt6::Core PRIVATE tbh_miniz)
target_compile_definitions(tbh_core PUBLIC TBH_APP_VERSION="${PROJECT_VERSION}")
tbh_set_warnings(tbh_core)
