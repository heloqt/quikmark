cmake_minimum_required(VERSION 3.25)

project(liteMark VERSION 1.0.0 LANGUAGES CXX RC)

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

option(LITEMARK_STATIC_WX "Link wxWidgets statically" OFF)

if(LITEMARK_STATIC_WX)
    set(wxWidgets_USE_STATIC ON)
endif()

find_file(MD4C_STATIC_LIBRARY NAMES libmd4c.a
    HINTS "D:/msys64-33/ucrt64"
    PATH_SUFFIXES lib
    REQUIRED)

find_package(wxWidgets 3.3 CONFIG REQUIRED COMPONENTS core base)

find_path(MD4C_INCLUDE_DIR NAMES md4c.h
    HINTS "D:/msys64-33/ucrt64"
    PATH_SUFFIXES include
    REQUIRED)

if(LITEMARK_STATIC_WX)
    find_file(MD4C_LIBRARY NAMES libmd4c.a
        HINTS "D:/msys64-33/ucrt64"
        PATH_SUFFIXES lib
        REQUIRED)
else()
    find_library(MD4C_LIBRARY NAMES md4c
        HINTS "D:/msys64-33/ucrt64"
        PATH_SUFFIXES lib
        REQUIRED)
endif()

add_executable(litemark WIN32
    src/app/Application.cpp
    src/app/litemark.rc
    src/app/main.cpp
    src/core/flow/Flowchart.cpp
    src/core/document/DocumentNavigation.cpp
    src/core/document/DocumentSearch.cpp
    src/core/document/DocumentText.cpp
    src/core/parser/MarkdownPrefix.cpp
    src/core/parser/MarkdownParser.cpp
    src/platform/windows/FileAssociation.cpp
    src/platform/windows/ImageDownloader.cpp
    src/platform/windows/IpcServer.cpp
    src/platform/windows/PerformanceTrace.cpp
    src/platform/windows/SingletonGuard.cpp
    src/session/TabManager.cpp
    src/session/WorkspaceState.cpp
    src/ui/DocumentView.cpp
    src/ui/MainFrame.cpp
    src/ui/OutlineList.cpp
    src/ui/OutlineToggleButton.cpp
    src/ui/TabStrip.cpp
    src/ui/ThemeController.cpp
)

target_include_directories(litemark PRIVATE
    src
    ${LITEMARK_GENERATED_DIR}
    ${MD4C_INCLUDE_DIR}
)
target_link_libraries(litemark PRIVATE
    wxWidgets::core
    wxWidgets::base
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

if(LITEMARK_STATIC_WX)
    target_link_options(litemark PRIVATE -static)
endif()

set_target_properties(litemark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "liteMark ${PROJECT_VERSION}")
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
        document_navigation
        document_search
        workspace_state
        PROPERTIES LABELS "unit"
    )
endif()
