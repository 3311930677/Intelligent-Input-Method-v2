cmake_minimum_required(VERSION 3.25)

project(OwOInputMethod VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

add_library(owo_engine STATIC
    src/engine/binary_lexicon.cpp
    src/engine/candidate_generator.cpp
    src/engine/full_pinyin_schema.cpp
    src/engine/lexicon.cpp
    src/engine/language_model.cpp
    src/engine/user_frequency.cpp
)
target_include_directories(owo_engine PUBLIC include)
target_compile_features(owo_engine PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(owo_engine PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(owo_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(owo_protocol STATIC
    src/protocol/envelope.cpp
    src/protocol/messages.cpp
)
target_include_directories(owo_protocol PUBLIC include)
target_compile_features(owo_protocol PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(owo_protocol PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(owo_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(owo_model STATIC
    src/model/model_assets.cpp
    src/model/model_backend.cpp
    src/model/model_protocol.cpp
)
target_include_directories(owo_model PUBLIC include)
target_compile_features(owo_model PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(owo_model PUBLIC bcrypt)
endif()
if(MSVC)
    target_compile_options(owo_model PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(owo_model PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_library(owo_ipc STATIC src/ipc/named_pipe.cpp)
    target_include_directories(owo_ipc PUBLIC include)
    target_link_libraries(owo_ipc PUBLIC owo_protocol owo_engine owo_model)
    target_compile_features(owo_ipc PUBLIC cxx_std_20)
    target_compile_definitions(owo_ipc PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    if(MSVC)
        target_compile_options(owo_ipc PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(owo_core_service apps/core_service/main.cpp)
    target_link_libraries(owo_core_service PRIVATE owo_ipc)
    target_compile_features(owo_core_service PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_core_service PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(owo_ipc_shell apps/ipc_shell/main.cpp)
    target_link_libraries(owo_ipc_shell PRIVATE owo_ipc)
    target_compile_features(owo_ipc_shell PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_ipc_shell PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(owo_model_host apps/model_host/main.cpp)
    target_link_libraries(owo_model_host PRIVATE owo_ipc)
    target_compile_features(owo_model_host PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_model_host PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(owo_model_shell apps/model_shell/main.cpp)
    target_link_libraries(owo_model_shell PRIVATE owo_ipc)
    target_compile_features(owo_model_shell PRIVATE cxx_std_20)
    target_compile_definitions(owo_model_shell PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(owo_model_shell PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_library(owo_tsf SHARED
        src/tsf/owo_tsf.def
        src/tsf/dll_main.cpp
        src/tsf/text_service.cpp
    )
    target_include_directories(owo_tsf PRIVATE include src/tsf)
    target_compile_features(owo_tsf PRIVATE cxx_std_20)
    target_compile_definitions(owo_tsf PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    target_link_libraries(owo_tsf PRIVATE owo_ipc ole32 advapi32 uuid user32)
    set_target_properties(owo_tsf PROPERTIES OUTPUT_NAME "OwO.TSF")
    if(MSVC)
        target_compile_options(owo_tsf PRIVATE /W4 /permissive- /utf-8)
    endif()

    add_executable(owo_tsf_profile_check tools/tsf_profile_check.cpp)
    target_compile_features(owo_tsf_profile_check PRIVATE cxx_std_20)
    target_compile_definitions(owo_tsf_profile_check PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(owo_tsf_profile_check PRIVATE ole32 uuid)
    if(MSVC)
        target_compile_options(owo_tsf_profile_check PRIVATE /W4 /permissive- /utf-8)
    endif()

endif()

add_executable(owo_lexicon_compiler tools/lexicon_compiler/main.cpp)
target_link_libraries(owo_lexicon_compiler PRIVATE owo_engine)
if(WIN32)
    target_link_libraries(owo_lexicon_compiler PRIVATE bcrypt)
endif()

add_executable(owo_lexicon_merge tools/lexicon_merge/main.cpp)
target_link_libraries(owo_lexicon_merge PRIVATE owo_engine)
target_compile_features(owo_lexicon_merge PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(owo_lexicon_merge PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(owo_lexicon_merge PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_compile_features(owo_lexicon_compiler PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(owo_lexicon_compiler PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(owo_lexicon_compiler PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(owo_engine_benchmark benchmarks/engine_benchmark.cpp)
target_link_libraries(owo_engine_benchmark PRIVATE owo_engine)
target_compile_features(owo_engine_benchmark PRIVATE cxx_std_20)
if(MSVC)
    target_compile_definitions(owo_engine_benchmark PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_compile_options(owo_engine_benchmark PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(owo_engine_benchmark PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(owo_model_backend_tests tests/model/model_backend_tests.cpp)
    target_link_libraries(owo_model_backend_tests PRIVATE owo_model)
    target_compile_features(owo_model_backend_tests PRIVATE cxx_std_20)
    add_test(NAME owo.model.backend COMMAND owo_model_backend_tests)
    add_executable(owo_model_assets_tests tests/model/model_assets_tests.cpp)
    target_link_libraries(owo_model_assets_tests PRIVATE owo_model)
    target_compile_features(owo_model_assets_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_model_assets_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME owo.model.assets COMMAND owo_model_assets_tests
             ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/model_fixture.manifest)
    add_executable(owo_model_protocol_tests tests/model/model_protocol_tests.cpp)
    target_link_libraries(owo_model_protocol_tests PRIVATE owo_model owo_protocol)
    target_compile_features(owo_model_protocol_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_model_protocol_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME owo.model.protocol COMMAND owo_model_protocol_tests)

    add_executable(owo_full_pinyin_tests tests/engine/full_pinyin_schema_tests.cpp)
    target_link_libraries(owo_full_pinyin_tests PRIVATE owo_engine)
    target_compile_features(owo_full_pinyin_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_full_pinyin_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(owo_full_pinyin_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME owo.engine.full_pinyin COMMAND owo_full_pinyin_tests)

    add_executable(owo_candidate_generator_tests tests/engine/candidate_generator_tests.cpp)
    target_link_libraries(owo_candidate_generator_tests PRIVATE owo_engine)
    target_compile_features(owo_candidate_generator_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_candidate_generator_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(owo_candidate_generator_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME owo.engine.candidates COMMAND owo_candidate_generator_tests)

    add_executable(owo_binary_lexicon_tests tests/engine/binary_lexicon_tests.cpp)
    target_link_libraries(owo_binary_lexicon_tests PRIVATE owo_engine)
    target_compile_features(owo_binary_lexicon_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_binary_lexicon_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(owo_binary_lexicon_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME owo.engine.binary_lexicon COMMAND owo_binary_lexicon_tests)

    add_executable(owo_user_frequency_tests tests/engine/user_frequency_tests.cpp)
    target_link_libraries(owo_user_frequency_tests PRIVATE owo_engine)
    target_compile_features(owo_user_frequency_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_user_frequency_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(owo_user_frequency_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME owo.engine.user_frequency COMMAND owo_user_frequency_tests)

    add_test(NAME owo.lexicon.compiler
             COMMAND owo_lexicon_compiler
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/lexicon_fixture.tsv
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/lexicon_fixture.manifest
                     ${CMAKE_CURRENT_BINARY_DIR}/lexicon_fixture.owolx)
    add_test(NAME owo.lexicon.compiler_rime_yaml
             COMMAND owo_lexicon_compiler
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/rime_lexicon_fixture.dict.yaml
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/rime_lexicon_fixture.manifest
                     ${CMAKE_CURRENT_BINARY_DIR}/rime_lexicon_fixture.owolx)
    add_test(NAME owo.lexicon.compiler_rime_auto
             COMMAND owo_lexicon_compiler
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/rime_auto_fixture.dict.yaml
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/rime_auto_fixture.manifest
                     ${CMAKE_CURRENT_BINARY_DIR}/rime_auto_fixture.owolx
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/rime_pronunciation_fixture.dict.yaml)
    set_tests_properties(owo.lexicon.compiler_rime_auto PROPERTIES FIXTURES_SETUP rime_auto_fixture)
    add_executable(owo_auto_lexicon_check tests/engine/auto_lexicon_check.cpp)
    target_link_libraries(owo_auto_lexicon_check PRIVATE owo_engine)
    target_compile_features(owo_auto_lexicon_check PRIVATE cxx_std_20)
    add_test(NAME owo.lexicon.compiler_rime_auto_result
             COMMAND owo_auto_lexicon_check ${CMAKE_CURRENT_BINARY_DIR}/rime_auto_fixture.owolx)
    set_tests_properties(owo.lexicon.compiler_rime_auto_result PROPERTIES FIXTURES_REQUIRED rime_auto_fixture)
    add_test(NAME owo.lexicon.compiler_rejects_manifest
             COMMAND owo_lexicon_compiler
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/lexicon_fixture.tsv
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/lexicon_invalid.manifest
                     ${CMAKE_CURRENT_BINARY_DIR}/lexicon_invalid.owolx)
    set_tests_properties(owo.lexicon.compiler_rejects_manifest PROPERTIES WILL_FAIL TRUE)
    add_test(NAME owo.lexicon.compiler_rejects_hash
             COMMAND owo_lexicon_compiler
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/lexicon_fixture.tsv
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/lexicon_wrong_hash.manifest
                     ${CMAKE_CURRENT_BINARY_DIR}/lexicon_wrong_hash.owolx)
    set_tests_properties(owo.lexicon.compiler_rejects_hash PROPERTIES WILL_FAIL TRUE)

    add_executable(owo_protocol_tests tests/protocol/envelope_tests.cpp)
    target_link_libraries(owo_protocol_tests PRIVATE owo_protocol)
    target_compile_features(owo_protocol_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_protocol_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(owo_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME owo.protocol.envelope COMMAND owo_protocol_tests)

    if(WIN32)
        add_test(NAME owo.model.host_rejects_bad_assets
                 COMMAND owo_model_host --asset-manifest
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/model_wrong_hash.manifest)
        set_tests_properties(owo.model.host_rejects_bad_assets PROPERTIES WILL_FAIL TRUE)
        add_executable(owo_messages_tests tests/protocol/messages_tests.cpp)
        target_link_libraries(owo_messages_tests PRIVATE owo_protocol)
        target_compile_features(owo_messages_tests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(owo_messages_tests PRIVATE /W4 /permissive- /utf-8)
        endif()
        add_test(NAME owo.protocol.messages COMMAND owo_messages_tests)

        add_executable(owo_tsf_smoke tests/tsf/dll_smoke.cpp)
        target_compile_features(owo_tsf_smoke PRIVATE cxx_std_20)
        target_compile_definitions(owo_tsf_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(owo_tsf_smoke PRIVATE ole32 uuid)
        if(MSVC)
            target_compile_options(owo_tsf_smoke PRIVATE /W4 /permissive- /utf-8)
        endif()
        add_test(NAME owo.tsf.dll_smoke
                 COMMAND owo_tsf_smoke $<TARGET_FILE:owo_tsf>)

        add_executable(owo_ipc_integration tests/ipc/core_service_integration.cpp)
        target_link_libraries(owo_ipc_integration PRIVATE owo_ipc)
        target_compile_features(owo_ipc_integration PRIVATE cxx_std_20)
        target_compile_definitions(owo_ipc_integration PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        if(MSVC)
            target_compile_options(owo_ipc_integration PRIVATE /W4 /permissive- /utf-8)
        endif()
        add_executable(owo_core_service_contract tests/ipc/core_service_contract.cpp)
        target_link_libraries(owo_core_service_contract PRIVATE owo_ipc)
        target_compile_features(owo_core_service_contract PRIVATE cxx_std_20)
        target_compile_definitions(owo_core_service_contract PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        if(MSVC)
            target_compile_options(owo_core_service_contract PRIVATE /W4 /permissive- /utf-8)
        endif()
    endif()
endif()
