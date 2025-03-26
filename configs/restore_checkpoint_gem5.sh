# 3200 * 2 * 8 / 16
export OMP_PROC_BIND=false; export OMP_NUM_THREADS=4;
./build/X86/gem5.opt  ./configs/deprecated/example/se.py --cpu-type O3CPU -n 4 --mem-size '4GB' --sys-clock '3GHz'\
  --caches --l1d_size=64kB --l1d_assoc=4 --l1i_size=32kB --l1i_assoc=4 --l2cache --l2_size=1MB --l2_assoc=16 --cacheline_size=64 \
  --mem-type DDR3_1600_8x8 --mem-channels 2 \
  --cmd $1 --options $2 -r 1

python3 ./util/extract_stats.py  m5out/stats.txt m5out/stats.json