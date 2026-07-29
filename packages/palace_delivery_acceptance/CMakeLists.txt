cmake_minimum_required(VERSION 3.14)
project(PalaceDeliveryAcceptancePlugin LANGUAGES CXX)

if(NOT DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    message(FATAL_ERROR "Set LOGOS_MODULE_BUILDER_ROOT to build Palace Delivery Acceptance")
endif()
include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)

find_package(OpenSSL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)

logos_module(
    NAME ${MODULE_NAME}
    REP_FILE src/palace_delivery_acceptance.rep
    SOURCES
        src/palace_delivery_acceptance_backend.h
        src/palace_delivery_acceptance_backend.cpp
    INCLUDE_DIRS src
)

target_link_libraries(
    ${MODULE_NAME}_module_plugin
    PRIVATE
        OpenSSL::Crypto
        Qt6::Core
)
