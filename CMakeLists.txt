cmake_minimum_required(VERSION 3.12)

project(Grover)

set(CMAKE_CXX_STANDARD 20)

# Use local installations if available
if(DEFINED GUROBI_ROOT)
    set(GUROBI_ROOT "${GUROBI_ROOT}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/extern/local")
    set(GUROBI_ROOT "${CMAKE_SOURCE_DIR}/extern/local")
else()
    set(GUROBI_ROOT "$ENV{GUROBI_HOME}")
endif()

if(DEFINED NETWORKIT_ROOT)
    set(NETWORKIT_ROOT "${NETWORKIT_ROOT}")
elseif(EXISTS "${CMAKE_SOURCE_DIR}/extern/local")
    set(NETWORKIT_ROOT "${CMAKE_SOURCE_DIR}/extern/local")
else()
    set(NETWORKIT_ROOT "/usr/local")
endif()

include_directories(
    "${GUROBI_ROOT}/include"
    "${NETWORKIT_ROOT}/include"
)

# Debug flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Release flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -finline-functions -fwhole-program -ftree-vectorize -flto -funroll-loops -falign-loops -march=native")

file(GLOB_RECURSE sources src/*.cpp src/*.h)

add_executable(grover main.cpp ${sources})

target_link_libraries(
    grover
    ${GUROBI_ROOT}/lib/libgurobi_c++.a
    ${GUROBI_ROOT}/lib/libgurobi120.so
    ${NETWORKIT_ROOT}/lib/libnetworkit.so
)

