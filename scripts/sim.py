import os
from threading import Thread, Lock

### get the parent directory where this script is located
GEM5_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = None
CPT_DIR = None
CPT_TS_DIR = None
CPT_SC_DIR = None
RSLT_DIR = None
RSLT_TS_DIR = None
RSLT_SC_DIR = None
BNCH_DIR = None

all_modes = ["BASE", "MAA"]
all_modes_DMP = ["DMP", "MAA"]

all_tile_sizes = [1024, 2048, 4096, 8192, 16384, 32768]
all_tile_sizes_str = ["1K", "2K", "4K", "8K", "16K", "32K"]

all_scaling_cores = [8]
all_scaling_maas = [[1, 2]]

all_NAS_kernels = ["is", "cg"]
all_GAPB_kernels = ["bfs", "pr", "bc"] # , "sssp"]
all_SPATTER_kernels = ["xrage"]
all_HASHJOIN_kernels = ["PRH", "PRO"]
all_UME_kernels = ["gradzatp", "gradzatz", "gradzatz_invert", "gradzatp_invert"]

tasks = []
lock = Lock()

class Task:
    def __init__(self, command, dependency = None):
        self.command = command
        self.started = False
        self.finished = False
        self.dependency = dependency
    
    def __eq__(self, value):
        if value == None or self == None:
            return False
        return self.command == value.command

def workerthread(my_tid):
    global lock
    global tasks
    task = None
    while True:
        selected_task = None
        selected_task_id = None
        with lock:
            for task_id, task in enumerate(tasks):
                if task.started == False and (task.dependency == None or tasks[task.dependency].finished == True):
                    selected_task = task
                    selected_task_id = task_id
                    tasks[task_id].started = True
                    break
        if selected_task == None:
            print("T[{}]: tasks finished, bye!".format(my_tid))
            break
        else:
            print("T[{}]: executing a new task: {}".format(my_tid, selected_task.command))
            os.system(selected_task.command)
            with lock:
                tasks[selected_task_id].finished = True

cpu_type = "X86O3CPU"
mem_size_per_core = 4
sys_clock = "3.2GHz"
l1d_size = "32kB"
l1d_assoc = 8
l1d_hwp_type = "StridePrefetcher"
l1d_mshrs = 16
l1d_write_buffers = 8
l1i_size = "32kB"
l1i_assoc = 8
l1i_hwp_type = "StridePrefetcher"
l1i_mshrs = 16
l1i_write_buffers = 8
l2_size = "256kB"
l2_assoc = 4
l2_mshrs = 32
l2_write_buffers = 16
l3_size_per_core = 2
l3_size_extramaa_per_core = 0.5
l3_assoc_per_core = 4
l3_assoc_extramaa_per_core = 1
l3_mshrs_per_core = 64
l3_write_buffers_per_core = 32
mem_type = "Ramulator2"
ramulator_config = f"{GEM5_DIR}/ext/ramulator2/ramulator2/example_gem5_config.yaml"
mem_channels_per_core = 0.5
program_interval = 1000
debug_type = "MAATrace"

def add_command_checkpoint(directory, command, options, num_cores = 4, force_rerun = False):
    if force_rerun == False and os.path.isdir(directory):
        contents = os.listdir(directory)
        for content in contents:
            if content[:3] == "cpt":
                print(f"Checkpoint {directory} already exists!")
                return None
    if os.path.exists(command) == False:
        print(f"ERROR: Command {command} does not exist!")
        exit(-1)
    COMMAND = f"rm -r {directory} 2>&1 > /dev/null; sleep 1; mkdir -p {directory}; sleep 2; "
    COMMAND += f"OMP_PROC_BIND=false OMP_NUM_THREADS={num_cores} build/X86/gem5.fast "
    COMMAND += f"--outdir={directory} "
    COMMAND += f"{GEM5_DIR}/configs/deprecated/example/se.py "
    COMMAND += f"--cpu-type AtomicSimpleCPU -n {num_cores} --mem-size \"{mem_size_per_core * num_cores}GB\" "
    COMMAND += f"--cmd {command} --options \"{options}\" "
    COMMAND += f"2>&1 "
    COMMAND += "| awk '{ print strftime(), $0; fflush() }' "
    COMMAND += f"| tee {directory}/logs_cpt.txt "
    task = Task(command=COMMAND)
    if task in tasks:
        print(f"Task {COMMAND} already exists!")
        exit(1)
    tasks.append(task)
    return len(tasks) - 1

