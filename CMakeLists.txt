cmake_minimum_required(VERSION 3.16)
project(ContourBot VERSION 1.1.0 LANGUAGES CXX)

# Имя автора: показывается в «О программе» и в свойствах exe. Поменять можно здесь.
set(APP_AUTHOR "VKM")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Работает и с Qt 6, и с Qt 5.15
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

set(SOURCES
    main.cpp
    mainwindow.h mainwindow.cpp
    areaselector.h areaselector.cpp
    drawer.h drawer.cpp
    contours.h contours.cpp
    winutil.h winutil.cpp
    colorpanel.h colorpanel.cpp
    resources/resources.qrc
)
if(WIN32)
    # иконка и сведения о файле для exe (версия и автор подставляются из этого файла)
    string(REPLACE "." "," APP_VERSION_COMMAS "${PROJECT_VERSION}")
    configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    list(APPEND SOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

add_executable(ContourBot WIN32 ${SOURCES})

target_link_libraries(ContourBot PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_compile_definitions(ContourBot PRIVATE APP_VERSION="${PROJECT_VERSION}" APP_AUTHOR="${APP_AUTHOR}")

if(WIN32)
    target_link_libraries(ContourBot PRIVATE user32 winmm dwmapi shell32 advapi32)
    target_compile_definitions(ContourBot PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

if(MSVC)
    # исходники в UTF-8 (русские строки)
    target_compile_options(ContourBot PRIVATE /utf-8)
endif()

install(TARGETS ContourBot RUNTIME DESTINATION .)
