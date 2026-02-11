#!/bin/bash
set -e

# Parse arguments
GUROBI_PATH=""
DOWNLOAD_GUROBI=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --gurobi-path)
            GUROBI_PATH="$2"
            shift 2
            ;;
        --download-gurobi)
            DOWNLOAD_GUROBI=true
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--gurobi-path PATH] [--download-gurobi]"
            exit 1
            ;;
    esac
done

# Check for Gurobi
if [ -z "$GUROBI_PATH" ] && [ "$DOWNLOAD_GUROBI" = false ]; then
    if [ -n "$GUROBI_HOME" ]; then
        GUROBI_PATH="$GUROBI_HOME"
        echo "Using Gurobi from GUROBI_HOME: $GUROBI_PATH"
    else
        echo "Error: Gurobi not specified."
        echo "Options:"
        echo "  1. Set GUROBI_HOME environment variable"
        echo "  2. Use --gurobi-path PATH"
        echo "  3. Use --download-gurobi to download Gurobi 12.0.1"
        exit 1
    fi
fi

# Create extern directory
mkdir -p extern
rm -rf extern/gurobi1201
rm -rf extern/local
rm -rf extern/networkit
cd extern

# Create local directory structure
mkdir -p local/{bin,include,lib}

# Setup Gurobi
if [ "$DOWNLOAD_GUROBI" = true ]; then
    echo "Downloading Gurobi 12.0.1..."
    if [ ! -f "gurobi12.0.1_linux64.tar.gz" ]; then
        wget https://packages.gurobi.com/12.0/gurobi12.0.1_linux64.tar.gz
    fi
    
    if [ ! -d "gurobi1201" ]; then
        tar -xzf gurobi12.0.1_linux64.tar.gz
    fi
    
    GUROBI_PATH="$(pwd)/gurobi1201/linux64"
fi

# Copy Gurobi files
echo "Using Gurobi from: $GUROBI_PATH"
cp -r "$GUROBI_PATH"/bin/* local/bin/
cp -r "$GUROBI_PATH"/include/* local/include/
cp -r "$GUROBI_PATH"/lib/* local/lib/

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
