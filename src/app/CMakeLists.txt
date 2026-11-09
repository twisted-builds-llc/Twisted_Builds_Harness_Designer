# SPDX-License-Identifier: GPL-3.0-or-later
# Everything except main() lives in tbh_app_lib so tests can link it.
add_library(tbh_app_lib STATIC
    Document.h Document.cpp
    bom/BomView.h bom/BomView.cpp
    Highlighter.h Highlighter.cpp
    LibraryStore.h LibraryStore.cpp
    Preferences.h Preferences.cpp
    SelectionModel.h
    commands/EditOps.h commands/EditOps.cpp
    commands/EntityCommands.h
    commands/HarnessCommands.h commands/HarnessCommands.cpp
    layout/LayoutItems.h layout/LayoutItems.cpp
    layout/LayoutScene.h layout/LayoutScene.cpp
    layout/LayoutView.h layout/LayoutView.cpp
    schematic/SchematicItems.h schematic/SchematicItems.cpp
    schematic/SchematicScene.h schematic/SchematicScene.cpp
    schematic/SchematicView.h schematic/SchematicView.cpp
    ui/CanvasView.h ui/CanvasView.cpp
    ui/ConnectorEdits.h ui/ConnectorEdits.cpp
    ui/Format.h ui/Format.cpp
    ui/LibraryDialog.h ui/LibraryDialog.cpp
    ui/MainWindow.h ui/MainWindow.cpp
    ui/PinPatternDialog.h ui/PinPatternDialog.cpp
    ui/ProblemsPanel.h ui/ProblemsPanel.cpp
    ui/properties/ConnectorPanel.h ui/properties/ConnectorPanel.cpp
    ui/properties/HarnessPanel.h ui/properties/HarnessPanel.cpp
    ui/properties/LayoutPanels.h ui/properties/LayoutPanels.cpp
    ui/properties/PanelWidgets.h ui/properties/PanelWidgets.cpp
    ui/properties/PropertiesPanel.h ui/properties/PropertiesPanel.cpp
    ui/properties/WirePanel.h ui/properties/WirePanel.cpp
    ui/PreferencesDialog.h ui/PreferencesDialog.cpp
    ui/theme/Theme.h ui/theme/Theme.cpp
    ui/UnitsCombo.h ui/UnitsCombo.cpp
    ui/WireColors.h ui/WireColors.cpp
)
target_include_directories(tbh_app_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
qt_add_resources(tbh_app_lib "default_library"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES "${PROJECT_SOURCE_DIR}/resources/default-library/library.json")
target_link_libraries(tbh_app_lib PUBLIC tbh_core Qt6::Widgets)
tbh_set_warnings(tbh_app_lib)

qt_add_executable(twisted-harness-designer main.cpp)
target_link_libraries(twisted-harness-designer PRIVATE tbh_app_lib)
set_target_properties(twisted-harness-designer PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON)
tbh_set_warnings(twisted-harness-designer)
