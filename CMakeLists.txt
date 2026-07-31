cmake_minimum_required(VERSION 3.25)

project(OwOInputMethod VERSION 0.1.0 LANGUAGES CXX)

include(CTest)

add_library(owo_protocol STATIC
    src/protocol/envelope.cpp
)
target_include_directories(owo_protocol PUBLIC include)
target_compile_features(owo_protocol PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(owo_protocol PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(owo_protocol PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(owo_protocol_tests tests/protocol/envelope_tests.cpp)
    target_link_libraries(owo_protocol_tests PRIVATE owo_protocol)
    target_compile_features(owo_protocol_tests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(owo_protocol_tests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(owo_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME owo.protocol.envelope COMMAND owo_protocol_tests)
endif()

