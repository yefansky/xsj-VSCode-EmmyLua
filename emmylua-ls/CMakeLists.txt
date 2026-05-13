cmake_minimum_required(VERSION 3.20)
project(emmylua-ls LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Dependencies via FetchContent
include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE
)

set(SPDLOG_NO_EXCEPTIONS ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(json spdlog)

# Source files
set(EMMY_LUA_LS_SOURCES
    src/main.cpp
    src/lsp/Transport.cpp
    src/lsp/Dispatcher.cpp
    src/parser/Lexer.cpp
    src/parser/Parser.cpp
    src/parser/AnnotationParser.cpp
    src/index/DocumentIndex.cpp
    src/index/SymbolIndex.cpp
    src/index/WorkspaceManager.cpp
)

add_executable(emmylua-ls ${EMMY_LUA_LS_SOURCES})

target_include_directories(emmylua-ls PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(emmylua-ls PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# Compiler warnings
if(MSVC)
    target_compile_options(emmylua-ls PRIVATE /W4 /WX-)
    target_compile_definitions(emmylua-ls PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(emmylua-ls PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests
option(EMMY_BUILD_TESTS "Build tests" ON)
if(EMMY_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(doctest)

    add_executable(emmylua-ls-test
        test/test_main.cpp
        test/test_lsp_transport.cpp
        test/test_parser.cpp
        test/test_index.cpp
        test/test_workspace.cpp
        test/test_real_files.cpp
        test/test_base_lua.cpp
        test/test_encoding.cpp
        src/lsp/Transport.cpp
        src/parser/Lexer.cpp
        src/parser/Parser.cpp
        src/parser/AnnotationParser.cpp
        src/index/DocumentIndex.cpp
        src/index/SymbolIndex.cpp
    )
    target_include_directories(emmylua-ls-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(emmylua-ls-test PRIVATE
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        doctest::doctest
    )
    add_test(NAME emmylua-ls-test COMMAND emmylua-ls-test)
endif()
