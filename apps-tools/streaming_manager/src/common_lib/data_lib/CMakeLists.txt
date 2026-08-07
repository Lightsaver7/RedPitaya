cmake_minimum_required(VERSION ${CMAKEVERS})

project(data_lib)

option(BUILD_UNIT_TESTS "Build data_lib unit tests (GoogleTest)" ON)
option(DATA_ENABLE_SANITIZERS "Build data_lib and its tests with ASan+UBSan" OFF)

message(STATUS "${Yellow}Project=${PROJECT_NAME}${ColourReset}")
message(STATUS "RedPitaya platform=${RP_PLATFORM}")
message(STATUS "CPU ARCH=${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "VERSION=${VERSION}")
message(STATUS "REVISION=${REVISION}")
message(STATUS "BUILD_NUMBER=${BUILD_NUMBER}")
message(STATUS "LINUX_VER=${LINUX_VER}")

message(STATUS "Compiler C path: ${CMAKE_C_COMPILER}")
message(STATUS "Compiler C ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "Compiler C version: ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Compiler C is part: ${CMAKE_COMPILER_IS_GNUC}")

message(STATUS "Compiler C++ path: ${CMAKE_CXX_COMPILER}")
message(STATUS "Compiler C++ ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Compiler C++version: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Compiler C++ is part: ${CMAKE_COMPILER_IS_GNUCXX}")

message(STATUS "Build ${PROJECT_NAME} unit tests (GoogleTest): ${BUILD_UNIT_TESTS}")

message(STATUS "Host system name=${CMAKE_HOST_SYSTEM_NAME}")
message(STATUS "Host system processor=${CMAKE_HOST_SYSTEM_PROCESSOR}")

message(STATUS "Target system name=${CMAKE_SYSTEM_NAME}")
message(STATUS "Target system processor=${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Cross compiling=${CMAKE_CROSSCOMPILING}")

FILE(GLOB_RECURSE INC_ALL "*.h")

FILE(GLOB_RECURSE INC_ALL2 "*.hpp")

add_library(${PROJECT_NAME} ${INC_ALL} ${INC_ALL2})

target_include_directories(${PROJECT_NAME}
    PUBLIC  ${PROJECT_SOURCE_DIR}
            ${COMMON_LIB_DIR}
    )

list(APPEND headers
            ${PROJECT_SOURCE_DIR}/buffer.h
            ${PROJECT_SOURCE_DIR}/buffers_pack.h
            ${PROJECT_SOURCE_DIR}/buffers_cached.h
            ${PROJECT_SOURCE_DIR}/network_header.h
            ${PROJECT_SOURCE_DIR}/neon_asm.h
            ${PROJECT_SOURCE_DIR}/signal.hpp
        )

list(APPEND src
            ${PROJECT_SOURCE_DIR}/buffer.cpp
            ${PROJECT_SOURCE_DIR}/buffers_pack.cpp
            ${PROJECT_SOURCE_DIR}/buffers_cached.cpp
            ${PROJECT_SOURCE_DIR}/network_header.cpp
            ${PROJECT_SOURCE_DIR}/neon_asm.cpp
        )

target_sources(${PROJECT_NAME} PRIVATE ${src})

target_link_libraries(${PROJECT_NAME}
    PRIVATE pthread stdc++)


if(DATA_ENABLE_SANITIZERS)
    set(_data_sanitizer_flags -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g)
    target_compile_options(${PROJECT_NAME} PUBLIC ${_data_sanitizer_flags})
    target_link_options(${PROJECT_NAME} PUBLIC ${_data_sanitizer_flags})
endif()

if (NOT CMAKE_CROSSCOMPILING)
    if(BUILD_UNIT_TESTS)
        enable_testing()
        add_subdirectory(tests)

        add_custom_target(rp_data_lib_run_tests ALL
            COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Running data_lib unit tests (ctest) - the build fails if any test fails"
            VERBATIM)
        add_dependencies(rp_data_lib_run_tests data_lib_tests)
    endif()
endif()


file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin/include/${PROJECT_NAME}")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${headers}
        "${CMAKE_BINARY_DIR}/bin/include/${PROJECT_NAME}"
    COMMENT "Copying ${PROJECT_NAME} public headers to ${CMAKE_BINARY_DIR}/include/${PROJECT_NAME}"
    VERBATIM)