def add_command_run_MAA(directory,
                        checkpoint,
                        checkpoint_id,
                        command,
                        options,
                        mode,
                        tile_size = 16384,
                        reconfigurable_RT = False,
                        maa_warmer = False,
                        num_cores = 4,
                        num_maas = 1,
                        do_prefetch = True,
                        do_reorder = True,
                        force_cache = False,
                        force_rerun = False):
    if force_rerun == False and os.path.isdir(directory):
        contents = os.listdir(directory)
        for content in contents:
            if "stats.txt" in content:
                with open(f"{directory}/stats.txt", "r") as f:
                    lines = f.readlines()
                    if len(lines) > 50:
                        print(f"Experiment {directory} already done!")
                        return None
    if os.path.exists(command) == False:
        print(f"ERROR: Command {command} does not exist!")
        exit(-1)
    
    have_maa = False
    l2_hwp_type = "StridePrefetcher"
    if mode == "DMP":
        l2_hwp_type = "DiffMatchingPrefetcher"
        l3_size = f"{int((l3_size_per_core + l3_size_extramaa_per_core) * num_cores)}MB"
        l3_assoc = int((l3_assoc_per_core + l3_assoc_extramaa_per_core) * num_cores)
    elif mode == "MAA":
        have_maa = True
        l3_size = f"{l3_size_per_core * num_cores}MB"
        l3_assoc = l3_assoc_per_core * num_cores
    elif mode == "BASE":
        l3_size = f"{int((l3_size_per_core + l3_size_extramaa_per_core) * num_cores)}MB"
        l3_assoc = int((l3_assoc_per_core + l3_assoc_extramaa_per_core) * num_cores)
    else:
        raise ValueError("Unknown mode")
    mem_channels = int(mem_channels_per_core * float(num_cores))
    ncbus_width = 32 if mem_channels == 2 else 64 if mem_channels == 4 else 128 if mem_channels == 8 else -1
    assert ncbus_width != -1

    COMMAND = f"OMP_PROC_BIND=false OMP_NUM_THREADS={num_cores} {GEM5_DIR}/build/X86/gem5.opt "
    # if debug_type != None: # and mode == "MAA":
    COMMAND += f"--debug-flags={debug_type} "
    COMMAND += f"--outdir={directory} "
    COMMAND += f"{GEM5_DIR}/configs/deprecated/example/se.py "
    COMMAND += f"--cpu-type {cpu_type} "
    COMMAND += f"-n {num_cores} "
    COMMAND += f"--mem-size '{mem_size_per_core * num_cores}GB' "
    COMMAND += f"--sys-clock '{sys_clock}' "
    COMMAND += f"--cpu-clock '{sys_clock}' "
    COMMAND += f"--caches "
    COMMAND += f"--l1d_size={l1d_size} "
    COMMAND += f"--l1d_assoc={l1d_assoc} "
    if do_prefetch:
        COMMAND += f"--l1d-hwp-type={l1d_hwp_type} "
    COMMAND += f"--l1d_mshrs={l1d_mshrs} "
    COMMAND += f"--l1d_write_buffers={l1d_write_buffers} "
    COMMAND += f"--l1i_size={l1i_size} "
    COMMAND += f"--l1i_assoc={l1i_assoc} "
    if do_prefetch:
        COMMAND += f"--l1i-hwp-type={l1i_hwp_type} "
    COMMAND += f"--l1i_mshrs={l1i_mshrs} "
    COMMAND += f"--l1i_write_buffers={l1i_write_buffers} "
    COMMAND += f"--l2cache "
    COMMAND += f"--l2_size={l2_size} "
    COMMAND += f"--l2_assoc={l2_assoc} "
    if do_prefetch:
        COMMAND += f"--l2-hwp-type={l2_hwp_type} "
        if l2_hwp_type == "DiffMatchingPrefetcher":
            COMMAND += f"--dmp-notify l1 "
    COMMAND += f"--l2_mshrs={l2_mshrs} "
    COMMAND += f"--l2_write_buffers={l2_write_buffers} "
    COMMAND += f"--l3cache "
    COMMAND += f"--l3_size={l3_size} "
    COMMAND += f"--l3_assoc={l3_assoc} "
    COMMAND += f"--l3_mshrs={l3_mshrs_per_core * num_cores} "
    COMMAND += f"--l3_write_buffers={l3_write_buffers_per_core * num_cores} "
    COMMAND += f"--l3_ports {num_cores} "
    COMMAND += "--cacheline_size=64 "
    COMMAND += f"--mem-type {mem_type} "
    COMMAND += f"--ramulator-config {ramulator_config} "
    COMMAND += f"--mem-channels {mem_channels} "
    COMMAND += f"--maa_ncbus_width {ncbus_width} "
    if have_maa or maa_warmer:
        COMMAND += "--maa "
        COMMAND += f"--maa_num_maas {num_maas} "
        COMMAND += f"--maa_num_tile_elements {tile_size} "
        COMMAND += "--maa_l2_uncacheable "
        COMMAND += "--maa_l3_uncacheable "
        COMMAND += f"--maa_num_initial_row_table_slices {int(mem_channels * 16)} "
        if do_reorder == False:
            COMMAND += "--maa_no_reorder "
        if force_cache == True:
            COMMAND += "--maa_force_cache_access "
    COMMAND += f"--cmd {command} "
    COMMAND += f"--options \"{options}\" "
    if checkpoint != None:
        COMMAND += f"-r 1 "
    COMMAND += f"--prog-interval={program_interval} "
    COMMAND += f"2>&1 "
    COMMAND += "| awk '{ print strftime(), $0; fflush() }' "
    COMMAND += f"| tee {directory}/logs_run.txt "
    command = None
    if checkpoint != None:
        command=f"rm -r {directory} 2>&1 > /dev/null; sleep 1; mkdir -p {directory} 2>&1 > /dev/null; sleep 1; rm -r {checkpoint}/cpt.%d 2>&1 > /dev/null; sleep 1; cp -r {checkpoint}/cpt.* {directory}/; sleep 1; {COMMAND}; sleep 1;"
    else:
        command=f"rm -r {directory} 2>&1 > /dev/null; sleep 1; mkdir -p {directory} 2>&1 > /dev/null; sleep 1; {COMMAND}; sleep 1;"
    task = Task(command=command, dependency=checkpoint_id)
    if task in tasks:
        print(f"Task {command} already exists!")
        exit(1)
    tasks.append(task)

