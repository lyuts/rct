cmake_minimum_required(VERSION 2.8)

# If building with tests, set flags to generate profile/coverage data
# for the compilation of the main code.
if (WITH_TESTS)
    SET(CMAKE_CXX_FLAGS "-g -O0 -Wall -fprofile-arcs -ftest-coverage")
    SET(CMAKE_C_FLAGS "-g -O0 -Wall -W -fprofile-arcs -ftest-coverage")
    SET(CMAKE_EXE_LINKER_FLAGS "-fprofile-arcs -ftest-coverage")
endif ()

include(rct.cmake)
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fstack-protector-all")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wstack-protector")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-all")
# set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstack-protector")

# add_executable(dbtest rct/dbtest.cpp)
# target_link_libraries(dbtest rct)

if (WITH_TESTS)
    enable_testing()
    add_subdirectory(tests)

    if (${CMAKE_CXX_COMPILER} MATCHES "clang")
        find_program(LLVM_COV_EXECUTABLE
            NAMES
            llvm-cov37 llvm-cov-3.7
            llvm-cov36 llvm-cov-3.6
            llvm-cov35 llvm-cov-3.5
            llvm-cov
            )

        add_custom_target(
            gen_llvm_cov_wrapper_script
            COMMAND
            echo "#!/bin/sh -xv" | tee llvm-cov.sh
            && echo ${LLVM_COV_EXECUTABLE} gcov $@ | tee -a llvm-cov.sh
            COMMAND chmod a+x llvm-cov.sh
            VERBATIM
        )

        set(GCOV_TOOL ./llvm-cov.sh)
    else ()
        find_program(GCOV_EXECUTABLE
            NAMES
            gcov49 gcov-4.9
            gcov48 gcov-4.8
            gcov47 gcov-4.7
            gcov
        )

        add_custom_target(
            gen_llvm_cov_wrapper_script
            COMMAND true
            VERBATIM
        )

        set(GCOV_TOOL ${GCOV_EXECUTABLE})
    endif ()

    find_program(LCOV_EXECUTABLE
        NAMES
            lcov
        PATHS
            ${PROJECT_BINARY_DIR}/lcov-1.12/bin
            lcov-1.12/bin
            )

    find_program(GENHTML_EXECUTABLE NAMES genhtml PATHS ${PROJECT_BINARY_DIR}/lcov-1.12/bin lcov-1.12/bin)

    message("lcov: ${LCOV_EXECUTABLE}")
    message("genhtml: ${GENHTML_EXECUTABLE}")

    if (GCOV_TOOL AND LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
        add_custom_target(
            coverage
            COMMAND
                ${LCOV_EXECUTABLE} -c -i -d . -b . --gcov-tool ${GCOV_TOOL} -o cov.info
                && ${LCOV_EXECUTABLE} --no-checksum -c -d . -b . --gcov-tool ${GCOV_TOOL} -o cov.info
                && ${LCOV_EXECUTABLE} -r cov.info "/usr/*" -o cov.info
                && rm -rf ./output
                && ${LCOV_EXECUTABLE} -r cov.info "tests/*" -o cov.info
            COMMAND ${GENHTML_EXECUTABLE} cov.info -o output
            DEPENDS gen_llvm_cov_wrapper_script
            VERBATIM
        )
    endif ()

endif ()

