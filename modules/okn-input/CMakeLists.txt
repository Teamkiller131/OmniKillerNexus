cmake_minimum_required(VERSION 3.20)
project(okn-input LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build tests" OFF)

file(GLOB_RECURSE ${PROJECT_NAME}_SRC
        CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_library(${PROJECT_NAME} STATIC ${${PROJECT_NAME}_SRC})
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(BUILD_TESTS)
    find_package(doctest CONFIG QUIET)
    if(doctest_FOUND)
        enable_testing()
        file(GLOB_RECURSE ${PROJECT_NAME}_TESTS
                CONFIGURE_DEPENDS
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
        )
        if(${PROJECT_NAME}_TESTS)
            add_executable(${PROJECT_NAME}_tests ${${PROJECT_NAME}_TESTS})
            target_link_libraries(${PROJECT_NAME}_tests PRIVATE ${PROJECT_NAME} doctest::doctest)
            add_test(NAME ${PROJECT_NAME}_tests COMMAND ${PROJECT_NAME}_tests)
        endif()
    else()
        message(WARNING "doctest not found — tests disabled for okn-input")
    endif()
endif()
