cmake_minimum_required(VERSION 3.14)
project(LogosPalaceUiPlugin LANGUAGES CXX)

if(NOT DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    message(FATAL_ERROR "Set LOGOS_MODULE_BUILDER_ROOT to build Logos Palace UI")
endif()
include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)

logos_module(
    NAME ${MODULE_NAME}
    REP_FILE src/logos_palace_ui.rep
    SOURCES
        src/logos_palace_ui_backend.h
        src/logos_palace_ui_backend.cpp
    INCLUDE_DIRS src
)
