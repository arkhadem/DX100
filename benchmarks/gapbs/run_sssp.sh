OMP_PROC_BIND=true OMP_NUM_THREADS=4 ./sssp -f ./serialized_graph_$1.wsg -n 1 -v
OMP_PROC_BIND=true OMP_NUM_THREADS=4 ./sssp_maa -f ./serialized_graph_$1.wsg -n 1 -v

