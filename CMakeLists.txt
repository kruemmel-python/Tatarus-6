cmake_minimum_required(VERSION 3.22)
project(Tatarus4EmbodiedIntelligenceSDK VERSION 4.0.0 LANGUAGES C CXX)

option(TATARUS_BUILD_TESTS "Build SDK tests" ON)
option(TATARUS_BUILD_EXAMPLES "Build SDK examples" ON)
option(TATARUS_BUILD_SHARED "Build shared C ABI library" ON)
option(TATARUS_BUILD_CORTEX "Build optional local-LLM Cortex layer" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Project policy: a compiler warning is a build failure. This is deliberate;
# TATARUS releases must be warning-clean on every supported compiler.
set(CMAKE_COMPILE_WARNING_AS_ERROR ON)

add_library(tatarus_neurobiology STATIC
    modules/tatarus_neurobiology/tatarus_neural_network.cpp
    modules/tatarus_neurobiology/tatarus_tissue.cpp
    modules/tatarus_neurobiology/tatarus_physiology.cpp
    modules/tatarus_neurobiology/tatarus_prospection.cpp
    modules/tatarus_neurobiology/tatarus_cognition.cpp
    modules/tatarus_neurobiology/tatarus_identity.cpp
    modules/tatarus_neurobiology/tatarus_visual_pathway.cpp
    modules/tatarus_neurobiology/tatarus_ocular_system.cpp
    modules/tatarus_neurobiology/tatarus_vestibular_system.cpp
)
target_include_directories(tatarus_neurobiology PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_neurobiology>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
set_target_properties(tatarus_neurobiology PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Neurobiology)

add_library(tatarus_organism STATIC
    modules/tatarus_organism/tatarus_conservation.cpp
    modules/tatarus_organism/tatarus_circulation.cpp
    modules/tatarus_organism/tatarus_heart.cpp
    modules/tatarus_organism/tatarus_lung.cpp
    modules/tatarus_organism/tatarus_kidney.cpp
    modules/tatarus_organism/tatarus_organism.cpp
)
target_include_directories(tatarus_organism PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_organism>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_neurobiology>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(tatarus_organism PUBLIC tatarus_neurobiology)
set_target_properties(tatarus_organism PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME Organism)

add_library(tatarus_sdk STATIC
    src/tatarus_cognitive_cue.cpp
    src/tatarus_mind.cpp
    src/tatarus_cartography.cpp
    src/tatarus_imaginatio.cpp
)
target_include_directories(tatarus_sdk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_neurobiology>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_organism>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(tatarus_sdk PUBLIC tatarus_neurobiology tatarus_organism)
set_target_properties(tatarus_sdk PROPERTIES POSITION_INDEPENDENT_CODE ON OUTPUT_NAME tatarus4_sdk EXPORT_NAME SDK)

if(TATARUS_BUILD_CORTEX)
    find_package(Threads REQUIRED)
    add_library(tatarus_cortex STATIC
        modules/tatarus_cortex/cortex_json.cpp
        modules/tatarus_cortex/cortex_context.cpp
        modules/tatarus_cortex/cortex_trigger.cpp
        modules/tatarus_cortex/cortex_arbiter.cpp
        modules/tatarus_cortex/rover_cortex_adapter.cpp
        modules/tatarus_cortex/imaginatio_cortex_adapter.cpp
        modules/tatarus_cortex/counterfactual_sandbox.cpp
        modules/tatarus_cortex/cortex_learning.cpp
        modules/tatarus_cortex/cortex_executive.cpp
        modules/tatarus_cortex/cortex_metacognition.cpp
        modules/tatarus_cortex/cortex_record_replay.cpp
        modules/tatarus_cortex/topdown_cortex_adapter.cpp
        modules/tatarus_cortex/cortex_worker.cpp
        modules/tatarus_cortex/cortex_orchestrator.cpp
        modules/tatarus_cortex/lm_transport_native.cpp
        modules/tatarus_cortex/lm_studio_client.cpp
    )
    target_include_directories(tatarus_cortex PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_include_directories(tatarus_cortex PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_cortex
        ${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_organism
        ${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_neurobiology
    )
    target_link_libraries(tatarus_cortex PUBLIC tatarus_sdk Threads::Threads)
    if(WIN32)
        target_link_libraries(tatarus_cortex PRIVATE winhttp)
    endif()
    set_target_properties(tatarus_cortex PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME tatarus4_cortex
        EXPORT_NAME Cortex
    )
endif()

if(TATARUS_BUILD_SHARED)
    add_library(tatarus_c SHARED src/tatarus_c_api.cpp)
    target_include_directories(tatarus_c PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_neurobiology>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/modules/tatarus_organism>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(tatarus_c PRIVATE tatarus_sdk tatarus_neurobiology tatarus_organism)
    if(TATARUS_BUILD_CORTEX)
        target_link_libraries(tatarus_c PRIVATE tatarus_cortex)
        target_compile_definitions(tatarus_c PRIVATE TATARUS_HAS_CORTEX=1)
    endif()
    set_target_properties(tatarus_c PROPERTIES OUTPUT_NAME tatarus4_c EXPORT_NAME C)
endif()

if(MSVC)
    if(TATARUS_BUILD_SHARED)
        target_compile_options(tatarus_c PRIVATE /W4 /WX /permissive-)
    endif()
    target_compile_options(tatarus_neurobiology PRIVATE /W4 /WX /permissive-)
    target_compile_options(tatarus_organism PRIVATE /W4 /WX /permissive-)
    target_compile_options(tatarus_sdk PRIVATE /W4 /WX /permissive-)
    if(TATARUS_BUILD_CORTEX)
        target_compile_options(tatarus_cortex PRIVATE /W4 /WX /permissive-)
    endif()
else()
    if(TATARUS_BUILD_SHARED)
        target_compile_options(tatarus_c PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    target_compile_options(tatarus_neurobiology PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(tatarus_organism PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(tatarus_sdk PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(TATARUS_BUILD_CORTEX)
        target_compile_options(tatarus_cortex PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()

if(TATARUS_BUILD_EXAMPLES)
    add_executable(tatarus_minimal_robot examples/minimal_robot.cpp)
    target_link_libraries(tatarus_minimal_robot PRIVATE tatarus_sdk)

    add_executable(tatarus_identity_demo examples/identity_demo.cpp)
    target_link_libraries(tatarus_identity_demo PRIVATE tatarus_sdk)

    add_executable(tatarus_embodied_demo examples/embodied_demo.cpp)
    target_link_libraries(tatarus_embodied_demo PRIVATE tatarus_sdk)

    add_executable(tatarus_explorer_rover examples/explorer_rover.cpp)
    target_link_libraries(tatarus_explorer_rover PRIVATE tatarus_sdk)

    add_executable(tatarus_imaginatio_demo examples/imaginatio_demo.cpp)
    target_link_libraries(tatarus_imaginatio_demo PRIVATE tatarus_sdk)

    add_executable(tatarus_organism_embodied_demo examples/organism_embodied_demo.cpp)
    target_link_libraries(tatarus_organism_embodied_demo PRIVATE tatarus_sdk tatarus_organism)

    if(TATARUS_BUILD_CORTEX)
        add_executable(tatarus_cortex_probe tools/cortex_probe/main.cpp)
        target_link_libraries(tatarus_cortex_probe PRIVATE tatarus_cortex)

        add_executable(tatarus_cortex_rover_demo examples/cortex_rover_demo.cpp)
        target_link_libraries(tatarus_cortex_rover_demo PRIVATE tatarus_cortex)

        add_executable(tatarus_cortex_hybrid_demo examples/cortex_hybrid_demo.cpp)
        target_link_libraries(tatarus_cortex_hybrid_demo PRIVATE tatarus_cortex)

        add_executable(tatarus_cortex_counterfactual_teacher_demo examples/cortex_counterfactual_teacher_demo.cpp)
        target_link_libraries(tatarus_cortex_counterfactual_teacher_demo PRIVATE tatarus_cortex)

        add_executable(tatarus_cortex_topdown_sleep_demo examples/cortex_topdown_sleep_demo.cpp)
        target_link_libraries(tatarus_cortex_topdown_sleep_demo PRIVATE tatarus_cortex)

        add_executable(tatarus_cortex_executive_replay_demo examples/cortex_executive_replay_demo.cpp)
        target_link_libraries(tatarus_cortex_executive_replay_demo PRIVATE tatarus_cortex)
    endif()

    if(TATARUS_BUILD_SHARED)
        add_executable(tatarus_c_demo examples/c_api_demo.c)
        target_link_libraries(tatarus_c_demo PRIVATE tatarus_c)
    endif()
endif()

include(CTest)
if(TATARUS_BUILD_TESTS)
    add_executable(tatarus_sdk_tests tests/tatarus_sdk_tests.cpp)
    target_link_libraries(tatarus_sdk_tests PRIVATE tatarus_sdk)
    add_test(NAME tatarus_sdk_tests COMMAND tatarus_sdk_tests)

    add_executable(tatarus_imaginatio_tests tests/tatarus_imaginatio_tests.cpp)
    target_link_libraries(tatarus_imaginatio_tests PRIVATE tatarus_sdk)
    add_test(
        NAME tatarus_imaginatio_tests
        COMMAND tatarus_imaginatio_tests ${CMAKE_CURRENT_BINARY_DIR}/tatarus_imaginatio_test_output
    )

    add_executable(tatarus_neurobiology_tests tests/tatarus_neurobiology_tests.cpp)
    target_link_libraries(tatarus_neurobiology_tests PRIVATE tatarus_neurobiology)

    add_executable(tatarus_sensory_organs_tests tests/tatarus_sensory_organs_tests.cpp)
    target_link_libraries(tatarus_sensory_organs_tests PRIVATE tatarus_sdk tatarus_neurobiology)
    add_test(NAME tatarus_sensory_organs_tests COMMAND tatarus_sensory_organs_tests)

    add_executable(tatarus_object_cortex_tests tests/tatarus_object_cortex_tests.cpp)
    target_link_libraries(tatarus_object_cortex_tests PRIVATE tatarus_sdk)
    add_test(NAME tatarus_object_cortex_tests COMMAND tatarus_object_cortex_tests)

    add_executable(tatarus_native_render_tests tests/tatarus_native_render_tests.cpp)
    target_link_libraries(tatarus_native_render_tests PRIVATE tatarus_sdk)
    add_test(NAME tatarus_native_render_tests COMMAND tatarus_native_render_tests)
    add_test(
        NAME tatarus_neurobiology_tests
        COMMAND tatarus_neurobiology_tests ${CMAKE_CURRENT_BINARY_DIR}/tatarus_neurobiology_test_output
    )

    add_executable(tatarus_physiology_tests tests/tatarus_physiology_tests.cpp)
    target_link_libraries(tatarus_physiology_tests PRIVATE tatarus_neurobiology)
    add_test(
        NAME tatarus_physiology_tests
        COMMAND tatarus_physiology_tests ${CMAKE_CURRENT_BINARY_DIR}/tatarus_physiology_test_output
    )

    add_executable(tatarus_circulation_tests tests/tatarus_circulation_tests.cpp)
    target_link_libraries(tatarus_circulation_tests PRIVATE tatarus_organism tatarus_sdk)
    add_test(NAME tatarus_circulation_tests COMMAND tatarus_circulation_tests)

    add_executable(tatarus_heart_tests tests/tatarus_heart_tests.cpp)
    target_link_libraries(tatarus_heart_tests PRIVATE tatarus_organism tatarus_sdk)
    add_test(NAME tatarus_heart_tests COMMAND tatarus_heart_tests)

    add_executable(tatarus_kidney_tests tests/tatarus_kidney_tests.cpp)
    target_link_libraries(tatarus_kidney_tests PRIVATE tatarus_organism tatarus_sdk)
    add_test(NAME tatarus_kidney_tests COMMAND tatarus_kidney_tests)

    add_executable(tatarus_causal_validation_tests tests/tatarus_causal_validation_tests.cpp)
    target_link_libraries(tatarus_causal_validation_tests PRIVATE tatarus_organism tatarus_sdk)
    add_test(NAME tatarus_causal_validation_tests COMMAND tatarus_causal_validation_tests)

    if(TATARUS_BUILD_CORTEX)
        add_executable(tatarus_cortex_tests tests/tatarus_cortex_tests.cpp)
        target_link_libraries(tatarus_cortex_tests PRIVATE tatarus_cortex)
        add_test(
            NAME tatarus_cortex_tests
            COMMAND tatarus_cortex_tests ${CMAKE_CURRENT_BINARY_DIR}/tatarus_cortex_test_output
        )

        add_executable(tatarus_cortex_phase12_15_tests tests/tatarus_cortex_phase12_15_tests.cpp)
        target_link_libraries(tatarus_cortex_phase12_15_tests PRIVATE tatarus_cortex)
        add_test(
            NAME tatarus_cortex_phase12_15_tests
            COMMAND tatarus_cortex_phase12_15_tests ${CMAKE_CURRENT_BINARY_DIR}/tatarus_cortex_phase12_15_output
        )

        if(TATARUS_BUILD_SHARED)
            add_executable(tatarus_cortex_ui_bridge_tests tests/tatarus_cortex_ui_bridge_tests.cpp)
            target_link_libraries(tatarus_cortex_ui_bridge_tests PRIVATE tatarus_c)
            add_test(
                NAME tatarus_cortex_ui_bridge_tests
                COMMAND tatarus_cortex_ui_bridge_tests ${CMAKE_CURRENT_SOURCE_DIR}/config/cortex_hybrid.json
            )
        endif()
    endif()

    if(TATARUS_BUILD_SHARED)
        add_executable(tatarus_organism_tests tests/tatarus_organism_tests.cpp)
        target_link_libraries(tatarus_organism_tests PRIVATE tatarus_organism tatarus_sdk tatarus_c)
        add_test(NAME tatarus_organism_tests COMMAND tatarus_organism_tests)

        add_executable(tatarus_cartography_c_tests tests/tatarus_cartography_c_tests.c)
        target_link_libraries(tatarus_cartography_c_tests PRIVATE tatarus_c)
        add_test(NAME tatarus_cartography_c_tests COMMAND tatarus_cartography_c_tests)
    endif()

    if(TATARUS_BUILD_SHARED)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(
                NAME tatarus_embodiment_tests
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/tatarus_embodiment_tests.py
                        --library $<TARGET_FILE:tatarus_c>
            )
        endif()
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS tatarus_sdk tatarus_neurobiology tatarus_organism
    EXPORT TatarusSDKTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(TATARUS_BUILD_CORTEX)
    install(TARGETS tatarus_cortex
        EXPORT TatarusSDKTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
if(TATARUS_BUILD_SHARED)
    install(TARGETS tatarus_c
        EXPORT TatarusSDKTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT TatarusSDKTargets
    FILE TatarusSDKTargets.cmake
    NAMESPACE Tatarus::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TatarusSDK
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/TatarusSDKConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/TatarusSDKConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TatarusSDK
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/TatarusSDKConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/TatarusSDKConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/TatarusSDKConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TatarusSDK
)
