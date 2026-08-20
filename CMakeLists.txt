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