def run_tasks(parallelism):
    print (f"There exists {len(tasks)} commands to run:")
    for task_id in range(len(tasks)):
        print (f"Task {task_id}: {tasks[task_id].command}")
    if parallelism != 0:
        threads = []
        for i in range(parallelism):
            threads.append(Thread(target=workerthread, args=(i,)))
        
        for i in range(parallelism):
            threads[i].start()
        
        for i in range(parallelism):
            print("T[M]: Waiting for T[{}] to join!".format(i))
            threads[i].join()

def run_simulation(parallelism, force_rerun):
    print("Starting BASE/MAA simulation with the following configurations:")
    print(f"\t\tParallelism: {parallelism}")
    print(f"\t\tForce rerun: {force_rerun}")
    print(f"\t\tGEM5 directory: {GEM5_DIR}")
    print(f"\t\tData directory: {DATA_DIR}")
    print(f"\t\tCheckpoint directory: {CPT_DIR}")
    print(f"\t\tResult directory: {RSLT_DIR}")
    print(f"\t\tBenchmark directory: {BNCH_DIR}")

    ########################################## NAS ##########################################
    for kernel in all_NAS_kernels:
        for mode in all_modes:
            file_name = f"{kernel}_maa" if mode == "MAA" else f"{kernel}_base"
            size = "c" if kernel == "cg" else "b"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                                    command=f"{BNCH_DIR}/NAS/{kernel}/{file_name}",
                                                    options=mode,
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/NAS/{kernel}/{file_name}",
                                options=mode,
                                mode=mode,
                                force_rerun=force_rerun)

    ########################################## SPATTER ##########################################
    for kernel in all_SPATTER_kernels:
        for mode in all_modes:
            file_name = "spatter_maa" if mode == "MAA" else "spatter_base"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/spatter/{kernel}/{mode}",
                                                    command=f"{BNCH_DIR}/spatter/build/{file_name}",
                                                    options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/spatter/{kernel}/{mode}",
                                checkpoint=f"{CPT_DIR}/spatter/{kernel}/{mode}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/spatter/build/{file_name}",
                                options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                mode=mode,
                                force_rerun=force_rerun)

    ######################################### HASHJOIN ##########################################
    size = 2000000
    size_per_core = 500000
    size_str = "2M"
    for kernel in all_HASHJOIN_kernels:
        for mode in all_modes:
            file_name = "hj_maa" if mode == "MAA" else "hj_base"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                                    command=f"{BNCH_DIR}/hashjoin/src/bin/x86/{file_name}",
                                                    options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/hashjoin/src/bin/x86/{file_name}",
                                options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                mode=mode,
                                force_rerun=force_rerun)
            
    ########################################## UME ##########################################
    size = 2000000
    size_per_core = 500000
    size_str = "2M"
    for kernel in all_UME_kernels:
        for mode in all_modes:
            file_name = f"{kernel}_maa" if mode == "MAA" else f"{kernel}_base"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                                    command=f"{BNCH_DIR}/UME/{file_name}",
                                                    options=f"{size}",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/UME/{file_name}",
                                options=f"{size}",
                                mode=mode,
                                force_rerun=force_rerun)

    ########################################## GAPB ##########################################
    for kernel in all_GAPB_kernels:
        for mode in all_modes:
            size = 20 if kernel == "bc" else 21 if kernel == "sssp" else 22
            file_name = f"{kernel}_maa" if mode == "MAA" else kernel
            graph_ext = "wsg" if kernel == "sssp" else "sg"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                                    command=f"{BNCH_DIR}/gapbs/{file_name}",
                                                    options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/gapbs/{file_name}",
                                options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                mode=mode,
                                force_rerun=force_rerun)

    ########################################## RUN SELECTED EXPERIMENTS ##########################################
    run_tasks(parallelism)

