cmake_minimum_required(VERSION 3.14)
project(PalaceCorePlugin LANGUAGES CXX)

if(NOT DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    message(FATAL_ERROR "Set LOGOS_MODULE_BUILDER_ROOT to build Palace Core")
endif()
include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
find_package(OpenSSL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/metadata.json" METADATA_JSON)
string(JSON MODULE_NAME GET ${METADATA_JSON} name)

option(
    PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE
    "Build test-only Delivery and Storage acceptance fixtures"
    OFF
)

set(PALACE_DELIVERY_ACCEPTANCE_SOURCES)
set(PALACE_DELIVERY_ACCEPTANCE_INCLUDE_DIRS)
if(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
    list(
        APPEND PALACE_DELIVERY_ACCEPTANCE_SOURCES
        tests/fixtures/palace_delivery_acceptance_fixture.h
        tests/fixtures/palace_delivery_acceptance_fixture.cpp
    )
    list(
        APPEND PALACE_DELIVERY_ACCEPTANCE_INCLUDE_DIRS
        tests/fixtures
    )
endif()

logos_module(
    NAME ${MODULE_NAME}
    SOURCES
        src/palace_availability.h
        src/palace_availability.cpp
        src/palace_sha256.h
        src/palace_sha256.cpp
        src/palace_initial_authoring.h
        src/palace_initial_authoring.cpp
        src/palace_application_round_trip.h
        src/palace_application_round_trip.cpp
        src/palace_authority.h
        src/palace_authority.cpp
        src/palace_delivery.h
        src/palace_delivery.cpp
        src/palace_delivery_session.h
        src/palace_delivery_session.cpp
        src/palace_delivery_bridge.h
        src/palace_delivery_bridge.cpp
        ${PALACE_DELIVERY_ACCEPTANCE_SOURCES}
        src/palace_callback_lifetime.h
        src/palace_projection.h
        src/palace_projection.cpp
        src/palace_room_transition.h
        src/palace_room_transition.cpp
        src/palace_asset.h
        src/palace_asset.cpp
        src/palace_storage.h
        src/palace_storage.cpp
        src/palace_storage_cid.h
        src/palace_storage_cid.cpp
        src/palace_storage_module_session.h
        src/palace_storage_module_session.cpp
        src/palace_storage_module_codec.h
        src/palace_storage_module_codec.cpp
        src/palace_storage_catalog_session.h
        src/palace_storage_catalog_session.cpp
        src/palace_storage_mvp.h
        src/palace_storage_mvp.cpp
        src/palace_storage_mvp_catalog_store.h
        src/palace_storage_mvp_catalog_store.cpp
        src/palace_verified_asset_store.h
        src/palace_verified_asset_store.cpp
        src/palace_asset_authoring.h
        src/palace_asset_authoring.cpp
        src/palace_asset_authoring_store.h
        src/palace_asset_authoring_store.cpp
        src/palace_identity.h
        src/palace_identity.cpp
        src/palace_delivery_identity_store.h
        src/palace_delivery_identity_store.cpp
        src/palace_lez.h
        src/palace_lez.cpp
        src/palace_lez_authority_projection.h
        src/palace_lez_authority_projection.cpp
        src/palace_lez_coordinator_store.h
        src/palace_lez_coordinator_store.cpp
        src/palace_lez_authority_bundle_store.h
        src/palace_lez_authority_bundle_store.cpp
        src/palace_lez_authority_state.h
        src/palace_lez_authority_state.cpp
        src/palace_lez_intent.h
        src/palace_lez_intent.cpp
        src/palace_lez_release_lock.h
        src/palace_lez_release_lock.cpp
        src/palace_lez_profile.h
        src/palace_lez_profile.cpp
        src/palace_lez_local_history.h
        src/palace_lez_local_history.cpp
        src/palace_lez_submission_intent_store.h
        src/palace_lez_submission_intent_store.cpp
        src/palace_human_moderation.h
        src/palace_human_moderation.cpp
        src/palace_vm_finalized_replay.h
        src/palace_vm_finalized_replay.cpp
        src/palace_lez_explorer_finality.h
        src/palace_lez_explorer_finality.cpp
        src/palace_lez_explorer_protocol.h
        src/palace_lez_explorer_history.h
        src/palace_lez_explorer_history.cpp
        src/palace_lez_explorer_submission_recovery.h
        src/palace_lez_explorer_submission_recovery.cpp
        src/palace_lez_finality_expectation.h
        src/palace_lez_finality_expectation.cpp
        src/palace_lez_explorer_qt_transport.h
        src/palace_lez_explorer_qt_transport.cpp
        src/palace_action_journal.h
        src/palace_action_journal.cpp
        src/palace_core_impl.h
        src/palace_core_impl.cpp
    INCLUDE_DIRS
        src
        ${PALACE_DELIVERY_ACCEPTANCE_INCLUDE_DIRS}
)

target_link_libraries(${MODULE_NAME}_module_plugin PRIVATE OpenSSL::Crypto Qt6::Core Qt6::Gui Qt6::Network)
if(PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE)
    target_compile_definitions(
        ${MODULE_NAME}_module_plugin
        PRIVATE PALACE_ENABLE_DELIVERY_ACCEPTANCE_FIXTURE=1
    )
endif()
