cmake_minimum_required(VERSION 3.25)

project(OwOInputMethod VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

add_library(owo_engine STATIC
    src/engine/full_pinyin_schema.cpp
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

if(WIN32)
    add_library(owo_ipc STATIC src/ipc/named_pipe.cpp)
    target_include_directories(owo_ipc PUBLIC include)
    target_link_libraries(owo_ipc PUBLIC owo_protocol)
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

if(BUILD_TESTING)
    add_executable(owo_full_pinyin_tests tests/engine/full_pinyin_schema_tests.cpp)
    target_link_libraries(owo_full_pinyin_tests PRIVATE owo_engine)
    target_compile_features(owo_full_pinyin_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_full_pinyin_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(owo_full_pinyin_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME owo.engine.full_pinyin COMMAND owo_full_pinyin_tests)

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
        add_test(NAME owo.ipc.core_service
                 COMMAND owo_ipc_integration $<TARGET_FILE:owo_core_service>)
        set_tests_properties(owo.ipc.core_service PROPERTIES TIMEOUT 10)
    endif()
endif()
