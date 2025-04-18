make clean
make FUNC DUMP=1
OMP_NUM_THREADS=4 ./is_base BASE &
OMP_NUM_THREADS=8 ./is_base_8C BASE &
wait
make clean
make $1 -j 1
