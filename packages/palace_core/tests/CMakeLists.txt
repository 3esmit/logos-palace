cmake_minimum_required(VERSION 3.14)
project(PalaceCoreTests LANGUAGES CXX)

include(LogosTest)
find_package(OpenSSL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Network)
find_package(Threads REQUIRED)

logos_test(
    NAME palace_core_contract_tests
    MODULE_SOURCES
        ../src/palace_availability.cpp
        ../src/palace_sha256.cpp
        ../src/palace_application_round_trip.cpp
        ../src/palace_authority.cpp
        ../src/palace_delivery.cpp
        ../src/palace_delivery_session.cpp
        ../src/palace_delivery_bridge.cpp
        ../src/palace_delivery_acceptance_fixture.cpp
        ../src/palace_callback_lifetime.h
        ../src/palace_projection.cpp
        ../src/palace_room_transition.cpp
        ../src/palace_asset.cpp
        ../src/palace_storage.cpp
        ../src/palace_storage_cid.cpp
        ../src/palace_storage_module_session.cpp
        ../src/palace_storage_module_codec.cpp
        ../src/palace_storage_catalog_session.cpp
        ../src/palace_storage_mvp.cpp
        ../src/palace_verified_asset_store.cpp
        ../src/palace_room_backgrounds.cpp
        ../src/palace_identity.cpp
        ../src/palace_delivery_identity_store.cpp
        ../src/palace_lez.cpp
        ../src/palace_lez_authority_projection.cpp
        ../src/palace_lez_coordinator_store.cpp
        ../src/palace_lez_authority_bundle_store.cpp
        ../src/palace_lez_authority_state.cpp
        ../src/palace_lez_intent.cpp
        ../src/palace_lez_release_lock.cpp
        ../src/palace_lez_submission_intent_store.cpp
        ../src/palace_human_moderation.cpp
        ../src/palace_vm_finalized_replay.cpp
        ../src/palace_lez_explorer_finality.cpp
        ../src/palace_lez_explorer_history.cpp
        ../src/palace_lez_explorer_submission_recovery.cpp
        ../src/palace_lez_finality_expectation.cpp
        ../src/palace_lez_explorer_qt_transport.cpp
        ../src/palace_action_journal.cpp
    TEST_SOURCES
        main.cpp
        test_availability.cpp
        test_application_round_trip.cpp
        test_authority_delivery.cpp
        test_delivery_session.cpp
        test_delivery_bridge.cpp
        test_callback_lifetime.cpp
        test_projection_store.cpp
        test_room_transition.cpp
        test_asset_covenant.cpp
        test_storage_asset_queue.cpp
        test_storage_cid.cpp
        test_storage_module_session.cpp
        test_storage_catalog_session.cpp
        test_storage_mvp.cpp
        test_verified_asset_store.cpp
        test_room_backgrounds.cpp
        test_identity.cpp
        test_delivery_identity_store.cpp
        test_lez_codec.cpp
        test_lez_authority_projection.cpp
        test_lez_coordinator_store.cpp
        test_lez_authority_bundle_store.cpp
        test_lez_authority_state.cpp
        test_lez_intent.cpp
        test_lez_release_lock.cpp
        test_lez_submission_intent_store.cpp
        test_human_moderation.cpp
        test_lez_explorer_finality.cpp
        test_lez_explorer_history.cpp
        test_lez_explorer_submission_recovery.cpp
        test_lez_finality_expectation.cpp
        test_lez_explorer_qt_transport.cpp
        test_action_journal.cpp
        test_core_vm_recovery.cpp
    EXTRA_INCLUDES ../src
)

target_link_libraries(
    palace_core_contract_tests
    PRIVATE
        OpenSSL::Crypto
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Threads::Threads
)

add_executable(
    palace_storage_module_codec_tests
    test_storage_module_codec.cpp
    ../src/palace_storage_cid.cpp
    ../src/palace_storage_module_codec.cpp
)
target_compile_features(palace_storage_module_codec_tests PRIVATE cxx_std_17)
target_include_directories(
    palace_storage_module_codec_tests
    PRIVATE
        ../src
)
add_test(
    NAME palace_storage_module_codec_tests
    COMMAND palace_storage_module_codec_tests
)
