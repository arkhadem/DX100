### Build CG with different modes
```bash
bash make.sh [CLASS] [FUNC|GEM5] [LOAD|DUMP|NORMAL]
```
- CLASS: S, W, A, B, C, D, E
- FUNC: function-level simulation, GEM5: gem5-level simulation
- LOAD: load the key buffer from a dumped header file, DUMP: dump the key buffer to a header file, NORMAL: normal simulation

Example:
```bash
bash make.sh A FUNC DUMP
```
This would generate CG executables with class A, function-level simulation, and dump the key buffer to a header file.

### RUN CG with different modes
```bash
./cg_cpp.$CLASS.$TILE_SIZE [MAA|BASE]
```
- MAA: run the MAA version, BASE: run the baseline version, CMP: compare the results of MAA and BASE

Example:
```bash
./cg_cpp.A.1024 MAA
```
This would run the MAA version of CG with class A and tile size 1024, and verify the results.

### Try it out!
```bash
# step1: build CG with DUMP mode
bash make.sh A FUNC DUMP
# step2: run CG to get the dumped header file
./cg_cpp.A.1024 BASE
# step3: build CG with LOAD mode
bash make.sh A FUNC LOAD
# step4: run CG with MAA version
./cg_cpp.A.1024 MAA
```

