cmake_minimum_required(VERSION 3.16)

project(alchemy_hpc_mpi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_MPI "Build the MPI executable" ON)

find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(FATAL_ERROR
        "nlohmann_json was not found. Install it first, for example:\n"
        "  Ubuntu/Debian: sudo apt install nlohmann-json3-dev\n"
        "  MSYS2 UCRT64:   pacman -S mingw-w64-ucrt-x86_64-nlohmann-json\n")
endif()

find_package(OpenMP QUIET)

set(ALCHEMY_COMMON_SOURCES
    src/common/RecipeGraph.cpp
    src/common/RecipeTree.cpp
    src/common/JsonLoader.cpp
    src/common/TierCatalog.cpp
    src/common/Search.cpp
    src/common/Visualizer.cpp
    src/common/Cli.cpp
    src/common/Statistics.cpp
)

add_library(alchemy_common ${ALCHEMY_COMMON_SOURCES})
target_include_directories(alchemy_common PUBLIC src)
target_link_libraries(alchemy_common PUBLIC nlohmann_json::nlohmann_json)
if(OpenMP_CXX_FOUND)
    target_compile_definitions(alchemy_common PUBLIC ALCHEMY_HAS_OPENMP=1)
    target_link_libraries(alchemy_common PUBLIC OpenMP::OpenMP_CXX)
else()
    message(STATUS "OpenMP C++ was not found, so alchemy_openmp will not be built and MPI hybrid threads are disabled.")
endif()

add_executable(alchemy_serial src/serial/main_serial.cpp)
target_link_libraries(alchemy_serial PRIVATE alchemy_common)

if(OpenMP_CXX_FOUND)
    add_executable(alchemy_openmp src/openmp/main_openmp.cpp)
    target_link_libraries(alchemy_openmp PRIVATE alchemy_common)
endif()

if(BUILD_MPI)
    find_package(MPI QUIET)
    if(MPI_CXX_FOUND)
        add_executable(alchemy_mpi
            src/mpi/main_mpi.cpp
            src/mpi/MpiMaster.cpp
            src/mpi/MpiWorker.cpp
        )
        target_include_directories(alchemy_mpi PRIVATE ${MPI_CXX_INCLUDE_DIRS})
        target_link_libraries(alchemy_mpi PRIVATE alchemy_common MPI::MPI_CXX)
    else()
        message(WARNING
            "MPI C++ was not found, so alchemy_mpi will not be built. "
            "Install OpenMPI/MPICH/MS-MPI development headers and rerun CMake.")
    endif()
endif()

enable_testing()
add_executable(alchemy_tests tests/test_search.cpp)
target_link_libraries(alchemy_tests PRIVATE alchemy_common)
add_test(NAME alchemy_tests COMMAND alchemy_tests)
set_tests_properties(alchemy_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
