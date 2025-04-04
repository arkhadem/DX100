# Artifcat of the DX100 Paper, ISCA 2025

This repository provides the gem5 simulator, benchmarks, and automation scripts required for the artifact evaluation of the "DX100: A Programmable Data Access Accelerator for Indirection" paper published in ISCA 2025.

## Directory Structure

The repo structure is as follows:
  - [src/mem/MAA](/src/mem/MAA/): gem5 source code of DX100.
  - [configs/common](/configs/common/): gem5 Python scripts for DX100 configuration.
  - [benchmarks](/benchmarks/): DX100 memory-mapped and functional simulator APIs, as well as the evaluated kernels from NAS, GAP, Hash-Join, UME, and Spatter benchmark suites.
  - [scripts](/scripts/): Automation scripts required for running the simulations, parsing the results, and plotting the charts.
  - [results](/results/): After running the automation scripts, raw results and charts are stored in this directory.

## Requirements

This artifact has several dependencies. To simplify the evaluation process, we provide a Docker image with all required dependencies pre-installed. We recommend using the Docker container to minimize setup time and ensure a consistent environment.

```bash
docker pull arkhadem95/dx100:latest
docker run -it --name dx100_container -v /path/to/data/dir:/data -w /home/ubuntu arkhadem95/dx100 bash
```

*Note:* `/path/to/data/dir` should point to your data directory, which must have at least 20GB of available disk space.

If you choose to run the artifact locally instead of using Docker, please ensure the following dependencies are installed:

### Ramulator2

Ramulator2 is tested with `g++-12` and `clang++-15` compilers.

### gem5

gem5 requires the following dependencies (from [gem5 documentations](https://www.gem5.org/documentation/general_docs/building)):

- SCons: gem5 uses SCons as its build environment. SCons 3.0 or greater must be used.
- Python 3.6+: gem5 relies on Python development libraries. gem5 can be compiled and run in environments using Python 3.6+.
- protobuf 2.1+ (Optional): The protobuf library is used for trace generation and playback.
- Boost (Optional): The Boost library is a set of general purpose C++ libraries. It is a necessary dependency if you wish to use the SystemC implementation.

**Setup gem5 on Ubuntu 24.04 (gem5 >= v24.0)**

If compiling gem5 on Ubuntu 24.04, or related Linux distributions, you may install all these dependencies using APT:

```bash
sudo apt install build-essential scons python3-dev git pre-commit zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    libboost-all-dev  libhdf5-serial-dev python3-pydot python3-venv python3-tk mypy \
    m4 libcapstone-dev libpng-dev libelf-dev pkg-config wget cmake doxygen
```

**Setup gem5 on Ubuntu 22.04 (gem5 >= v21.1)**

If compiling gem5 on Ubuntu 22.04, or related Linux distributions, you may install all these dependencies using APT:

```bash
sudo apt install build-essential git m4 scons zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    python3-dev libboost-all-dev pkg-config python3-tk
```

**Setup gem5 on Ubuntu 20.04 (gem5 >= v21.0)**

If compiling gem5 on Ubuntu 20.04, or related Linux distributions, you may install all these dependencies using APT:

```bash
sudo apt install build-essential git m4 scons zlib1g zlib1g-dev \
    libprotobuf-dev protobuf-compiler libprotoc-dev libgoogle-perftools-dev \
    python3-dev python-is-python3 libboost-all-dev pkg-config gcc-10 g++-10 \
    python3-tk
```

### Automation Scripts

These scripts require Python 3.6+ and `matplotlib` package.

### Benchmarks

CMake 3.25+ is required for the Spatter benchmark.

## Build

Building the artifact requires approximately 6GB of disk space and takes around 35 minutes to complete.

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

Refer to [gem5 documentations](https://www.gem5.org/documentation/general_docs/m5ops/) for more information:

```bash
cd $GEM5_HOME/util/m5
scons build/x86/out/m5 -j8
```

### 4- Build gem5

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

Before running the artifact, you can remove the current results.

```bash
rm -r $GEM5_HOME/results
```

Use the following script to run the simulations, parse the results, and plot the charts.

```bash
cd $GEM5_HOME
python3 scripts/benchmark.py -j NUM_THREADS -a all -dir /path/to/data/dir
```

- `NUM_THREADS` specifies the number of parallel simulations. Each simulation requires approximately *35GB of memory*, so set this value based on your system's available DRAM.

- `/path/to/data/dir` is the directory where gem5 simulation results will be saved. Ensure it has at least *20GB of free disk space*.  
  **Note:** If you are using the Docker container, set this path to `/data`.

- `all` runs the full pipeline, including simulation, result parsing, and chart plotting.  
  Alternatively, you can specify `simulate`, `parse`, or `plot` to execute each step individually.

### How Long Simulation Takes?

Using a single thread, the complete end-to-end execution takes approximately *84 hours*. By setting `NUM_THREADS` to `4`, the execution time is reduced to around *24 hours* that requires approximately *140GB of memory*.

### How to Ensure Each Step Runs Correctly?

- **After simulation**:  
  You can verify the simulation step by checking the logs located in the `/path/to/data/dir/results` directory.

- **After parsing**:  
  The raw results will be available in the `results/results.csv` file.

- **After plotting**:  
  The following charts will be generated in the `results` directory:
  - `speedup.png`
  - `bandwidth.png`
  - `Row_Buffer_Hitrate.png`
  - `Request_Buffer_Occupancy.png`
  - `instruction_reduction.png`
  - `MPKI.png`

## Citation

If you use *DX100*, please cite this paper:

> Alireza Khadem, Kamalavasan Kamalakkannan, Zhenyan Zhu, Akash Poptani, Yufeng Gu, Jered Benjamin Dominguez-Trujillo, Nishil Talati, Daichi Fujiki, Scott Mahlke, Galen Shipman, and Reetuparna Das
> *DX100: Programmable Data Access Accelerator for Indirection*,
> In Proceedings of the 52th Annual International Symposium on Computer Architecture (ISCA'25)

```
@inproceedings{dx100,
  title={DX100: Programmable Data Access Accelerator for Indirection},
  author={Khadem, Alireza and Kamalakkannan, Kamalavasan and Zhu, Zhenyan and Poptani, Akash and Gu, Yufeng and Dominguez-Trujillo, Jered Benjamin and Talati, Nishil and Fujiki, Daichi and Mahlke, Scott and Shipman, Galen and Das, Reetuparna},
  booktitle={Proceedings of the 52th Annual International Symposium on Computer Architecture}, 
  year={2025}
}
```

## Issues and bug reporting

We appreciate any feedback and suggestions from the community.
Feel free to raise an issue or submit a pull request on Github.
For assistance in using DX100, please contact: Alireza Khadem (arkhadem@umich.edu)

## Licensing

This repository is available under a [MIT license](/LICENSE).

## Acknowledgement

This work was supported in part by the NSF-1908601 award, JST PRESTO Grant Number JPMJPR22P7, and research funds from Los Alamos National Lab.