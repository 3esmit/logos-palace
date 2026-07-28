cmake_minimum_required(VERSION 3.14)
project(PalaceCorePlugin LANGUAGES CXX)

if(NOT DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    message(FATAL_ERROR "Set LOGOS_MODULE_BUILDER_ROOT to build Palace Core")
endif()
include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
find_package(OpenSSL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)

logos_module(
    NAME ${MODULE_NAME}
    SOURCES
        src/palace_availability.h
        src/palace_availability.cpp
        src/palace_sha256.h
        src/palace_sha256.cpp
        src/palace_authority.h
        src/palace_authority.cpp
        src/palace_delivery.h
        src/palace_delivery.cpp
        src/palace_projection.h
        src/palace_projection.cpp
        src/palace_asset.h
        src/palace_asset.cpp
        src/palace_verified_asset_store.h
        src/palace_verified_asset_store.cpp
        src/palace_identity.h
        src/palace_identity.cpp
        src/palace_action_journal.h
        src/palace_action_journal.cpp
        src/palace_core_impl.h
        src/palace_core_impl.cpp
    INCLUDE_DIRS src
)

target_link_libraries(${MODULE_NAME}_module_plugin PRIVATE OpenSSL::Crypto Qt6::Core Qt6::Gui)
