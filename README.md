## Requirements

### Ramulator2:

- g++-12
- clang++-15

### Gem5:

https://www.gem5.org/documentation/general_docs/building

- git : gem5 uses git for version control.
- gcc: gcc is used to compiled gem5. Version >=10 must be used. We support up to gcc Version 13.
- Clang: Clang can also be used. At present, we support Clang 7 to Clang 16 (inclusive).
- SCons : gem5 uses SCons as its build environment. SCons 3.0 or greater must be used.
- Python 3.6+ : gem5 relies on Python development libraries. gem5 can be compiled and run in environments using Python 3.6+.
- protobuf 2.1+ (Optional): The protobuf library is used for trace generation and playback.
- Boost (Optional): The Boost library is a set of general purpose C++ libraries. It is a necessary dependency if you wish to use the SystemC implementation.

**Setup on Ubuntu 24.04 (gem5 >= v24.0)**

If compiling gem5 on Ubuntu 24.04, or related Linux distributions, you may install all these dependencies using APT:

```bash
sudo apt install build-essential scons python3-dev git pre-commit zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    libboost-all-dev  libhdf5-serial-dev python3-pydot python3-venv python3-tk mypy \
    m4 libcapstone-dev libpng-dev libelf-dev pkg-config wget cmake doxygen
```

**Setup on Ubuntu 22.04 (gem5 >= v21.1)**

If compiling gem5 on Ubuntu 22.04, or related Linux distributions, you may install all these dependencies using APT:

```bash
sudo apt install build-essential git m4 scons zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    python3-dev libboost-all-dev pkg-config python3-tk
```

**Setup on Ubuntu 20.04 (gem5 >= v21.0)**

If compiling gem5 on Ubuntu 20.04, or related Linux distributions, you may install all these dependencies using APT:

```bash
sudo apt install build-essential git m4 scons zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    python3-dev python-is-python3 libboost-all-dev pkg-config gcc-10 g++-10 \
    python3-tk
```

## Build

### Ramulator2

cd DX100/ext/ramulator2/ramulator2/
mkdir build & cd build
cmake .. -DCMAKE_C_COMPILER=gcc-12 -DCMAKE_CXX_COMPILER=g++-12
make -j

### M5ops

https://www.gem5.org/documentation/general_docs/m5ops/
cd DX100/util/m5
scons build/x86/out/m5 -j8

### Gem5

cd DX100
bash scripts/make.sh # press ENTER, y, ENTER
bash scripts/make_fast.sh # press ENTER, y, ENTER

### Benchmarks