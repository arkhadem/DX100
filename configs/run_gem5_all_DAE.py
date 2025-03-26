import argparse
import os
from threading import Thread, Lock

parallelism = 32
GEM5_DIR = "/home/arkhadem/gem5-hpc"
DATA_DIR = "/data4/arkhadem/gem5-hpc"
DAE_CPT_DIR = f"{DATA_DIR}/checkpoints_DAE_new2"
DAE_RSLT_DIR = f"{DATA_DIR}/results_DAE_new2"
LOG_DIR = f"{DATA_DIR}/logs"

RUN_NAS = True
RUN_GAPB = True
RUN_SPATTER = True
RUN_HASHJOIN = True
RUN_UME = True

all_NAS_kernels = ["is", "cg"]
all_GAPB_kernels = ["bfs", "pr", "bc", "sssp"]
all_SPATTER_kernels = ["xrage", "flag"]
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
l3_mshrs = 256
l3_write_buffers = 128
mem_type = "Ramulator2"
ramulator_config = f"{GEM5_DIR}/ext/ramulator2/ramulator2/example_gem5_config.yaml"
program_interval = 1000
debug_type = "MAATrace" # "MAAAll,XBar,Cache,CacheVerbose,Exec,-ExecSymbol" #SyscallVerbose,MMU,Vma"#,Exec,-ExecSymbol" #,Exec,-ExecSymbol,MAAController,MAACpuPort,O3CPUAll" # ,MAACpuPort,MAAIndirect"
#,TLB,MMU" #,MAAAll" #" #,MAAAll,TLB,MMU" #,XBar,Ramulator2" # "MAAAll,MAATrace,XBar,Cache,CacheVerbose,MSHR" # "MAAAll,MAATrace" # "XBar,Cache,MAAAll" # "MAAAll" # "XBar,Cache,MAAAll,HWPrefetch" # PacketQueue
# debug_type = "LSQ,CacheAll,PseudoInst"
# debug_type = "O3CPUAll,CacheAll,PseudoInst"
# MemoryAccess,XBar,Cache,MAACpuPort,XBar,MemoryAccess,Cache,
# if sim_type == "MAA": 
#     debug_type = "SPD,MAARangeFuser,MAAALU,MAAController,MAACachePort,MAAMemPort,MAAIndirect,MAAStream,MAAInvalidator"
    # debug_type = "MAACachePort,MAAIndirect,MAAStream,Cache"

def add_command_checkpoint(directory, command, options, num_cores = 5):
    if os.path.isdir(directory):
        contents = os.listdir(directory)
        for content in contents:
            if "cpt." in content:
                print(f"Checkpoint {directory} already exists!")
                return None
    COMMAND = f"rm -r {directory} 2>&1 > /dev/null; sleep 1; mkdir -p {directory}; sleep 2; "
    COMMAND += f"OMP_PROC_BIND=false OMP_NUM_THREADS={num_cores} build/X86/gem5.fast "
    COMMAND += f"--outdir={directory} "
    COMMAND += f"{GEM5_DIR}/configs/deprecated/example/se.py "
    COMMAND += f"--cpu-type AtomicSimpleCPU -n {num_cores} --mem-size \"16GB\" "
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
                        num_cores = 5):
    if os.path.isdir(directory):
        contents = os.listdir(directory)
        for content in contents:
            if "stats.txt" in content:
                with open(f"{directory}/stats.txt", "r") as f:
                    lines = f.readlines()
                    if len(lines) > 50:
                        print(f"Experiment {directory} already done!")
                        return None
    l2_hwp_type = "DiffMatchingPrefetcher"
    l3_size = f"10MB"
    l3_assoc = 10
    l3_ports = 4
    mem_channels = 2
    ncbus_width = 32

    COMMAND = f"OMP_PROC_BIND=false OMP_NUM_THREADS={num_cores} {GEM5_DIR}/build/X86/gem5.opt "
    # if debug_type != None: # and mode == "MAA":
    COMMAND += f"--debug-flags={debug_type} "
    COMMAND += f"--outdir={directory} "
    COMMAND += f"{GEM5_DIR}/configs/deprecated/example/se.py "
    COMMAND += f"--cpu-type {cpu_type} "
    COMMAND += f"-n {num_cores} "
    COMMAND += f"--mem-size '16GB' "
    COMMAND += f"--sys-clock '{sys_clock}' "
    COMMAND += f"--cpu-clock '{sys_clock}' "
    COMMAND += f"--caches "
    COMMAND += f"--l1d_size={l1d_size} "
    COMMAND += f"--l1d_assoc={l1d_assoc} "
    COMMAND += f"--l1d-hwp-type={l1d_hwp_type} "
    COMMAND += f"--l1d_mshrs={l1d_mshrs} "
    COMMAND += f"--l1d_write_buffers={l1d_write_buffers} "
    COMMAND += f"--l1i_size={l1i_size} "
    COMMAND += f"--l1i_assoc={l1i_assoc} "
    COMMAND += f"--l1i-hwp-type={l1i_hwp_type} "
    COMMAND += f"--l1i_mshrs={l1i_mshrs} "
    COMMAND += f"--l1i_write_buffers={l1i_write_buffers} "
    COMMAND += f"--l2cache "
    COMMAND += f"--l2_size={l2_size} "
    COMMAND += f"--l2_assoc={l2_assoc} "
    COMMAND += f"--l2-hwp-type={l2_hwp_type} "
    COMMAND += f"--dmp-notify l1 "
    COMMAND += f"--l2_mshrs={l2_mshrs} "
    COMMAND += f"--l2_write_buffers={l2_write_buffers} "
    COMMAND += f"--l3cache "
    COMMAND += f"--l3_size={l3_size} "
    COMMAND += f"--l3_assoc={l3_assoc} "
    COMMAND += f"--l3_mshrs={l3_mshrs} "
    COMMAND += f"--l3_write_buffers={l3_write_buffers} "
    COMMAND += f"--l3_ports {l3_ports} "
    COMMAND += "--cacheline_size=64 "
    COMMAND += f"--mem-type {mem_type} "
    COMMAND += f"--ramulator-config {ramulator_config} "
    COMMAND += f"--mem-channels {mem_channels} "
    COMMAND += f"--maa_ncbus_width {ncbus_width} "
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
    # print(command)
    # exit(1)
    task = Task(command=command, dependency=checkpoint_id)
    if task in tasks:
        print(f"Task {command} already exists!")
        exit(1)
    tasks.append(task)

