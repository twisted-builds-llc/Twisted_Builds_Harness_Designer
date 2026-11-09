# SPDX-License-Identifier: GPL-3.0-or-later
function(tbh_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE ${ARG_LIBS} Qt6::Test)
    tbh_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

tbh_add_test(tst_tbhfile SOURCES core/tst_tbhfile.cpp LIBS tbh_core)
tbh_add_test(tst_harnessmodel SOURCES core/tst_harnessmodel.cpp LIBS tbh_core)
tbh_add_test(tst_commands SOURCES app/tst_commands.cpp LIBS tbh_app_lib)