def run_simulation_DMP(parallelism, force_rerun):
    print("Starting DMP/MAA simulation with the following configurations:")
    print(f"\t\tParallelism: {parallelism}")
    print(f"\t\tForce rerun: {force_rerun}")
    print(f"\t\tGEM5 directory: {GEM5_DIR}")
    print(f"\t\tData directory: {DATA_DIR}")
    print(f"\t\tCheckpoint directory: {CPT_DIR}")
    print(f"\t\tResult directory: {RSLT_DIR}")
    print(f"\t\tBenchmark directory: {BNCH_DIR}")

    ########################################## NAS ##########################################
    for kernel in all_NAS_kernels:
        for mode in all_modes_DMP:
            file_name = f"{kernel}_maa" if mode == "MAA" else f"{kernel}_base"
            options = "BASE" if mode == "DMP" else mode
            size = "c" if kernel == "cg" else "b"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                                    command=f"{BNCH_DIR}/NAS/{kernel}/{file_name}",
                                                    options=options,
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/NAS/{kernel}/{file_name}",
                                options=options,
                                mode=mode,
                                force_rerun=force_rerun)

    ########################################## SPATTER ##########################################
    for kernel in all_SPATTER_kernels:
        for mode in all_modes_DMP:
            file_name = "spatter_maa" if mode == "MAA" else "spatter_base"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/spatter/{kernel}/{mode}",
                                                    command=f"{BNCH_DIR}/spatter/build/{file_name}",
                                                    options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/spatter/{kernel}/{mode}",
                                checkpoint=f"{CPT_DIR}/spatter/{kernel}/{mode}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/spatter/build/{file_name}",
                                options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                mode=mode,
                                force_rerun=force_rerun)

    ######################################### HASHJOIN ##########################################
    size = 2000000
    size_per_core = 500000
    size_str = "2M"
    for kernel in all_HASHJOIN_kernels:
        for mode in all_modes_DMP:
            file_name = "hj_maa" if mode == "MAA" else "hj_base"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                                    command=f"{BNCH_DIR}/hashjoin/src/bin/x86/{file_name}",
                                                    options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/hashjoin/src/bin/x86/{file_name}",
                                options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                mode=mode,
                                force_rerun=force_rerun)
            
    ########################################## UME ##########################################
    size = 2000000
    size_per_core = 500000
    size_str = "2M"
    for kernel in all_UME_kernels:
        for mode in all_modes_DMP:
            file_name = f"{kernel}_maa" if mode == "MAA" else f"{kernel}_base"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                                    command=f"{BNCH_DIR}/UME/{file_name}",
                                                    options=f"{size}",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/UME/{file_name}",
                                options=f"{size}",
                                mode=mode,
                                force_rerun=force_rerun)

    ########################################## GAPB ##########################################
    for kernel in all_GAPB_kernels:
        for mode in all_modes_DMP:
            size = 20 if kernel == "bc" else 21 if kernel == "sssp" else 22
            file_name = f"{kernel}_maa" if mode == "MAA" else kernel
            graph_ext = "wsg" if kernel == "sssp" else "sg"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                                    command=f"{BNCH_DIR}/gapbs/{file_name}",
                                                    options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/gapbs/{file_name}",
                                options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                mode=mode,
                                force_rerun=force_rerun)

    ########################################## RUN SELECTED EXPERIMENTS ##########################################
    run_tasks(parallelism)

