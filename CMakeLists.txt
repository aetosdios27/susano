cmake_minimum_required(VERSION 3.25)

project(susano
    VERSION 0.1.0
    DESCRIPTION "A from-scratch in-memory analytical database"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_executable(susano
    src/main.cpp
)

target_compile_features(susano PRIVATE cxx_std_23)

if(BUILD_TESTING)
    add_test(NAME susano_smoke COMMAND susano)
    set_tests_properties(susano_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "Susano is ready\\."
    )
endif()
