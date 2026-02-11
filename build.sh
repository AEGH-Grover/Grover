#!/bin/bash
set -e

# Create extern directory
mkdir -p extern
rm -rf extern/gurobi1201
rm -rf extern/local
rm -rf extern/networkit
cd extern

# Download and setup Gurobi
if [ ! -f "gurobi12.0.1_linux64.tar.gz" ]; then
    wget https://packages.gurobi.com/12.0/gurobi12.0.1_linux64.tar.gz
fi

if [ ! -d "gurobi1201" ]; then
    tar -xzf gurobi12.0.1_linux64.tar.gz
fi

# Create local directory structure
mkdir -p local/{bin,include,lib}

# Copy Gurobi files
cp -r gurobi1201/linux64/bin/* local/bin/
cp -r gurobi1201/linux64/include/* local/include/
cp -r gurobi1201/linux64/lib/* local/lib/

# Download NetworKit stable release
if [ ! -d "networkit" ]; then
    git clone --recursive https://github.com/networkit/networkit.git
    cd networkit
    git checkout 11.0  # Use stable release
    git submodule update --init --recursive
    cd ..
fi

cd networkit

# Apply patch for reduction parameter
patch -p1 < ../networkit-reduction.patch

# Build C++ library only
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DNETWORKIT_BUILD_TESTS=OFF
make -j$(nproc)
cd ..

# Copy library and headers
cp build/libnetworkit.so ../local/lib/
cp -r include/* ../local/include/
cp -r extlibs/tlx/tlx ../local/include/

cd ../..

# Build project
rm -rf build
mkdir build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DGUROBI_ROOT=$(pwd)/extern/local -DNETWORKIT_ROOT=$(pwd)/extern/local
cd build && make
