cmake_minimum_required(VERSION 3.25)

project(liteMark VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
    src/core/parser/MarkdownParser.cpp
    src/platform/windows/FileAssociation.cpp
    src/platform/windows/ImageDownloader.cpp
    src/platform/windows/IpcServer.cpp
    src/platform/windows/SingletonGuard.cpp
    src/session/TabManager.cpp
    src/ui/DocumentView.cpp
    src/ui/MainFrame.cpp
    src/ui/TabStrip.cpp
    src/ui/ThemeController.cpp
)

target_include_directories(litemark PRIVATE src ${MD4C_INCLUDE_DIR})
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

option(LITEMARK_BUILD_TESTS "Build parser regression tests" ON)
if(LITEMARK_BUILD_TESTS)
    enable_testing()
    add_executable(litemark_parser_tests
        tests/parser_smoke.cpp
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
endif()
