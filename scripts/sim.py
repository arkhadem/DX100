import os
from threading import Thread, Lock

### get the parent directory where this script is located
GEM5_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = "/data4/arkhadem/gem5-hpc"
CPT_DIR = f"{DATA_DIR}/checkpoints_AE"
RSLT_DIR = f"{DATA_DIR}/results_AE"
LOG_DIR = f"{DATA_DIR}/logs_AE"
BNCH_DIR = f"{GEM5_DIR}/benchmarks"

all_modes = ["BASE", "MAA"]

all_NAS_kernels = ["is", "cg"]
all_GAPB_kernels = ["bfs", "pr", "bc", "sssp"]
all_SPATTER_kernels = ["xrage"]
all_HASHJOIN_kernels = ["PRH", "PRO"]
all_UME_kernels = ["gradzatp", "gradzatz", "gradzatz_invert", "gradzatp_invert"]

os.system("mkdir -p " + LOG_DIR)

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
    my_log_addr = f"{LOG_DIR}/logs_T{my_tid}.txt"
    os.system(f"rm {my_log_addr} 2>&1 > /dev/null; sleep 1;")
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
            with open (my_log_addr, "a") as f:
                f.write("T[{}]: tasks finished, bye!\n".format(my_tid))
            print("T[{}]: tasks finished, bye!".format(my_tid))
            break
        else:
            with open (my_log_addr, "a") as f:
                f.write("T[{}]: executing a new task: {}\n".format(my_tid, selected_task.command))
            print("T[{}]: executing a new task: {}".format(my_tid, selected_task.command))
            os.system(f"{selected_task.command} 2>&1 | tee -a {my_log_addr}")
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
                    
    have_maa = False
    l2_hwp_type = "StridePrefetcher"
    if mode == "MAA":
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

def run_simulation(parallelism, force_rerun):
    print("Starting simulation with the following configurations:")
    print(f"\t\tParallelism: {parallelism}")
    print(f"\t\tForce rerun: {force_rerun}")
    print(f"\t\tGEM5 directory: {GEM5_DIR}")
    print(f"\t\tData directory: {DATA_DIR}")
    print(f"\t\tCheckpoint directory: {CPT_DIR}")
    print(f"\t\tResult directory: {RSLT_DIR}")
    print(f"\t\tLog directory: {LOG_DIR}")
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
    os.system(f"cp {BNCH_DIR}/hashjoin-ph-2/relR_2M.dat ./")
    os.system(f"cp {BNCH_DIR}/hashjoin-ph-2/relS_2M.dat ./")
    os.system(f"cp {BNCH_DIR}/hashjoin-ph-2/relR_4M.dat ./")
    os.system(f"cp {BNCH_DIR}/hashjoin-ph-2/relS_4M.dat ./")
    os.system(f"cp {BNCH_DIR}/hashjoin-ph-2/relR_8M.dat ./")
    os.system(f"cp {BNCH_DIR}/hashjoin-ph-2/relS_8M.dat ./")
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