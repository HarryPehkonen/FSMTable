cmake_minimum_required(VERSION 3.18)
project(fsmTable VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The gate counts warnings in the log as well as relying on -Werror, but a target that
# never got the flag would slip past. Every target below gets it explicitly.
set(FSMTABLE_WARNINGS -Wall -Wextra -Wpedantic -Werror)
set(FSMTABLE_SANITIZERS -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)

# The version number has exactly one home: project(... VERSION x.y.z) above. This is the
# second copy the gate's `version` stage compares against that line — generated, never
# typed, and at `build/generated/version.hpp` because that is the path the stage looks at
# by default (see CI_VERSION_HEADER). A .ci.env override would not survive a fresh clone:
# that file is gitignored, so the default has to be the real path.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp
    @ONLY
)

option(FSMTABLE_BUILD_TESTS "Build the GTest suite" ON)
option(FSMTABLE_BUILD_FUZZING "Build the libFuzzer target (clang only)" OFF)

add_library(fsmTable src/fsmtable.cpp)
target_include_directories(fsmTable PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(fsmTable PRIVATE ${FSMTABLE_WARNINGS})

if(FSMTABLE_BUILD_TESTS)
    include(CTest)
    find_package(GTest REQUIRED)
    add_executable(fsmTable_tests tests/parser_test.cpp)
    target_link_libraries(fsmTable_tests PRIVATE fsmTable GTest::gtest_main)
    target_compile_options(fsmTable_tests PRIVATE ${FSMTABLE_WARNINGS})
    # The corpus is input, not a build product: the test that walks it needs the source
    # directory, not the build directory, because ctest runs the binary outside the tree.
    target_compile_definitions(fsmTable_tests
        PRIVATE FSMTABLE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus")
    # The generated version header is build-tree only, and nothing in the library includes
    # it — the test below is the one translation unit that must compile it.
    target_include_directories(fsmTable_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    add_test(NAME fsmTable_tests COMMAND fsmTable_tests)
endif()

# The fuzz target is written against libFuzzer's entry point, so it can only be LINKED
# where the sanitizer runtime exists (clang). It is compiled in every configuration
# regardless, as an object library, because two stages need it to be: the warning count in
# `build` only covers what is compiled, and `lint` refuses to analyse a source the build
# never compiles — with reason, since a file linted only sometimes is not linted.
add_library(fsmtable_fuzz_obj OBJECT fuzz/fuzz_fsmtable.cpp)
target_link_libraries(fsmtable_fuzz_obj PRIVATE fsmTable)
target_compile_options(fsmtable_fuzz_obj PRIVATE ${FSMTABLE_WARNINGS}
    $<$<BOOL:${FSMTABLE_BUILD_FUZZING}>:${FSMTABLE_SANITIZERS}>)

if(FSMTABLE_BUILD_FUZZING)
    add_executable(fsmtable_fuzz $<TARGET_OBJECTS:fsmtable_fuzz_obj>)
    target_link_libraries(fsmtable_fuzz PRIVATE fsmTable)
    target_link_options(fsmtable_fuzz PRIVATE ${FSMTABLE_SANITIZERS})
endif()
