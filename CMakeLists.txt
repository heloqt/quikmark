cmake_minimum_required(VERSION 3.25)

project(liteMark VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LITEMARK_STATIC_WX "Link wxWidgets statically" OFF)

if(LITEMARK_STATIC_WX)
    set(wxWidgets_USE_STATIC ON)
endif()

find_package(wxWidgets 3.3 CONFIG REQUIRED COMPONENTS core base)

add_executable(litemark WIN32
    src/app/Application.cpp
    src/app/litemark.rc
    src/app/main.cpp
    src/session/TabManager.cpp
    src/ui/DocumentView.cpp
    src/ui/MainFrame.cpp
    src/ui/TabStrip.cpp
    src/ui/ThemeController.cpp
)

target_include_directories(litemark PRIVATE src)
target_link_libraries(litemark PRIVATE wxWidgets::core wxWidgets::base)

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
