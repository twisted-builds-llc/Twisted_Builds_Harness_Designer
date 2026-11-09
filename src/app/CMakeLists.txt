# SPDX-License-Identifier: GPL-3.0-or-later
# Everything except main() lives in tbh_app_lib so tests can link it.
add_library(tbh_app_lib STATIC
    Document.h Document.cpp
    Preferences.h Preferences.cpp
    commands/SetHarnessInfoCommand.h commands/SetHarnessInfoCommand.cpp
    ui/HarnessPropertiesPanel.h ui/HarnessPropertiesPanel.cpp
    ui/MainWindow.h ui/MainWindow.cpp
    ui/PreferencesDialog.h ui/PreferencesDialog.cpp
    ui/theme/Theme.h ui/theme/Theme.cpp
    ui/UnitsCombo.h ui/UnitsCombo.cpp
)
target_include_directories(tbh_app_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tbh_app_lib PUBLIC tbh_core Qt6::Widgets)
tbh_set_warnings(tbh_app_lib)

qt_add_executable(twisted-harness-designer main.cpp)
target_link_libraries(twisted-harness-designer PRIVATE tbh_app_lib)
set_target_properties(twisted-harness-designer PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON)
tbh_set_warnings(twisted-harness-designer)
