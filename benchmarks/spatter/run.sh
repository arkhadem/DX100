echo "Running Spatter MAABenchmarks MAA"
OMP_PROC_BIND=false OMP_NUM_THREADS=4 ./build/spatter_maa -f tests/test-data/flag/all.json
echo "Running Spatter MAABenchmarks Base"
OMP_PROC_BIND=false OMP_NUM_THREADS=4 ./build/spatter_base -f tests/test-data/flag/all.json