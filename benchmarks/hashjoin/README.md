
EFFICIENT MAIN-MEMORY HASH JOINS ON MULTI-CORE CPUS:
	  TUNING TO THE UNDERLYING HARDWARE

  [ You can obtain an HTML version of this file by running doxygen
    with the `Doxyfile' file in `doc' directory.  The documentation
    will then be generated in doc/html/index.html. ]

### Instructions
```bash
# build
bash compile_x86.sh (FUNC|GEM5)
# run
time ./src/bin/x86/hj2-no -a PRH -n 4
```



   
A. Introduction

This package provides implementations of the main-memory hash join algorithms
described and studied in our ICDE 2013 paper. Namely, the implemented
algorithms are the following with the abbreviated names:

 - NPO:    No Partitioning Join Optimized (Hardware-oblivious algo. in paper)
 - PRO:    Parallel Radix Join Optimized (Hardware-conscious algo. in paper)
 - PRH:    Parallel Radix Join Histogram-based
 - PRHO:   Parallel Radix Join Histogram-based Optimized
 - RJ:     Radix Join (single-threaded)
 - NPO_st: No Partitioning Join Optimized (single-threaded)


B. Compilation

The package includes implementations of the algorithms and also the driver
code to run and repeat the experimental studies described in the paper.

The code has been written using standard GNU tools and uses autotools
for configuration. Thus, compilation should be as simple as:

       $ ./configure
       $ make

Besides the usual ./configure options, compilation can be customized with the
following options:

   --enable-debug         enable debug messages on commandline  [default=no]
   --enable-key8B         use 8B keys and values making tuples 16B  [default=no]
   --enable-perfcounters  enable performance monitoring with Intel PCM  [no]
   --enable-paddedbucket  enable padding of buckets to cache line size in NPO [no]
   --enable-timing        enable execution timing  [default=yes]
   --enable-syncstats     enable synchronization timing stats  [default=no]
   --enable-skewhandling  enable fine-granular task decomposition based skew
                          handling  in radix [default=no]

Additionally, the code can be configured to enable further optimizations
discussed in the Technical Report version of the paper:

   --enable-prefetch-npj   enable prefetching in no partitioning join [default=no]
   --enable-swwc-part      enable software write-combining optimization in 
                           partitioning? (Experimental, not tested extensively) [default=no]

Our code makes use of the Intel Performance Counter Monitor tool which was
slightly modified to be integrated in to our implementation. The original
code can be downloaded from:

http://software.intel.com/en-us/articles/intel-performance-counter-monitor/

We are providing the copy that we used for our experimental study under
<b>`lib/intel-pcm-1.7`</b> directory which comes with its own Makefile. Its
build process is actually separate from the autotools build process but with
the <tt>--enable-perfcounters</tt> option, make command from the top level
directory also builds the shared library <b>`libperf.so'</b> that we link to
our code. After compiling with --enable-perfcounters, in order to run the
executable add `lib/intel-pcm-1.7/lib' to your
<tt>LD_LIBRARY_PATH</tt>. In addition, the code must be run with
root privileges to acces model specific registers, probably after
issuing the following command: `modprobe msr`. Also note that with
--enable-perfcounters the code is compiled with g++ since Intel
code is written in C++. 

We have successfully compiled and run our code on different Linux
variants; the experiments in the paper were performed on Debian and Ubuntu
Linux systems.

C. Usage and Invocation

The mchashjoins binary understands the following command line
options: 

      Join algorithm selection, algorithms : RJ, PRO, PRH, PRHO, NPO, NPO_st
         -a --algo=<name>    Run the hash join algorithm named <name> [PRO]
 
      Other join configuration options, with default values in [] :
         -n --nthreads=<N>  Number of threads to use <N> [2]
         -r --r-size=<R>    Number of tuples in build relation R <R> [128000000]
         -s --s-size=<S>    Number of tuples in probe relation S <S> [128000000]
         -x --r-seed=<x>    Seed value for generating relation R <x> [12345]    
         -y --s-seed=<y>    Seed value for generating relation S <y> [54321]    
         -z --skew=<z>      Zipf skew parameter for probe relation S <z> [0.0]  
         --non-unique       Use non-unique (duplicated) keys in input relations 
         --full-range       Spread keys in relns. in full 32-bit integer range
         --basic-numa       Numa-localize relations to threads (Experimental)

      Performance profiling options, when compiled with --enable-perfcounters.
         -p --perfconf=<P>  Intel PCM config file with upto 4 counters [none]  
         -o --perfout=<O>   Output file to print performance counters [stdout]
 
      Basic user options
          -h --help         Show this message
          --verbose         Be more verbose -- show misc extra info 
          --version         Show version