def run_simulation_TS(parallelism, force_rerun):
    print("Starting Tile Size simulation with the following configurations:")
    print(f"\t\tParallelism: {parallelism}")
    print(f"\t\tForce rerun: {force_rerun}")
    print(f"\t\tGEM5 directory: {GEM5_DIR}")
    print(f"\t\tData directory: {DATA_DIR}")
    print(f"\t\tCheckpoint directory: {CPT_DIR}")
    print(f"\t\tCheckpoint TS directory: {CPT_TS_DIR}")
    print(f"\t\tResult directory: {RSLT_DIR}")
    print(f"\t\tResult TS directory: {RSLT_TS_DIR}")
    print(f"\t\tBenchmark directory: {BNCH_DIR}")

    ########################################## NAS ##########################################
    for kernel in all_NAS_kernels:
        size = "c" if kernel == "cg" else "b"
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/BASE/{size}",
                                                command=f"{BNCH_DIR}/NAS/{kernel}/{kernel}_base",
                                                options="BASE",
                                                force_rerun=force_rerun)
        add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/BASE/{size}",
                            checkpoint=f"{CPT_DIR}/{kernel}/BASE/{size}",
                            checkpoint_id = checkpoint_id,
                            command=f"{BNCH_DIR}/NAS/{kernel}/{kernel}_base",
                            options="BASE",
                            mode="BASE",
                            force_rerun=force_rerun)
        for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
            file_name = f"{kernel}_maa_{tile_size_str}"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_TS_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                                    command=f"{BNCH_DIR}/NAS/{kernel}/{file_name}",
                                                    options="MAA",
                                                    force_rerun=force_rerun) 
            add_command_run_MAA(directory=f"{RSLT_TS_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                checkpoint=f"{CPT_TS_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/NAS/{kernel}/{file_name}",
                                options="MAA",
                                mode="MAA",
                                tile_size=tile_size,
                                force_rerun=force_rerun)

    ########################################## SPATTER ##########################################
    for kernel in all_SPATTER_kernels:
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/spatter/{kernel}/BASE",
                                                command=f"{BNCH_DIR}/spatter/build/spatter_base",
                                                options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                                force_rerun=force_rerun)
        add_command_run_MAA(directory=f"{RSLT_DIR}/spatter/{kernel}/BASE",
                            checkpoint=f"{CPT_DIR}/spatter/{kernel}/BASE",
                            checkpoint_id = checkpoint_id,
                            command=f"{BNCH_DIR}/spatter/build/spatter_base",
                            options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                            mode="BASE",
                            force_rerun=force_rerun)
        for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
            file_name = f"spatter_maa_{tile_size_str}"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_TS_DIR}/spatter/{kernel}/MAA/{tile_size_str}",
                                                    command=f"{BNCH_DIR}/spatter/build/{file_name}",
                                                    options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_TS_DIR}/spatter/{kernel}/MAA/{tile_size_str}",
                                checkpoint=f"{CPT_TS_DIR}/spatter/{kernel}/MAA/{tile_size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/spatter/build/{file_name}",
                                options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                mode="MAA",
                                tile_size=tile_size,
                                force_rerun=force_rerun)

    ######################################### HASHJOIN ##########################################
    size = 2000000
    size_per_core = 500000
    size_str = "2M"
    for kernel in all_HASHJOIN_kernels:
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/BASE/{size_str}",
                                                command=f"{BNCH_DIR}/hashjoin/src/bin/x86/hj_base",
                                                options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                                force_rerun=force_rerun)
        add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/BASE/{size_str}",
                            checkpoint=f"{CPT_DIR}/{kernel}/BASE/{size_str}",
                            checkpoint_id = checkpoint_id,
                            command=f"{BNCH_DIR}/hashjoin/src/bin/x86/hj_base",
                            options=f"-a {kernel} -n 4 -r {size} -s {size}",
                            mode="BASE",
                            force_rerun=force_rerun)
        for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
            file_name = f"hj_maa_{tile_size_str}"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_TS_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                                    command=f"{BNCH_DIR}/hashjoin/src/bin/x86/{file_name}",
                                                    options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_TS_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                checkpoint=f"{CPT_TS_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/hashjoin/src/bin/x86/{file_name}",
                                options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                mode="MAA",
                                tile_size=tile_size,
                                force_rerun=force_rerun)
            
    ########################################## UME ##########################################
    size = 2000000
    size_per_core = 500000
    size_str = "2M"
    for kernel in all_UME_kernels:
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/BASE/{size_str}",
                                                command=f"{BNCH_DIR}/UME/{kernel}_bases",
                                                options=f"{size}",
                                                force_rerun=force_rerun)
        add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/BASE/{size_str}",
                            checkpoint=f"{CPT_DIR}/{kernel}/BASE/{size_str}",
                            checkpoint_id = checkpoint_id,
                            command=f"{BNCH_DIR}/UME/{kernel}_bases",
                            options=f"{size}",
                            mode="BASE",
                            force_rerun=force_rerun)
        for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
            file_name = f"{kernel}_maa_{tile_size_str}"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_TS_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                                command=f"{BNCH_DIR}/UME/{file_name}",
                                                options=f"{size}",
                                                force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_TS_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                checkpoint=f"{CPT_TS_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/UME/{file_name}",
                                options=f"{size}",
                                mode="MAA",
                                tile_size=tile_size,
                                force_rerun=force_rerun)

    ########################################## GAPB ##########################################
    for kernel in all_GAPB_kernels:
        size = 20 if kernel == "bc" else 21 if kernel == "sssp" else 22
        graph_ext = "wsg" if kernel == "sssp" else "sg"
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/BASE/{size}",
                                                command=f"{BNCH_DIR}/gapbs/{kernel}",
                                                options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                                force_rerun=force_rerun)
        add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/BASE/{size}",
                            checkpoint=f"{CPT_DIR}/{kernel}/BASE/{size}",
                            checkpoint_id = checkpoint_id,
                            command=f"{BNCH_DIR}/gapbs/{kernel}",
                            options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                            mode="BASE",
                            force_rerun=force_rerun)
        for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
            file_name = f"{kernel}_maa_{tile_size_str}"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_TS_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                                    command=f"{BNCH_DIR}/gapbs/{file_name}",
                                                    options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_TS_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                checkpoint=f"{CPT_TS_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/gapbs/{file_name}",
                                options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                mode="MAA",
                                tile_size=tile_size,
                                force_rerun=force_rerun)

    ########################################## RUN SELECTED EXPERIMENTS ##########################################
    run_tasks(parallelism)

