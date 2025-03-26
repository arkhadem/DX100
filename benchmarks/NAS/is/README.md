### Build IS with different modes
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
This would generate IS executables with class A, function-level simulation, and dump the key buffer to a header file.

### RUN IS with different modes
```bash
./is.$CLASS.$TILE_SIZE.$VERIFY [MAA|BASE|CMP]
```
- MAA: run the MAA version, BASE: run the baseline version, CMP: compare the results of MAA and BASE

Example:
```bash
./is.A.1024.VERIFY MAA
```
This would run the MAA version of IS with class A and tile size 1024, and verify the results.

### Try it out!
```bash
# step1: build IS with DUMP mode
bash make.sh A FUNC DUMP
# step2: run IS to get the dumped header file
./is.A.1024.NOVERIFY BASE
# step3: build IS with LOAD mode
bash make.sh A FUNC LOAD
# step4: run IS with MAA version
./is.A.1024.VERIFY MAA
```

