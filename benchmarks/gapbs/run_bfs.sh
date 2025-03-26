OMP_PROC_BIND=true OMP_NUM_THREADS=4 ./bfs -f ./serialized_graph_$1.sg -l -n 1 -v
OMP_PROC_BIND=true OMP_NUM_THREADS=4 ./bfs_maa -f ./serialized_graph_$1.sg -l -n 1 -v