def run_simulation_SC(parallelism, force_rerun):
    print("Starting Scalability simulation with the following configurations:")
    print(f"\t\tParallelism: {parallelism}")
    print(f"\t\tForce rerun: {force_rerun}")
    print(f"\t\tGEM5 directory: {GEM5_DIR}")
    print(f"\t\tData directory: {DATA_DIR}")
    print(f"\t\tCheckpoint directory: {CPT_DIR}")
    print(f"\t\tCheckpoint SC directory: {CPT_SC_DIR}")
    print(f"\t\tResult directory: {RSLT_DIR}")
    print(f"\t\tResult SC directory: {RSLT_SC_DIR}")
    print(f"\t\tBenchmark directory: {BNCH_DIR}")

    ########################################## NAS ##########################################
    for kernel in all_NAS_kernels:
        size = "c" if kernel == "cg" else "b"
        # 4 cores -- 1 MAA, BASE and MAA modes
        for mode in all_modes:
            file_name = f"{kernel}_maa" if mode == "MAA" else f"{kernel}_base"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                                    command=f"{BNCH_DIR}/NAS/{kernel}/{file_name}",
                                                    options=mode,
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/NAS/{kernel}/{file_name}",
                                options=mode,
                                mode=mode,
                                force_rerun=force_rerun)
        # 8 cores -- 1/2 MAAs
        for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
            # BASE mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/{kernel}/BASE/{size}/{num_cores}",
                                                    command=f"{BNCH_DIR}/NAS/{kernel}/{kernel}_base_{num_cores}C",
                                                    options="BASE",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_SC_DIR}/{kernel}/BASE/{size}/{num_cores}",
                                checkpoint=f"{CPT_SC_DIR}/{kernel}/BASE/{size}/{num_cores}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/NAS/{kernel}/{kernel}_base_{num_cores}C",
                                options="BASE",
                                mode="MAA",
                                num_cores=num_cores,
                                force_rerun=force_rerun)
            # checkpoint for MAA mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/{kernel}/MAA/{size}/{num_cores}",
                                                    command=f"{BNCH_DIR}/NAS/{kernel}/{kernel}_maa_{num_cores}C",
                                                    options="MAA",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            # simulation for 1/2 MAAs, MAA Mode
            for num_maa in num_maas:
                add_command_run_MAA(directory=f"{RSLT_SC_DIR}/{kernel}/MAA/{size}/{num_cores}/{num_maa}",
                                    checkpoint=f"{CPT_SC_DIR}/{kernel}/MAA/{size}/{num_cores}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{BNCH_DIR}/NAS/{kernel}/{kernel}_maa_{num_cores}C",
                                    options="MAA",
                                    mode="MAA",
                                    num_cores=num_cores,
                                    num_maas=num_maa,
                                    force_rerun=force_rerun)


    ########################################## SPATTER ##########################################
    for kernel in all_SPATTER_kernels:
        # 4 cores -- 1 MAA, BASE and MAA modes
        for mode in all_modes:
            file_name = "spatter_maa" if mode == "MAA" else "spatter_base"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/spatter/{kernel}/{mode}",
                                                    command=f"{BNCH_DIR}/spatter/build/{file_name}",
                                                    options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/spatter/{kernel}/{mode}",
                                checkpoint=f"{CPT_DIR}/spatter/{kernel}/{mode}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/spatter/build/{file_name}",
                                options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                mode=mode,
                                force_rerun=force_rerun)
        # 8 cores -- 1/2 MAAs
        for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
            # BASE mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/spatter/{kernel}/BASE/{num_cores}",
                                                    command=f"{BNCH_DIR}/spatter/build/spatter_base",
                                                    options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_SC_DIR}/spatter/{kernel}/BASE/{num_cores}",
                                checkpoint=f"{CPT_SC_DIR}/spatter/{kernel}/BASE/{num_cores}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/spatter/build/spatter_base",
                                options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                mode="BASE",
                                num_cores=num_cores,
                                force_rerun=force_rerun)
            # checkpoint for MAA mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/spatter/{kernel}/MAA/{num_cores}",
                                                    command=f"{BNCH_DIR}/spatter/build/spatter_maa_{num_cores}C",
                                                    options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            # simulation for 1/2 MAAs, MAA Mode
            for num_maa in num_maas:
                add_command_run_MAA(directory=f"{RSLT_SC_DIR}/spatter/{kernel}/MAA/{num_cores}/{num_maa}",
                                    checkpoint=f"{CPT_SC_DIR}/spatter/{kernel}/MAA/{num_cores}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{BNCH_DIR}/spatter/build/spatter_maa_{num_cores}C",
                                    options=f"-f {BNCH_DIR}/spatter/tests/test-data/{kernel}/all.json",
                                    mode="MAA",
                                    num_cores=num_cores,
                                    num_maas=num_maa,
                                    force_rerun=force_rerun)

    ######################################### HASHJOIN ##########################################
    size = 2000000
    size_per_core = 500000
    size_str = "2M"
    for kernel in all_HASHJOIN_kernels:
        # 4 cores -- 1 MAA, BASE and MAA modes
        for mode in all_modes:
            file_name = "hj_maa" if mode == "MAA" else "hj_base"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                                    command=f"{BNCH_DIR}/hashjoin/src/bin/x86/{file_name}",
                                                    options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/hashjoin/src/bin/x86/{file_name}",
                                options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                mode=mode,
                                force_rerun=force_rerun)
        # 8 cores -- 1/2 MAAs
        for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
            csize = size_per_core * num_cores
            csize_str = f"{int(csize/1000000)}M"
            # BASE mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/{kernel}/BASE/{csize_str}/{num_cores}",
                                                    command=f"{BNCH_DIR}/hashjoin/src/bin/x86/hj_base",
                                                    options=f"-a {kernel} -n {num_cores} -r {csize} -s {csize}",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_SC_DIR}/{kernel}/BASE/{csize_str}/{num_cores}",
                                checkpoint=f"{CPT_SC_DIR}/{kernel}/BASE/{csize_str}/{num_cores}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/hashjoin/src/bin/x86/hj_base",
                                options=f"-a {kernel} -n {num_cores} -r {csize} -s {csize}",
                                mode="BASE",
                                num_cores=num_cores,
                                force_rerun=force_rerun)
            # checkpoint for MAA mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/{kernel}/MAA/{csize_str}/{num_cores}",
                                                    command=f"{BNCH_DIR}/hashjoin/src/bin/x86/hj_maa_{num_cores}C",
                                                    options=f"-a {kernel} -n {num_cores} -r {csize} -s {csize}",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            # simulation for 1/2 MAAs, MAA Mode
            for num_maa in num_maas:
                add_command_run_MAA(directory=f"{RSLT_SC_DIR}/{kernel}/MAA/{csize_str}/{num_cores}/{num_maa}",
                                    checkpoint=f"{CPT_SC_DIR}/{kernel}/MAA/{csize_str}/{num_cores}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{BNCH_DIR}/hashjoin/src/bin/x86/hj_maa_{num_cores}C",
                                    options=f"-a {kernel} -n {num_cores} -r {csize} -s {csize}",
                                    mode="MAA",
                                    num_cores=num_cores,
                                    num_maas=num_maa,
                                    force_rerun=force_rerun)

    ########################################## UME ##########################################
    size = 2000000
    size_per_core = 500000
    size_str = "2M"
    for kernel in all_UME_kernels:
        # 4 cores -- 1 MAA, BASE and MAA modes
        for mode in all_modes:
            file_name = f"{kernel}_maa" if mode == "MAA" else f"{kernel}_base"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                                    command=f"{BNCH_DIR}/UME/{file_name}",
                                                    options=f"{size}",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/UME/{file_name}",
                                options=f"{size}",
                                mode=mode,
                                force_rerun=force_rerun)
        # 8 cores -- 1/2 MAAs
        for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
            csize = size_per_core * num_cores
            csize_str = f"{int(csize/1000000)}M"
            # BASE mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/{kernel}/BASE/{csize_str}/{num_cores}",
                                                    command=f"{BNCH_DIR}/UME/{kernel}_base",
                                                    options=f"{csize}",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_SC_DIR}/{kernel}/BASE/{csize_str}/{num_cores}",
                                checkpoint=f"{CPT_SC_DIR}/{kernel}/BASE/{csize_str}/{num_cores}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/UME/{kernel}_base",
                                options=f"{csize}",
                                mode="BASE",
                                num_cores=num_cores,
                                force_rerun=force_rerun)
            # checkpoint for MAA mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/{kernel}/MAA/{csize_str}/{num_cores}",
                                                    command=f"{BNCH_DIR}/UME/{kernel}_maa_{num_cores}C",
                                                    options=f"{csize}",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            # simulation for 1/2 MAAs, MAA Mode
            for num_maa in num_maas:
                add_command_run_MAA(directory=f"{RSLT_SC_DIR}/{kernel}/MAA/{csize_str}/{num_cores}/{num_maa}",
                                    checkpoint=f"{CPT_SC_DIR}/{kernel}/MAA/{csize_str}/{num_cores}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{BNCH_DIR}/UME/{kernel}_maa_{num_cores}C",
                                    options=f"{csize}",
                                    mode="MAA",
                                    num_cores=num_cores,
                                    num_maas=num_maa,
                                    force_rerun=force_rerun)

    ########################################## GAPB ##########################################
    for kernel in all_GAPB_kernels:
        graph_ext = "wsg" if kernel == "sssp" else "sg"
        # 4 cores -- 1 MAA, BASE and MAA modes
        for mode in all_modes:
            size = 20 if kernel == "bc" else 21 if kernel == "sssp" else 22
            file_name = f"{kernel}_maa" if mode == "MAA" else kernel
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                                    command=f"{BNCH_DIR}/gapbs/{file_name}",
                                                    options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/gapbs/{file_name}",
                                options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                mode=mode,
                                force_rerun=force_rerun)
        # 8 cores -- 1/2 MAAs
        for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
            lsize = 0 if num_cores == 4 else 1 if num_cores == 8 else 2
            size = (20+lsize) if kernel == "bc" else (21+lsize) if kernel == "sssp" else (22+lsize)
            # BASE mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/{kernel}/BASE/{size}/{num_cores}",
                                                    command=f"{BNCH_DIR}/gapbs/{kernel}",
                                                    options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            add_command_run_MAA(directory=f"{RSLT_SC_DIR}/{kernel}/BASE/{size}/{num_cores}",
                                checkpoint=f"{CPT_SC_DIR}/{kernel}/BASE/{size}/{num_cores}",
                                checkpoint_id = checkpoint_id,
                                command=f"{BNCH_DIR}/gapbs/{kernel}",
                                options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                mode=mode,
                                num_cores=num_cores,
                                force_rerun=force_rerun)
            # checkpoint for MAA mode
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_SC_DIR}/{kernel}/MAA/{size}/{num_cores}",
                                                    command=f"{BNCH_DIR}/gapbs/{kernel}_maa_{num_cores}C",
                                                    options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                                    num_cores=num_cores,
                                                    force_rerun=force_rerun)
            # simulation for 1/2 MAAs, MAA Mode
            for num_maa in num_maas:
                add_command_run_MAA(directory=f"{RSLT_SC_DIR}/{kernel}/MAA/{size}/{num_cores}/{num_maa}",
                                    checkpoint=f"{CPT_SC_DIR}/{kernel}/MAA/{size}/{num_cores}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{BNCH_DIR}/gapbs/{kernel}_maa_{num_cores}C",
                                    options=f"-f {BNCH_DIR}/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                    mode=mode,
                                    num_cores=num_cores,
                                    num_maas=num_maa,
                                    force_rerun=force_rerun)

    ########################################## RUN SELECTED EXPERIMENTS ##########################################
    run_tasks(parallelism)

def set_data_directory(path):
    global DATA_DIR
    global CPT_DIR
    global CPT_TS_DIR
    global CPT_SC_DIR
    global RSLT_DIR
    global RSLT_TS_DIR
    global RSLT_SC_DIR
    global BNCH_DIR
    DATA_DIR = path
    CPT_DIR = f"{DATA_DIR}/checkpoints"
    CPT_TS_DIR = f"{CPT_DIR}_TS"
    CPT_SC_DIR = f"{CPT_DIR}_SC"
    RSLT_DIR = f"{DATA_DIR}/results"
    RSLT_TS_DIR = f"{RSLT_DIR}_TS"
    RSLT_SC_DIR = f"{RSLT_DIR}_SC"
    BNCH_DIR = f"{GEM5_DIR}/benchmarks"
