cmake_minimum_required(VERSION 3.14)
project(PalaceCoreTests LANGUAGES CXX)

include(LogosTest)
find_package(OpenSSL REQUIRED)

logos_test(
    NAME palace_core_contract_tests
    MODULE_SOURCES
        ../src/palace_availability.cpp
        ../src/palace_sha256.cpp
        ../src/palace_authority.cpp
        ../src/palace_delivery.cpp
        ../src/palace_projection.cpp
        ../src/palace_asset.cpp
        ../src/palace_identity.cpp
        ../src/palace_action_journal.cpp
    TEST_SOURCES
        main.cpp
        test_availability.cpp
        test_authority_delivery.cpp
        test_projection_store.cpp
        test_asset_covenant.cpp
        test_identity.cpp
        test_action_journal.cpp
    EXTRA_INCLUDES ../src
)

target_link_libraries(palace_core_contract_tests PRIVATE OpenSSL::Crypto)
