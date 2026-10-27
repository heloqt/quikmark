cmake_minimum_required(VERSION 3.25)

project(QuikMark VERSION 1.2.0 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LITEMARK_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${LITEMARK_GENERATED_DIR}/litemark")
configure_file(
    src/app/Version.h.in
    "${LITEMARK_GENERATED_DIR}/litemark/Version.h"
    @ONLY
)

set(LITEMARK_STATUS_ICON_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/status-icons/manifest.txt")
set(LITEMARK_STATUS_ICON_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/status-icons")
file(STRINGS "${LITEMARK_STATUS_ICON_MANIFEST}" LITEMARK_STATUS_ICON_ENTRIES)
set(LITEMARK_STATUS_ICON_FILES "${LITEMARK_STATUS_ICON_MANIFEST}")
foreach(entry IN LISTS LITEMARK_STATUS_ICON_ENTRIES)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 2 asset_name)
    list(APPEND LITEMARK_STATUS_ICON_FILES
        "${LITEMARK_STATUS_ICON_SOURCE_DIR}/${asset_name}.png")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${LITEMARK_STATUS_ICON_FILES}
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateStatusIcons.cmake")
set(LITEMARK_STATUS_ICON_HEADER
    "${LITEMARK_GENERATED_DIR}/litemark/StatusIconsGenerated.h")
set(LITEMARK_STATUS_ICON_SOURCE
    "${LITEMARK_GENERATED_DIR}/litemark/StatusIconsGenerated.cpp")
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DSTATUS_ICON_MANIFEST=${LITEMARK_STATUS_ICON_MANIFEST}"
        "-DSTATUS_ICON_SOURCE_DIR=${LITEMARK_STATUS_ICON_SOURCE_DIR}"
        "-DSTATUS_ICON_HEADER=${LITEMARK_STATUS_ICON_HEADER}"
        "-DSTATUS_ICON_SOURCE=${LITEMARK_STATUS_ICON_SOURCE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateStatusIcons.cmake"
    RESULT_VARIABLE status_icon_result
)
if(NOT status_icon_result EQUAL 0)
    message(FATAL_ERROR "Failed to generate embedded status icons")
endif()

option(LITEMARK_STATIC_WX "Link wxWidgets statically" OFF)

if(WIN32 AND LITEMARK_STATIC_WX)
    set(wxWidgets_USE_STATIC ON)
endif()

set(LITEMARK_MD4C_HINTS)
if(WIN32)
    list(APPEND LITEMARK_MD4C_HINTS "D:/msys64-33/ucrt64")
endif()

find_file(MD4C_STATIC_LIBRARY NAMES libmd4c.a
    HINTS ${LITEMARK_MD4C_HINTS}
    PATH_SUFFIXES lib
    REQUIRED)

find_package(wxWidgets 3.3 CONFIG REQUIRED COMPONENTS core base)
find_package(Threads REQUIRED)

if(NOT TARGET wxWidgets::core AND TARGET wx::core)
    add_library(wxWidgets::core INTERFACE IMPORTED)
    set_property(TARGET wxWidgets::core PROPERTY
        INTERFACE_LINK_LIBRARIES wx::core)
endif()
if(NOT TARGET wxWidgets::base AND TARGET wx::base)
    add_library(wxWidgets::base INTERFACE IMPORTED)
    set_property(TARGET wxWidgets::base PROPERTY
        INTERFACE_LINK_LIBRARIES wx::base)
endif()

find_path(MD4C_INCLUDE_DIR NAMES md4c.h
    HINTS ${LITEMARK_MD4C_HINTS}
    PATH_SUFFIXES include
    REQUIRED)

if(LITEMARK_STATIC_WX)
    find_file(MD4C_LIBRARY NAMES libmd4c.a
        HINTS ${LITEMARK_MD4C_HINTS}
        PATH_SUFFIXES lib
        REQUIRED)
else()
    find_library(MD4C_LIBRARY NAMES md4c
        HINTS ${LITEMARK_MD4C_HINTS}
        PATH_SUFFIXES lib
        REQUIRED)
endif()

set(LITEMARK_SOURCES
    src/app/Application.cpp
    src/app/LanguageController.cpp
    src/app/main.cpp
    src/app/UserData.cpp
    src/core/flow/Flowchart.cpp
    src/core/document/DocumentNavigation.cpp
    src/core/document/DocumentSearch.cpp
    src/core/document/DocumentText.cpp
    src/core/document/TableClipboard.cpp
    src/core/parser/MarkdownPrefix.cpp
    src/core/parser/MarkdownParser.cpp
    src/platform/PerformanceTraceCommon.cpp
    src/session/TabManager.cpp
    src/session/WorkspaceState.cpp
    src/ui/DocumentView.cpp
    src/ui/DocumentMediaRenderer.cpp
    src/ui/StatusIcons.cpp
    src/ui/StatusIconRenderer.cpp
    "${LITEMARK_STATUS_ICON_SOURCE}"
    src/ui/FontCatalog.cpp
    src/ui/HelpPaths.cpp
    src/ui/MainFrame.cpp
    src/ui/OutlineList.cpp
    src/ui/OutlineToggleButton.cpp
    src/ui/TabStrip.cpp
    src/ui/TableLayout.cpp
    src/ui/TextLayout.cpp
    src/ui/ThemeController.cpp
)

if(WIN32)
    set_source_files_properties(src/app/litemark.rc PROPERTIES
        OBJECT_DEPENDS "${LITEMARK_GENERATED_DIR}/litemark/Version.h")
    list(APPEND LITEMARK_SOURCES
        src/app/litemark.rc
        src/platform/windows/FileAssociation.cpp
        src/platform/windows/ImageDownloader.cpp
        src/platform/windows/IpcServer.cpp
        src/platform/windows/PerformanceTrace.cpp
        src/platform/windows/SingletonGuard.cpp
    )
    add_executable(litemark WIN32 ${LITEMARK_SOURCES})
elseif(UNIX AND NOT APPLE)
    list(APPEND LITEMARK_SOURCES
        src/platform/linux/FileAssociation.cpp
        src/platform/linux/ImageDownloader.cpp
        src/platform/linux/IpcServer.cpp
        src/platform/linux/PerformanceTrace.cpp
        src/platform/linux/SingletonGuard.cpp
    )
    add_executable(litemark ${LITEMARK_SOURCES})
    find_package(CURL REQUIRED)
    target_link_libraries(litemark PRIVATE CURL::libcurl)
else()
    message(FATAL_ERROR "QuikMark currently supports Windows and Linux only")
endif()

target_include_directories(litemark PRIVATE
    src
    ${LITEMARK_GENERATED_DIR}
    ${MD4C_INCLUDE_DIR}
)
target_link_libraries(litemark PRIVATE
    wxWidgets::core
    wxWidgets::base
    Threads::Threads
    ${MD4C_LIBRARY}
)

if(WIN32)
    target_link_libraries(litemark PRIVATE winhttp)
endif()

if(MINGW)
    target_compile_options(litemark PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )
    target_link_options(litemark PRIVATE -static-libgcc -static-libstdc++)
endif()

if(WIN32 AND LITEMARK_STATIC_WX)
    target_link_options(litemark PRIVATE -static)
endif()

set(LITEMARK_EXECUTABLE_NAME "quikmark")
set_target_properties(litemark PROPERTIES
    OUTPUT_NAME "${LITEMARK_EXECUTABLE_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_custom_target(litemark_locale ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/locale"
        "${CMAKE_BINARY_DIR}/bin/locale"
    COMMENT "Copying QuikMark language catalogs"
)
add_dependencies(litemark litemark_locale)

message(STATUS "QuikMark ${PROJECT_VERSION}")
message(STATUS "wxWidgets 3.3, static=${LITEMARK_STATIC_WX}")
message(STATUS "md4c: ${MD4C_LIBRARY}")

option(LITEMARK_BUILD_TESTS "Build regression tests" ON)
if(LITEMARK_BUILD_TESTS)
    enable_testing()

    add_executable(litemark_pending_scroll_tests
        tests/pending_scroll_tests.cpp
    )
    target_include_directories(litemark_pending_scroll_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_pending_scroll_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME pending_scroll_state COMMAND litemark_pending_scroll_tests)

    add_executable(litemark_parser_tests
        tests/parser_smoke.cpp
        src/core/flow/Flowchart.cpp
        src/core/parser/MarkdownParser.cpp
    )
    target_include_directories(litemark_parser_tests PRIVATE src ${MD4C_INCLUDE_DIR})
    target_link_libraries(litemark_parser_tests PRIVATE ${MD4C_STATIC_LIBRARY})
    if(MINGW)
        target_link_options(litemark_parser_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME parser_smoke COMMAND litemark_parser_tests)

    add_executable(litemark_markdown_prefix_tests
        tests/markdown_prefix_tests.cpp
        src/core/parser/MarkdownPrefix.cpp
    )
    target_include_directories(litemark_markdown_prefix_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_markdown_prefix_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME markdown_prefix_boundary
        COMMAND litemark_markdown_prefix_tests)

    add_executable(litemark_list_layout_tests
        tests/list_layout_tests.cpp
    )
    target_include_directories(litemark_list_layout_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_list_layout_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME list_layout COMMAND litemark_list_layout_tests)

    add_executable(litemark_tab_manager_tests
        tests/tab_manager_tests.cpp
        src/core/flow/Flowchart.cpp
        src/core/parser/MarkdownPrefix.cpp
        src/core/parser/MarkdownParser.cpp
        src/session/TabManager.cpp
    )
    target_include_directories(litemark_tab_manager_tests PRIVATE
        src
        ${MD4C_INCLUDE_DIR}
    )
    target_link_libraries(litemark_tab_manager_tests PRIVATE
        wxWidgets::base
        ${MD4C_STATIC_LIBRARY}
    )
    if(MINGW)
        target_link_options(litemark_tab_manager_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME tab_manager_state COMMAND litemark_tab_manager_tests)

    add_executable(litemark_resource_policy_tests
        tests/resource_policy_tests.cpp
    )
    target_include_directories(litemark_resource_policy_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_resource_policy_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME resource_policy COMMAND litemark_resource_policy_tests)

    add_executable(litemark_flowchart_tests
        tests/flowchart_tests.cpp
        src/core/flow/Flowchart.cpp
    )
    target_include_directories(litemark_flowchart_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_flowchart_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME flowchart_whitelist COMMAND litemark_flowchart_tests)

    add_executable(litemark_markdown_requirements_tests
        tests/markdown_requirements_tests.cpp
        src/core/document/DocumentText.cpp
        src/core/flow/Flowchart.cpp
        src/core/parser/MarkdownParser.cpp
    )
    target_include_directories(litemark_markdown_requirements_tests PRIVATE
        src
        ${MD4C_INCLUDE_DIR}
    )
    target_link_libraries(litemark_markdown_requirements_tests PRIVATE
        ${MD4C_STATIC_LIBRARY}
    )
    if(MINGW)
        target_link_options(litemark_markdown_requirements_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME markdown_requirements
        COMMAND litemark_markdown_requirements_tests)

    add_executable(litemark_file_loading_tests
        tests/file_loading_tests.cpp
        src/core/flow/Flowchart.cpp
        src/core/parser/MarkdownPrefix.cpp
        src/core/parser/MarkdownParser.cpp
        src/session/TabManager.cpp
    )
    target_include_directories(litemark_file_loading_tests PRIVATE
        src
        ${MD4C_INCLUDE_DIR}
    )
    target_link_libraries(litemark_file_loading_tests PRIVATE
        wxWidgets::base
        ${MD4C_STATIC_LIBRARY}
    )
    if(MINGW)
        target_link_options(litemark_file_loading_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME file_loading_encoding COMMAND litemark_file_loading_tests)

    add_executable(litemark_theme_controller_tests
        tests/theme_controller_tests.cpp
        src/app/UserData.cpp
        src/ui/ThemeController.cpp
    )
    target_include_directories(litemark_theme_controller_tests PRIVATE src)
    target_link_libraries(litemark_theme_controller_tests PRIVATE
        wxWidgets::core
        wxWidgets::base
    )
    if(MINGW)
        target_link_options(litemark_theme_controller_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME theme_controller COMMAND litemark_theme_controller_tests)

    add_executable(litemark_document_text_tests
        tests/document_text_tests.cpp
        src/core/document/DocumentText.cpp
    )
    target_include_directories(litemark_document_text_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_document_text_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME document_text_selection COMMAND litemark_document_text_tests)

    add_executable(litemark_language_controller_tests
        tests/language_controller_tests.cpp
        src/app/LanguageController.cpp
    )
    target_include_directories(
        litemark_language_controller_tests PRIVATE src)
    target_link_libraries(litemark_language_controller_tests PRIVATE
        wxWidgets::base
    )
    if(MINGW)
        target_link_options(litemark_language_controller_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME language_controller
        COMMAND litemark_language_controller_tests)
    set_tests_properties(language_controller PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    add_executable(litemark_table_clipboard_tests
        tests/table_clipboard_tests.cpp
        src/core/document/DocumentText.cpp
        src/core/document/TableClipboard.cpp
    )
    target_include_directories(litemark_table_clipboard_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_table_clipboard_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME table_clipboard_export
        COMMAND litemark_table_clipboard_tests)

    add_executable(litemark_status_icon_tests
        tests/status_icon_tests.cpp
        src/ui/StatusIcons.cpp
        "${LITEMARK_STATUS_ICON_SOURCE}"
    )
    target_include_directories(litemark_status_icon_tests PRIVATE
        src
        ${LITEMARK_GENERATED_DIR}
    )
    target_link_libraries(litemark_status_icon_tests PRIVATE
        wxWidgets::base
    )
    if(MINGW)
        target_link_options(litemark_status_icon_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME status_icon_detection
        COMMAND litemark_status_icon_tests)

    add_executable(litemark_status_icon_render_tests
            tests/status_icon_render_tests.cpp
            src/ui/StatusIconRenderer.cpp
            src/ui/StatusIcons.cpp
            src/ui/FontCatalog.cpp
            src/ui/TextLayout.cpp
            "${LITEMARK_STATUS_ICON_SOURCE}"
    )
    target_include_directories(litemark_status_icon_render_tests PRIVATE
        src
        ${LITEMARK_GENERATED_DIR}
    )
    target_link_libraries(litemark_status_icon_render_tests PRIVATE
        wxWidgets::core
        wxWidgets::base
    )
    if(MINGW)
        target_link_options(litemark_status_icon_render_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    set(LITEMARK_STATUS_ICON_RENDER_TEST_ADDED FALSE)
    if(UNIX AND NOT APPLE)
        find_program(XVFB_RUN_EXECUTABLE xvfb-run)
        if(XVFB_RUN_EXECUTABLE)
            add_test(NAME status_icon_rendering
                COMMAND "${XVFB_RUN_EXECUTABLE}" -a
                    $<TARGET_FILE:litemark_status_icon_render_tests>)
            set(LITEMARK_STATUS_ICON_RENDER_TEST_ADDED TRUE)
        endif()
    else()
        add_test(NAME status_icon_rendering
            COMMAND litemark_status_icon_render_tests)
        set(LITEMARK_STATUS_ICON_RENDER_TEST_ADDED TRUE)
    endif()

    add_executable(litemark_document_navigation_tests
        tests/document_navigation_tests.cpp
        src/core/document/DocumentNavigation.cpp
        src/core/document/DocumentText.cpp
    )
    target_include_directories(litemark_document_navigation_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_document_navigation_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME document_navigation COMMAND litemark_document_navigation_tests)

    add_executable(litemark_document_search_tests
        tests/document_search_tests.cpp
        src/core/document/DocumentSearch.cpp
        src/core/document/DocumentText.cpp
    )
    target_include_directories(litemark_document_search_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_document_search_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME document_search COMMAND litemark_document_search_tests)

    add_executable(litemark_workspace_state_tests
        tests/workspace_state_tests.cpp
        src/session/WorkspaceState.cpp
    )
    target_include_directories(litemark_workspace_state_tests PRIVATE src)
    if(MINGW)
        target_link_options(litemark_workspace_state_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME workspace_state COMMAND litemark_workspace_state_tests)

    add_executable(litemark_help_path_tests
        tests/help_path_tests.cpp
        src/ui/HelpPaths.cpp
    )
    target_include_directories(litemark_help_path_tests PRIVATE src)
    target_link_libraries(litemark_help_path_tests PRIVATE
        wxWidgets::base
    )
    if(MINGW)
        target_link_options(litemark_help_path_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME help_path COMMAND litemark_help_path_tests)

    add_executable(litemark_user_data_tests
        tests/user_data_tests.cpp
        src/app/UserData.cpp
    )
    target_include_directories(litemark_user_data_tests PRIVATE src)
    target_link_libraries(litemark_user_data_tests PRIVATE
        wxWidgets::base
    )
    if(MINGW)
        target_link_options(litemark_user_data_tests PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
        )
    endif()
    add_test(NAME user_data_migration
        COMMAND litemark_user_data_tests)

    if(UNIX AND NOT APPLE)
        add_executable(litemark_image_downloader_tests
            tests/linux_image_downloader_tests.cpp
            src/platform/linux/ImageDownloader.cpp
        )
        target_include_directories(litemark_image_downloader_tests PRIVATE src)
        target_link_libraries(litemark_image_downloader_tests PRIVATE
            wxWidgets::base
            CURL::libcurl
            Threads::Threads
        )
        add_test(NAME image_downloader_linux
            COMMAND litemark_image_downloader_tests)

        find_program(XVFB_RUN_EXECUTABLE xvfb-run)
        if(XVFB_RUN_EXECUTABLE)
            add_executable(litemark_file_association_tests
                tests/linux_file_association_tests.cpp
                src/platform/linux/FileAssociation.cpp
            )
            target_include_directories(
                litemark_file_association_tests PRIVATE src)
            target_link_libraries(litemark_file_association_tests PRIVATE
                wxWidgets::core
                wxWidgets::base
                Threads::Threads
            )
            add_test(NAME file_association_linux
                COMMAND "${XVFB_RUN_EXECUTABLE}" -a
                    $<TARGET_FILE:litemark_file_association_tests>)
        endif()
    endif()

    set_tests_properties(
        pending_scroll_state
        parser_smoke
        markdown_prefix_boundary
        list_layout
        tab_manager_state
        resource_policy
        flowchart_whitelist
        markdown_requirements
        file_loading_encoding
        theme_controller
        document_text_selection
        language_controller
        table_clipboard_export
        status_icon_detection
        document_navigation
        document_search
        workspace_state
        help_path
        user_data_migration
        PROPERTIES LABELS "unit"
    )
    if(LITEMARK_STATUS_ICON_RENDER_TEST_ADDED)
        set_tests_properties(status_icon_rendering
            PROPERTIES LABELS "unit")
    endif()
    if(UNIX AND NOT APPLE)
        set_tests_properties(image_downloader_linux PROPERTIES LABELS "unit")
        if(TARGET litemark_file_association_tests)
            set_tests_properties(file_association_linux PROPERTIES LABELS "unit")
        endif()
    endif()
endif()
