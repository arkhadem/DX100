# Artifcat of the DX100 Paper, ISCA 2025

This repository provides the GEM5 simulator, benchmarks, and automation scripts required for the artifact evaluation of the "DX100: A Programmable Data Access Accelerator for Indirection" paper published in ISCA 2025.

## Directory Structure

The repo structure is as follows:
  - [src/mem/MAA](/src/mem/MAA/): GEM5 source code of DX100.
  - [configs/common](/configs/common/): GEM5 Python scripts for DX100 configuration.
  - [benchmarks](/benchmarks/): DX100 memory-mapped and functional simulator APIs, as well as the evaluated kernels from NAS, GAP, Hash-Join, UME, and Spatter benchmark suites.
  - [scripts](/scripts/): Automation scripts required for running the simulations, parsing the results, and plotting the charts.
  - [results](/results/): After running the automation scripts, raw results and charts are stored in this directory.

## Requirements

This artifact requires the following dependencies. To ease the evaluation of this artifact, we have pre-installed all requirements in a docker image. We suggest using docker containers to reduce the setup effort:

```bash
docker pull arkhadem95/dx100:latest
docker run -it --name dx100_container -v /path/to/data/dir:/data -w /home/ubuntu arkhadem95/dx100 bash
```

Where `/path/to/data/dir` is your data directory with at least 20GB disk space.

If you choose to run artifact locally, you can use install the following dependencies:

### Ramulator2

Ramulator2 is tested with `g++-12` and `clang++-15` compilers.

### Gem5

Gem5 requires the following dependencies (taken from [Gem5 documentations](https://www.gem5.org/documentation/general_docs/building)):

- `SCons`: gem5 uses SCons as its build environment. SCons 3.0 or greater must be used.
- Python 3.6+: gem5 relies on Python development libraries. gem5 can be compiled and run in environments using Python 3.6+.
- protobuf 2.1+ (Optional): The protobuf library is used for trace generation and playback.
- Boost (Optional): The Boost library is a set of general purpose C++ libraries. It is a necessary dependency if you wish to use the SystemC implementation.

**Setup Gem5 on Ubuntu 24.04 (gem5 >= v24.0)**

If compiling gem5 on Ubuntu 24.04, or related Linux distributions, you may install all these dependencies using APT:

```bash
sudo apt install build-essential scons python3-dev git pre-commit zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    libboost-all-dev  libhdf5-serial-dev python3-pydot python3-venv python3-tk mypy \
    m4 libcapstone-dev libpng-dev libelf-dev pkg-config wget cmake doxygen
```

**Setup Gem5 on Ubuntu 22.04 (gem5 >= v21.1)**

If compiling gem5 on Ubuntu 22.04, or related Linux distributions, you may install all these dependencies using APT:

```bash
sudo apt install build-essential git m4 scons zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    python3-dev libboost-all-dev pkg-config python3-tk
```

**Setup Gem5 on Ubuntu 20.04 (gem5 >= v21.0)**

If compiling gem5 on Ubuntu 20.04, or related Linux distributions, you may install all these dependencies using APT:

```bash
sudo apt install build-essential git m4 scons zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    python3-dev python-is-python3 libboost-all-dev pkg-config gcc-10 g++-10 \
    python3-tk
```

### Automation Scripts

These scripts require Python 3.6+ and `matplotlib` package.

## Build

Building this artifact requires 

### 1- Clone Repository

```bash
git clone https://github.com/arkhadem/DX100.git
cd DX100
export GEM5_HOME=$(pwd)
```

### 2- Build Ramulator2

```bash
cd $GEM5_HOME/ext/ramulator2/ramulator2/
mkdir build; cd build;
cmake .. -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12
make -j
```

### 3- Build M5ops

Refer to [Gem5 documentations](https://www.gem5.org/documentation/general_docs/m5ops/) for more information:

```bash
cd $GEM5_HOME/util/m5
scons build/x86/out/m5 -j8
```

### 4- Build Gem5

```bash
cd $GEM5_HOME
bash scripts/make.sh
bash scripts/make_fast.sh
```

### 5- Build Benchmarks

```bash
cd $GEM5_HOME/benchmarks
bash build.sh
```

## Run Artifact

```bash
cd $GEM5_HOME
python3 
```