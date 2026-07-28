cmake_minimum_required(VERSION 3.14)
project(PalaceVmTests LANGUAGES CXX)

include(LogosTest)

logos_test(
    NAME palace_vm_contract_tests
    MODULE_SOURCES
        ../src/palace_sha256.cpp
        ../src/palace_vm_engine.cpp
    TEST_SOURCES
        main.cpp
        test_vm_contract.cpp
    EXTRA_INCLUDES ../src
)
