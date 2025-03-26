OMP_PROC_BIND=true OMP_NUM_THREADS=4 ./pr_maa -f ./serialized_graph_$1.sg -n 1 -v
OMP_PROC_BIND=true OMP_NUM_THREADS=4 ./pr -f ./serialized_graph_$1.sg -n 1 -v