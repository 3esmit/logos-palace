cmake_minimum_required(VERSION 3.14)
project(PalaceVmPlugin LANGUAGES CXX)

if(NOT DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    message(FATAL_ERROR "Set LOGOS_MODULE_BUILDER_ROOT to build Palace VM")
endif()
include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)

logos_module(
    NAME ${MODULE_NAME}
    SOURCES
        src/palace_sha256.h
        src/palace_sha256.cpp
        src/palace_vm_engine.h
        src/palace_vm_engine.cpp
        src/palace_vm_finality.h
        src/palace_vm_finality.cpp
        src/palace_vm_impl.h
        src/palace_vm_impl.cpp
    INCLUDE_DIRS src
)