########################################## NAS ##########################################

if RUN_NAS:
    for kernel in all_NAS_kernels:
        file_name = f"{kernel}_maa_5C2"
        options = "MAA"
        size = "c" if kernel == "cg" else "b"
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{DAE_CPT_DIR}/{kernel}/DMP/{size}",
                                                command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                                                options=options)
        add_command_run_MAA(directory=f"{DAE_RSLT_DIR}/{kernel}/DMP/{size}",
                            checkpoint=f"{DAE_CPT_DIR}/{kernel}/DMP/{size}",
                            checkpoint_id = checkpoint_id,
                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                            options=options)
        
########################################## GAPB ##########################################

if RUN_GAPB:
    for kernel in all_GAPB_kernels:
        size = 20 if kernel == "bc" else 22
        file_name = f"{kernel}_maa_5C2"
        graph_ext = "wsg" if kernel == "sssp" else "sg"
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{DAE_CPT_DIR}/{kernel}/DMP/{size}",
                                                command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                                                options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1")
        add_command_run_MAA(directory=f"{DAE_RSLT_DIR}/{kernel}/DMP/{size}",
                            checkpoint=f"{DAE_CPT_DIR}/{kernel}/DMP/{size}",
                            checkpoint_id = checkpoint_id,
                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                            options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1")
            
# ########################################## SPATTER ##########################################

if RUN_SPATTER:
    for kernel in all_SPATTER_kernels:
        file_name = "spatter_maa_5C2"
        
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{DAE_CPT_DIR}/spatter/{kernel}/DMP",
                                                command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                                                options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json")
        add_command_run_MAA(directory=f"{DAE_RSLT_DIR}/spatter/{kernel}/DMP",
                            checkpoint=f"{DAE_CPT_DIR}/spatter/{kernel}/DMP",
                            checkpoint_id = checkpoint_id,
                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                            options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json")

# ######################################### HASHJOIN ##########################################

if RUN_HASHJOIN:
    size = 2000000
    size_str = "2M"

    for kernel in all_HASHJOIN_kernels:
            file_name = "hj_maa_5C2"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{DAE_CPT_DIR}/{kernel}/DMP/{size_str}",
                                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                                    options=f"-a {kernel} -n 5 -r {size} -s {size}")
            add_command_run_MAA(directory=f"{DAE_RSLT_DIR}/{kernel}/DMP/{size_str}",
                                checkpoint=f"{DAE_CPT_DIR}/{kernel}/DMP/{size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                options=f"-a {kernel} -n 5 -r {size} -s {size}")

########################################## UME ##########################################

if RUN_UME:
    size = 2000000
    size_str = "2M"

    for kernel in all_UME_kernels:
        file_name = f"{kernel}_maa_5C2"
        
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{DAE_CPT_DIR}/{kernel}/DMP/{size_str}",
                                                command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                                                options=f"{size}")
        add_command_run_MAA(directory=f"{DAE_RSLT_DIR}/{kernel}/DMP/{size_str}",
                            checkpoint=f"{DAE_CPT_DIR}/{kernel}/DMP/{size_str}",
                            checkpoint_id = checkpoint_id,
                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                            options=f"{size}")

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