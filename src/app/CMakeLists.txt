# SPDX-License-Identifier: GPL-3.0-or-later
# Everything except main() lives in tbh_app_lib so tests can link it.
add_library(tbh_app_lib STATIC
    Autosave.h Autosave.cpp
    Document.h Document.cpp
    bom/BomView.h bom/BomView.cpp
    Highlighter.h Highlighter.cpp
    LibraryStore.h LibraryStore.cpp
    Preferences.h Preferences.cpp
    Translations.h Translations.cpp
    SelectionModel.h
    commands/EditOps.h commands/EditOps.cpp
    commands/EntityCommands.h
    commands/HarnessCommands.h commands/HarnessCommands.cpp
    layout/LayoutItems.h layout/LayoutItems.cpp
    layout/LayoutScene.h layout/LayoutScene.cpp
    layout/LayoutView.h layout/LayoutView.cpp
    print/PdfExport.h print/PdfExport.cpp
    schematic/SchematicItems.h schematic/SchematicItems.cpp
    schematic/SchematicScene.h schematic/SchematicScene.cpp
    schematic/SchematicView.h schematic/SchematicView.cpp
    schematic/SheetTabBar.h schematic/SheetTabBar.cpp
    ui/CanvasView.h ui/CanvasView.cpp
    ui/NoteItem.h ui/NoteItem.cpp
    ui/ConnectorEdits.h ui/ConnectorEdits.cpp
    ui/Format.h ui/Format.cpp
    ui/HelpDialog.h ui/HelpDialog.cpp
    ui/LibraryDialog.h ui/LibraryDialog.cpp
    ui/MainWindow.h ui/MainWindow.cpp
    ui/PinPatternDialog.h ui/PinPatternDialog.cpp
    ui/ProblemsPanel.h ui/ProblemsPanel.cpp
    ui/RecoveryDialog.h ui/RecoveryDialog.cpp
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
    FILES "${PROJECT_SOURCE_DIR}/resources/default-library/library.json"
          "${PROJECT_SOURCE_DIR}/resources/icons/twisted-harness-designer.png")
target_link_libraries(tbh_app_lib PUBLIC tbh_core Qt6::Widgets)
tbh_set_warnings(tbh_app_lib)

set(TBH_PACKAGING_DIR "${PROJECT_SOURCE_DIR}/packaging")
set(TBH_ICON_DIR "${PROJECT_SOURCE_DIR}/resources/icons")

qt_add_executable(twisted-harness-designer main.cpp)
target_link_libraries(twisted-harness-designer PRIVATE tbh_app_lib)
set_target_properties(twisted-harness-designer PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON)
tbh_set_warnings(twisted-harness-designer)

if(WIN32)
    target_sources(twisted-harness-designer PRIVATE
        "${TBH_PACKAGING_DIR}/windows/twisted-harness-designer.rc")
elseif(APPLE)
    set(TBH_ICNS "${TBH_ICON_DIR}/twisted-harness-designer.icns")
    set_source_files_properties("${TBH_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(twisted-harness-designer PRIVATE "${TBH_ICNS}")
    # No OUTPUT_NAME with spaces: Qt 6.8's macdeployqt deploy step cannot handle them.
    # The release workflow renames the bundle to "Twisted Harness Designer.app" in the dmg.
    set_target_properties(twisted-harness-designer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${TBH_PACKAGING_DIR}/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Harness Designer"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.twistedbuilds.harnessdesigner"
        MACOSX_BUNDLE_ICON_FILE "twisted-harness-designer.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Twisted Builds LLC. GPL-3.0-or-later.")
endif()

# Install layout used by the packages (see packaging/README.md).
install(TARGETS twisted-harness-designer
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(WIN32 OR APPLE)
    # Copies the Qt libraries and plugins next to the app (windeployqt/macdeployqt).
    # On Windows the MSVC runtime DLLs are installed below, so windeployqt does not
    # need to add the 25 MB vc_redist installer.
    set(tbh_deploy_options "")
    if(WIN32)
        set(tbh_deploy_options DEPLOY_TOOL_OPTIONS --no-compiler-runtime)
    endif()
    qt_generate_deploy_app_script(
        TARGET twisted-harness-designer
        OUTPUT_SCRIPT tbh_deploy_script
        ${tbh_deploy_options})
    install(SCRIPT "${tbh_deploy_script}")
endif()

if(WIN32)
    # MSVC runtime DLLs next to the .exe, so the app runs without a separate
    # Visual C++ redistributable install (also needed by the portable zip).
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
elseif(UNIX AND NOT APPLE)
    install(FILES "${TBH_PACKAGING_DIR}/linux/twisted-harness-designer.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${TBH_PACKAGING_DIR}/linux/twisted-harness-designer.xml"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(FILES "${TBH_ICON_DIR}/twisted-harness-designer.png"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()

# Translations (.qm embedded under :/i18n). The English file only holds plural forms
# ("1 wire" / "2 wires"); other languages are added in the top-level CMakeLists.txt.
qt_add_translations(TARGETS tbh_app_lib
    SOURCE_TARGETS tbh_core tbh_app_lib twisted-harness-designer
    TS_FILE_BASE twisted-harness-designer
    TS_FILE_DIR "${PROJECT_SOURCE_DIR}/translations"
    PLURALS_TS_FILE "${PROJECT_SOURCE_DIR}/translations/twisted-harness-designer_en.ts")
