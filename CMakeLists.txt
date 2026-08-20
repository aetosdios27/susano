cmake_minimum_required(VERSION 3.25)

project(susano
    VERSION 0.1.0
    DESCRIPTION "A from-scratch in-memory analytical database"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SUSANO_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SUSANO_BUILD_BENCHMARKS "Build Susano microbenchmarks" OFF)

include(CTest)

function(susano_configure_target target)
    target_compile_features(${target} PRIVATE cxx_std_23)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/src")

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
            -Wshadow
            -Werror
        )
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    endif()

    if(SUSANO_ENABLE_SANITIZERS)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            message(FATAL_ERROR "SUSANO_ENABLE_SANITIZERS requires GCC or Clang")
        endif()

        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

function(susano_add_test test_name source)
    add_executable(${test_name} ${source})
    susano_configure_target(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_executable(susano src/main.cpp)
susano_configure_target(susano)

if(SUSANO_BUILD_BENCHMARKS)
    add_executable(susano_bench_fixed_column benchmarks/fixed_column_scan.cpp)
    susano_configure_target(susano_bench_fixed_column)
endif()

if(BUILD_TESTING)
    add_test(NAME susano_smoke COMMAND susano)
    set_tests_properties(susano_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "Susano is ready\\."
    )

    susano_add_test(physical_type_test tests/physical_type_test.cpp)
    susano_add_test(row_id_test tests/row_id_test.cpp)
    susano_add_test(validity_bitmap_test tests/validity_bitmap_test.cpp)
    susano_add_test(fixed_column_test tests/fixed_column_test.cpp)
    susano_add_test(sorted_dictionary_test tests/sorted_dictionary_test.cpp)
endif()