The above command line options can be used to instantiate a certain
configuration to run various joins and print out the resulting
statistics. Following the same methodology of the related work, our joins
never materialize their results as this would be a common cost for all
joins. We only count the number of matching tuples and report this. In order
to materialize results, one needs to copy results to a result buffer in the
corresponding locations of the source code.

D. Configuration Parameters

D.1. Logical to Pyhsical CPU Mapping

If running on a machine with multiple CPU sockets and/or SMT feature enabled,
then it is necessary to identify correct mappings of CPUs on which threads
will execute. For instance one of our experiment machines, Intel Xeon L5520
had 2 sockets and each socket had 4 cores and 8 threads. In order to only
utilize the first socket, we had to use the following configuration for
mapping threads 1 to 8 to correct CPUs:

cpu-mapping.txt
8 0 1 2 3 8 9 10 11

This file is must be created in the executable directory and used by default 
if exists in the directory. It basically says that we will use 8 CPUs listed 
and threads spawned 1 to 8 will map to the given list in order. For instance 
thread 5 will run CPU 8. This file must be changed according to the system at
hand. If it is absent, threads will be assigned round-robin. This CPU mapping
utility is also integrated into the Wisconsin implementation (found in 
`wisconsin-src') and same settings are also valid there.

D.2. Performance Monitoring

For performance monitoring a config file can be provided on the command line
with --perfconf which specifies which hardware counters to monitor. For 
detailed list of hardware counters consult to "Intel 64 and IA-32 
Architectures Software Developer’s Manual" Appendix A. For an example 
configuration file used in the experiments, see <b>`pcm.cfg'</b> file. 
Lastly, an output file name with --perfout on commandline can be specified to
print out profiling results, otherwise it defaults to stdout.

D.3. System and Implementation Parameters

The join implementations need to know about the system at hand to a certain
degree. For instance #CACHE_LINE_SIZE is required by both of the
implementations. In case of no partitioning join, other implementation
parameters such as bucket size or whether to pre-allocate for overflowing
buckets are parametrized and can be modified in `npj_params.h'.

On the other hand, radix joins are more sensitive to system parameters and 
the optimal setting of parameters should be found from machine to machine to 
get the same results as presented in the paper. System parameters needed are
#CACHE_LINE_SIZE, #L1_CACHE_SIZE and
#L1_ASSOCIATIVITY. Other implementation parameters specific to radix
join are also important such as #NUM_RADIX_BITS
which determines number of created partitions and #NUM_PASSES which
determines number of partitioning passes. Our implementations support between
1 and 2 passes and they can be configured using these parameters to find the
ideal performance on a given machine.

E. Generating Data Sets of Our Experiments

Here we briefly describe how to generate data sets used in our experiments 
with the command line parameters above.

E.1. Workload B 

In this data set, the inner relation R and outer relation S have 128*10^6 
tuples each. The tuples are 8 bytes long, consisting of 4-byte (or 32-bit) 
integers and a 4-byte payload. As for the data distribution, if not 
explicitly specified, we use relations with randomly shuffled unique keys 
ranging from 1 to 128*10^6. To generate this data set, append the following 
parameters to the executable
mchashjoins:

      $ ./mchashjoins [other options] --r-size=128000000 --s-size=128000000 

note: Configure must have run without --enable-key8B.

E.2. Workload A

This data set reflects the case where the join is performed between the 
primary key of the inner relation R and the foreign key of the outer relation
S. The size of R is fixed at 16*2^20 and size of S is fixed at 256*2^20. 
The ratio of the inner relation to the outer relation is 1:16. In this data 
set, tuples are represented as (key, payload) pairs of 8 bytes each, summing 
up to 16 bytes per tuple. To generate this data set do the following:

     $ ./configure --enable-key8B
     $ make
     $ ./mchashjoins [other options] --r-size=16777216 --s-size=268435456 

E.3. Introducing Skew in Data Sets

Skew can be introduced to the relation S as in our experiments by appending 
the following parameter to the command line, which is basically a Zipf 
distribution skewness parameter:

     $ ./mchashjoins [other options] --skew=1.05

F. Wisconsin Implementation

A slightly modified version of the original implementation provided by 
Blanas et al. from University of Wisconsin is provided under `wisconsin-src'
directory. The changes we made are documented in the header of the README 
file. These implementations provide the algorithms mentioned as 
`non-optimized no partitioning join' and `non-optimized radix join' in our 
paper. The original source code can be downloaded from 
http://pages.cs.wisc.edu/~sblanas/files/multijoin.tar.bz2 .

Author: Cagri Balkesen <cagri.balkesen@inf.ethz.ch>

(c) 2012, ETH Zurich, Systems Group
