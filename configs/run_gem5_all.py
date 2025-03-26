import argparse
import os
from threading import Thread, Lock

parallelism = 8
GEM5_DIR = "/home/arkhadem/gem5-hpc"
DATA_DIR = "/data4/arkhadem/gem5-hpc"
CPT_DIR = f"{DATA_DIR}/checkpoints_new"
RSLT_DIR = f"{DATA_DIR}/results_new"
RC_CPT_DIR = f"{DATA_DIR}/checkpoints_RC_new"
RC_RSLT_DIR = f"{DATA_DIR}/results_RC_new"
TS_CPT_DIR = f"{DATA_DIR}/checkpoints_TS_new"
TS_RSLT_DIR = f"{DATA_DIR}/results_TS_new"
SC_CPT_DIR = f"{DATA_DIR}/checkpoints_SC_new"
SC_RSLT_DIR = f"{DATA_DIR}/results_SC_new"
LOG_DIR = f"{DATA_DIR}/logs"

all_MAA_configs = [{"do_reorder": True, "force_cache": False},
                   {"do_reorder": False, "force_cache": False},
                   {"do_reorder": True, "force_cache": True},
                   {"do_reorder": False, "force_cache": True}]

all_modes = ["BASE", "MAA", "DMP"]
all_scaling_modes = ["BASE", "MAA"]

# all_tile_sizes = [1024, 2048, 4096, 8192, 16384, 32768]
# all_tile_sizes_str = ["1K", "2K", "4K", "8K", "16K", "32K"]
all_tile_sizes = [1024, 2048, 4096, 8192, 32768]
all_tile_sizes_str = ["1K", "2K", "4K", "8K", "32K"]
# all_scaling_cores = [4, 8, 16]
# all_scaling_maas = [1, 2, 4]
# all_scaling_cores = [4, 8]
# all_scaling_maas = [1, 2]
all_scaling_cores = [16] #[8] # [8]
all_scaling_maas = [4] # [2]

DO_GENERAL_EXP = False
DO_REORDER_FORCE_CACHE_EXP = False
DO_TILE_SIZE_EXP = False
DO_SCALING_EXP = True

RUN_MICRO = False
RUN_NAS = True
RUN_GAPB = True
RUN_SPATTER = True
RUN_HASHJOIN = True
RUN_UME = True

all_MICRO_kernels =   ["gather",
                        "scatter",
                        "rmw",
                        "gather_scatter",
                        "gather_rmw",
                        "gather_rmw_dst",
                        "gather_rmw_cond",
                        "gather_rmw_directrangeloop_cond",
                        "gather_rmw_indirectrangeloop_cond",
                        "gather_rmw_cond_indirectrangeloop_cond",
                        "gather_rmw_indirectcond_indirectrangeloop_indirectcond",
                        "gather_rmw_indirectcond_indirectrangeloop_indirectcond_dst"]
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
l3_assoc_per_core = 2
l3_assoc_extramaa_per_core = 0.5
l3_mshrs_per_core = 64
l3_write_buffers_per_core = 32
mem_type = "Ramulator2"
ramulator_config = f"{GEM5_DIR}/ext/ramulator2/ramulator2/example_gem5_config.yaml"
mem_channels_per_core = 0.5
program_interval = 1000
debug_type = "MAATrace" # "MAAAll,XBar,Cache,CacheVerbose,Exec,-ExecSymbol" #SyscallVerbose,MMU,Vma"#,Exec,-ExecSymbol" #,Exec,-ExecSymbol,MAAController,MAACpuPort,O3CPUAll" # ,MAACpuPort,MAAIndirect"
#,TLB,MMU" #,MAAAll" #" #,MAAAll,TLB,MMU" #,XBar,Ramulator2" # "MAAAll,MAATrace,XBar,Cache,CacheVerbose,MSHR" # "MAAAll,MAATrace" # "XBar,Cache,MAAAll" # "MAAAll" # "XBar,Cache,MAAAll,HWPrefetch" # PacketQueue
# debug_type = "LSQ,CacheAll,PseudoInst"
# debug_type = "O3CPUAll,CacheAll,PseudoInst"
# MemoryAccess,XBar,Cache,MAACpuPort,XBar,MemoryAccess,Cache,
# if sim_type == "MAA": 
#     debug_type = "SPD,MAARangeFuser,MAAALU,MAAController,MAACachePort,MAAMemPort,MAAIndirect,MAAStream,MAAInvalidator"
    # debug_type = "MAACachePort,MAAIndirect,MAAStream,Cache"

def add_command_checkpoint(directory, command, options, num_cores = 4):
    if os.path.isdir(directory):
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
                        force_cache = False):
    return None
    if os.path.isdir(directory):
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
    if mode == "DMP":
        l2_hwp_type = "DiffMatchingPrefetcher"
        l3_size = f"{int((l3_size_per_core + l3_size_extramaa_per_core) * num_cores)}MB"
        l3_assoc = int((l3_assoc_per_core + l3_assoc_extramaa_per_core) * num_cores)
    elif mode in ["MAA", "CMP"]:
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
        # if reconfigurable_RT:
        #     COMMAND += "--maa_reconfigure_row_table "
        # else:
        #     COMMAND += "--maa_num_initial_row_table_slices 4 "
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
    # print(command)
    # exit(1)
    task = Task(command=command, dependency=checkpoint_id)
    if task in tasks:
        print(f"Task {command} already exists!")
        exit(1)
    tasks.append(task)


# add_command_run_MAA(directory=f"{SC_RSLT_DIR}/is/MAA/b/8",
#                 checkpoint=f"{SC_CPT_DIR}/is/MAA/b/8",
#                 checkpoint_id = None,
#                 command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/is/is_maa_8C",
#                 options="MAA",
#                 mode="MAA",
#                 num_cores=8,
#                 num_maas=2)


# checkpoint_id = None
# checkpoint_id = add_command_checkpoint(directory=f"{RC_CPT_DIR}/bfs/MAA/22/debug",
#                                         command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/bfs_maa",
#                                         options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_22.sg -l -n 1")
# add_command_run_MAA(directory=f"{RC_RSLT_DIR}/bfs/MAA/22/REORDER/FCACHE/debug",
#                     checkpoint=f"{RC_CPT_DIR}/bfs/MAA/22/debug",
#                     checkpoint_id = checkpoint_id,
#                     command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/bfs_maa",
#                     options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_22.sg -l -n 1",
#                     mode="MAA",
#                     do_reorder=True,
#                     force_cache=True)


# all_tiles = [1024, 2048, 4096, 8192, 16384]
# all_tiles_str = ["1K", "2K", "4K", "8K", "16K"]
# all_distances = [256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304]
# all_distances_str = ["256", "1K", "4K", "16K", "64K", "256K", "1M", "4M"]
# all_sizes = [2000000]
# all_sizes_str = ["2M"]
                    

# add_command_run_MAA(directory=f"{RSLT_DIR}/rmw/allmiss/BAH0/RBH100/CBH0/BGH0/64K_MAA_port",
#                     checkpoint=f"{CPT_DIR}/rmw/allmiss/BAH0/RBH100/CBH0/BGH0/64K_MAA_port",
#                     checkpoint_id = None,
#                     command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_T16K.o",
#                     options=f"65536 MAA rmw allmiss 0 100 0 0",
#                     mode="MAA",
#                     num_cores=4)

# test_dir = "CISC"
# for kernel in ["rmw", "scatter", "gather_full"]: #["gather"]: #, "rmw", "scatter", "gather_full"]: #  ["gather_directrangeloop_indircond"]:
#     num_cores = 1 if kernel == "scatter" else 4
#     for size, size_str in zip([65536], ["64K"]):
#         for mode in ["MAA", "CMP", "BASE"]: #, "CMP"]: # ["MAA"]:
#             for BAH in [0, 1]:
#                 for RBH in [0, 50, 100]:
#                     for CHH in [0, 1]:
#                         if CHH == 1 and (BAH != 1 or RBH != 100):
#                             continue
#                         for BGH in [0, 1]:
#                             if BGH == 1 and (BAH != 1 or RBH != 100 or CHH != 1):
#                                 continue
#                             checkpoint_id = None
#                             # checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/allmiss/BAH{BAH}/RBH{RBH}/CBH{CHH}/BGH{BGH}/{size_str}_{mode}_port",
#                             #                                         command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#                             #                                         options=f"{size} {mode} {kernel} allmiss {BAH} {RBH} {CHH} {BGH}",
#                             #                                         num_cores=num_cores)

#                             add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/allmiss/BAH{BAH}/RBH{RBH}/CBH{CHH}/BGH{BGH}/{size_str}_{mode}_port",
#                                                 checkpoint=f"{CPT_DIR}/{kernel}/allmiss/BAH{BAH}/RBH{RBH}/CBH{CHH}/BGH{BGH}/{size_str}_{mode}_port",
#                                                 checkpoint_id = checkpoint_id,
#                                                 command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#                                                 options=f"{size} {mode} {kernel} allmiss {BAH} {RBH} {CHH} {BGH}",
#                                                 mode=mode,
#                                                 num_cores=num_cores)
                        
#             checkpoint_id = None
#             # checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/allhit/{size_str}_{mode}_port",
#             #                                         command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#             #                                         options=f"{size} {mode} {kernel} allhit", num_cores=num_cores)
#             add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/allhit/{size_str}_{mode}_port",
#                                 checkpoint=f"{CPT_DIR}/{kernel}/allhit/{size_str}_{mode}_port",
#                                 checkpoint_id = checkpoint_id,
#                                 command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#                                 options=f"{size} {mode} {kernel} allhit",
#                                 mode=mode,
#                                 num_cores=num_cores)
            
#             checkpoint_id = None
#             # checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/allhitl3/{size_str}_{mode}_port",
#             #                                         command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#             #                                         options=f"{size} {mode} {kernel} allhitl3", num_cores=num_cores)
#             add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/allhitl3/{size_str}_{mode}_port",
#                                 checkpoint=f"{CPT_DIR}/{kernel}/allhitl3/{size_str}_{mode}_port",
#                                 checkpoint_id = checkpoint_id,
#                                 command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#                                 options=f"{size} {mode} {kernel} allhitl3",
#                                 mode=mode,
#                                 maa_warmer=True,
#                                 num_cores=num_cores)

# for kernel in ["gather_rmw_dst"]: # ["rmw", "scatter", "gather", "gather_full"]:
#     for mode in ["MAA", "CMP"]: # "CMP", "BASE", 
#         checkpoint_id = None
#         checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/random/256K_{mode}",
#                                                 command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_T16K.o",
#                                                 options=f"262144 {mode} {kernel} random 32768", num_cores=4)
#         add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/random/reorder/256K_{mode}",
#                             checkpoint=f"{CPT_DIR}/{kernel}/random/256K_{mode}",
#                             checkpoint_id = checkpoint_id,
#                             command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_T16K.o",
#                             options=f"262144 {mode} {kernel} random 32768",
#                             mode=mode,
#                             num_cores=4,
#                             do_reorder=True)
#         0(directory=f"{RSLT_DIR}/{kernel}/random/noreorder/256K_{mode}",
#                             checkpoint=f"{CPT_DIR}/{kernel}/random/256K_{mode}",
#                             checkpoint_id = checkpoint_id,
#                             command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_T16K.o",
#                             options=f"262144 {mode} {kernel} random 32768",
#                             mode=mode,
#                             num_cores=4,
#                             do_reorder=False)

# for kernel in ["gather", "rmw"]: # ["rmw", "gather", "gather_rmw_directrangeloop_cond"]: # ["rmw"]:
#     test_dir = "CISC_allrowmiss" # "CISC_allrowmiss_gathercorrect" if kernel == "gather" else "CISC_allrowmiss"
#     for size, size_str in zip([65536], ["64K"]): # zip([16384], ["16K"]): #  zip([16384, 262144], ["16K", "256K"]):
#         for mode in ["BASE", "MAA"]: # , "CMP"]: # ["MAA"]:
#             for BAH in [0, 1]:
#                 for RBH in [0, 13, 25, 38, 50, 63, 75, 88, 100]:
#                     for CHH in [0]:
#                         for BGH in [0]:
#                             checkpoint_id = None
#                             checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/allmiss/BAH{BAH}/RBH{RBH}/CBH{CHH}/BGH{BGH}/{size_str}_{mode}",
#                                                                     command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#                                                                     options=f"{size} {mode} {kernel} allmiss {BAH} {RBH} {CHH} {BGH}")

#                             add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/allmiss/BAH{BAH}/RBH{RBH}/CBH{CHH}/BGH{BGH}/{size_str}_{mode}",
#                                                 checkpoint=f"{CPT_DIR}/{kernel}/allmiss/BAH{BAH}/RBH{RBH}/CBH{CHH}/BGH{BGH}/{size_str}_{mode}",
#                                                 checkpoint_id = checkpoint_id,
#                                                 command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#                                                 options=f"{size} {mode} {kernel} allmiss {BAH} {RBH} {CHH} {BGH}",
#                                                 mode=mode,
#                                                 tile_size=16384)
                        
#             checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/allhit/{size_str}_{mode}",
#                                                     command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#                                                     options=f"{size} {mode} {kernel} allhit")

#             add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/allhit/{size_str}_{mode}",
#                                 checkpoint=f"{CPT_DIR}/{kernel}/allhit/{size_str}_{mode}",
#                                 checkpoint_id = checkpoint_id,
#                                 command=f"{GEM5_DIR}/tests/test-progs/MAA/{test_dir}/test_T16K.o",
#                                 options=f"{size} {mode} {kernel} allhit",
#                                 mode=mode,
#                                 tile_size=16384)

########################################## NAS ##########################################

if RUN_NAS:
    # General experiments
    if DO_GENERAL_EXP:
        for kernel in all_NAS_kernels:
            for mode in all_modes:
                file_name = f"{kernel}_maa" if mode == "MAA" else f"{kernel}_base"
                options = "BASE" if mode == "DMP" else mode
                size = "c" if kernel == "cg" else "b"
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                                                        options=options)
                add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size}",
                                    checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                                    options=options,
                                    mode=mode)

    # Reordering and force cache experiments
    if DO_REORDER_FORCE_CACHE_EXP:
        for kernel in all_NAS_kernels:
            file_name = f"{kernel}_maa"
            size = "c" if kernel == "cg" else "b"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{RC_CPT_DIR}/{kernel}/MAA/{size}",
                                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                                                    options="MAA")
            for MAA_config in all_MAA_configs:
                reordering = "REORDER" if MAA_config['do_reorder'] else "NOREORDER"
                force_cache = "FCACHE" if MAA_config['force_cache'] else "NOFCACHE"
                add_command_run_MAA(directory=f"{RC_RSLT_DIR}/{kernel}/MAA/{size}/{reordering}/{force_cache}",
                                    checkpoint=f"{RC_CPT_DIR}/{kernel}/MAA/{size}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                                    options="MAA",
                                    mode="MAA",
                                    do_reorder=MAA_config['do_reorder'],
                                    force_cache=MAA_config['force_cache'])

    # Tile size experiments
    if DO_TILE_SIZE_EXP:
        for kernel in all_NAS_kernels:
            for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
                file_name = f"{kernel}_maa_{tile_size_str}"
                size = "c" if kernel == "cg" else "b"
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{TS_CPT_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                                                        options="MAA") 
                add_command_run_MAA(directory=f"{TS_RSLT_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                    checkpoint=f"{TS_CPT_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                                    options="MAA",
                                    mode="MAA",
                                    tile_size=tile_size)
    
    # Scaling experiments
    if DO_SCALING_EXP:
        for kernel in all_NAS_kernels:
            for mode in all_scaling_modes:
                for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
                    file_name = f"{kernel}_maa_{num_cores}C" if mode == "MAA" else f"{kernel}_base_{num_cores}C"
                    size = "c" if kernel == "cg" else "b"
                    options = "BASE" if mode == "DMP" else mode
                    checkpoint_id = None
                    checkpoint_id = add_command_checkpoint(directory=f"{SC_CPT_DIR}/{kernel}/{mode}/{size}/{num_cores}",
                                                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                                                            options=options,
                                                            num_cores=num_cores)
                    add_command_run_MAA(directory=f"{SC_RSLT_DIR}/{kernel}/{mode}/{size}/{num_cores}/{num_maas}",
                                        checkpoint=f"{SC_CPT_DIR}/{kernel}/{mode}/{size}/{num_cores}",
                                        checkpoint_id = checkpoint_id,
                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/{kernel}/{file_name}",
                                        options=options,
                                        mode=mode,
                                        num_cores=num_cores,
                                        num_maas=num_maas)

# ########################################## SPATTER ##########################################

if RUN_SPATTER:
    # General experiments
    if DO_GENERAL_EXP:
        for kernel in all_SPATTER_kernels:
            for mode in all_modes:
                file_name = "spatter_maa" if mode == "MAA" else "spatter_base"
                
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/spatter/{kernel}/{mode}",
                                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                                                        options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json")
                add_command_run_MAA(directory=f"{RSLT_DIR}/spatter/{kernel}/{mode}",
                                    checkpoint=f"{CPT_DIR}/spatter/{kernel}/{mode}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                                    options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json",
                                    mode=mode)

    # Reordering and force cache experiments
    if DO_REORDER_FORCE_CACHE_EXP:
        for kernel in all_SPATTER_kernels:
            file_name = "spatter_maa"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{RC_CPT_DIR}/spatter/{kernel}/MAA",
                                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                                                    options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json")
            for MAA_config in all_MAA_configs:
                reordering = "REORDER" if MAA_config['do_reorder'] else "NOREORDER"
                force_cache = "FCACHE" if MAA_config['force_cache'] else "NOFCACHE"
                add_command_run_MAA(directory=f"{RC_RSLT_DIR}/spatter/{kernel}/MAA/{reordering}/{force_cache}",
                                    checkpoint=f"{RC_CPT_DIR}/spatter/{kernel}/MAA",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                                    options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json",
                                    mode="MAA",
                                    do_reorder=MAA_config['do_reorder'],
                                    force_cache=MAA_config['force_cache'])
            
    # Tile size experiments
    if DO_TILE_SIZE_EXP:
        for kernel in all_SPATTER_kernels:
            for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
                file_name = f"spatter_maa_{tile_size_str}"
                
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{TS_CPT_DIR}/spatter/{kernel}/MAA/{tile_size_str}",
                                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                                                        options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json")
                add_command_run_MAA(directory=f"{TS_RSLT_DIR}/spatter/{kernel}/MAA/{tile_size_str}",
                                    checkpoint=f"{TS_CPT_DIR}/spatter/{kernel}/MAA/{tile_size_str}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                                    options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json",
                                    mode="MAA",
                                    tile_size=tile_size)
    
    # Scaling experiments
    if DO_SCALING_EXP:
        for kernel in all_SPATTER_kernels:
            for mode in all_scaling_modes:
                for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
                    file_name = f"spatter_maa_{num_cores}C" if mode == "MAA" else "spatter_base"
                    checkpoint_id = None
                    checkpoint_id = add_command_checkpoint(directory=f"{SC_CPT_DIR}/spatter/{kernel}/{mode}/{num_cores}",
                                                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                                                            options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json",
                                                            num_cores=num_cores)
                    add_command_run_MAA(directory=f"{SC_RSLT_DIR}/spatter/{kernel}/{mode}/{num_cores}/{num_maas}",
                                        checkpoint=f"{SC_CPT_DIR}/spatter/{kernel}/{mode}/{num_cores}",
                                        checkpoint_id = checkpoint_id,
                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
                                        options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json",
                                        mode=mode,
                                        num_cores=num_cores,
                                        num_maas=num_maas)

# ######################################### HASHJOIN ##########################################

# os.system(f"cp {GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/relR_2M.dat ./")
# os.system(f"cp {GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/relS_2M.dat ./")
# os.system(f"cp {GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/relR_8M.dat ./")
# os.system(f"cp {GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/relS_8M.dat ./")

if RUN_HASHJOIN:
    size = 2000000
    size_per_core = 500000
    size_str = "2M"

    # General experiments
    if DO_GENERAL_EXP:
        for kernel in all_HASHJOIN_kernels:
            for mode in all_modes:
                file_name = "hj_maa" if mode == "MAA" else "hj_base"
                
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                                        options=f"-a {kernel} -n 4 -r {size} -s {size}")
                add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
                                    checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                    options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                    mode=mode)
            
    # Reordering and force cache experiments
    if DO_REORDER_FORCE_CACHE_EXP:
        for kernel in all_HASHJOIN_kernels:
            file_name = "hj_maa"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{RC_CPT_DIR}/{kernel}/MAA/{size_str}",
                                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                                    options=f"-a {kernel} -n 4 -r {size} -s {size}")
            for MAA_config in all_MAA_configs:
                reordering = "REORDER" if MAA_config['do_reorder'] else "NOREORDER"
                force_cache = "FCACHE" if MAA_config['force_cache'] else "NOFCACHE"
                add_command_run_MAA(directory=f"{RC_RSLT_DIR}/{kernel}/MAA/{size_str}/{reordering}/{force_cache}",
                                    checkpoint=f"{RC_CPT_DIR}/{kernel}/MAA/{size_str}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                    options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                    mode="MAA",
                                    do_reorder=MAA_config['do_reorder'],
                                    force_cache=MAA_config['force_cache'])
            
    # Tile size experiments
    if DO_TILE_SIZE_EXP:
        for kernel in all_HASHJOIN_kernels:
            for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
                file_name = f"hj_maa_{tile_size_str}"
                
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{TS_CPT_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                                        options=f"-a {kernel} -n 4 -r {size} -s {size}")
                add_command_run_MAA(directory=f"{TS_RSLT_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                    checkpoint=f"{TS_CPT_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                    options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                    mode="MAA",
                                    tile_size=tile_size)
    
    # Scaling experiments
    if DO_SCALING_EXP:
        for kernel in all_HASHJOIN_kernels:
            for mode in all_scaling_modes:
                for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
                    file_name = f"hj_maa_{num_cores}C" if mode == "MAA" else "hj_base"
                    csize = size_per_core * num_cores
                    csize_str = f"{int(csize/1000000)}M"
                    checkpoint_id = None
                    checkpoint_id = add_command_checkpoint(directory=f"{SC_CPT_DIR}/{kernel}/{mode}/{csize_str}/{num_cores}",
                                                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                                            options=f"-a {kernel} -n {num_cores} -r {csize} -s {csize}",
                                                            num_cores=num_cores)
                    add_command_run_MAA(directory=f"{SC_RSLT_DIR}/{kernel}/{mode}/{csize_str}/{num_cores}/{num_maas}",
                                        checkpoint=f"{SC_CPT_DIR}/{kernel}/{mode}/{csize_str}/{num_cores}",
                                        checkpoint_id = checkpoint_id,
                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin/src/bin/x86/{file_name}",
                                        options=f"-a {kernel} -n {num_cores} -r {csize} -s {csize}",
                                        mode=mode,
                                        num_cores=num_cores,
                                        num_maas=num_maas)


########################################## UME ##########################################

if RUN_UME:
    size = 2000000
    size_per_core = 500000
    size_str = "2M"

    # General experiments
    if DO_GENERAL_EXP:
        for kernel in all_UME_kernels:
            for mode in all_modes:
                    file_name = f"{kernel}_maa" if mode == "MAA" else f"{kernel}_base"
                    
                    checkpoint_id = None
                    checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                                                            options=f"{size}")
                    add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
                                        checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                        checkpoint_id = checkpoint_id,
                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                                        options=f"{size}",
                                        mode=mode)

    # Reordering and force cache experiments
    if DO_REORDER_FORCE_CACHE_EXP:
        for kernel in all_UME_kernels:
            file_name = f"{kernel}_maa"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{RC_CPT_DIR}/{kernel}/MAA/{size_str}",
                                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                                                    options=f"{size}")
            for MAA_config in all_MAA_configs:
                reordering = "REORDER" if MAA_config['do_reorder'] else "NOREORDER"
                force_cache = "FCACHE" if MAA_config['force_cache'] else "NOFCACHE"
                add_command_run_MAA(directory=f"{RC_RSLT_DIR}/{kernel}/MAA/{size_str}/{reordering}/{force_cache}",
                                    checkpoint=f"{RC_CPT_DIR}/{kernel}/MAA/{size_str}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                                    options=f"{size}",
                                    mode="MAA",
                                    do_reorder=MAA_config['do_reorder'],
                                    force_cache=MAA_config['force_cache'])
            
    # Tile size experiments
    if DO_TILE_SIZE_EXP:
        for kernel in all_UME_kernels:
            for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
                file_name = f"{kernel}_maa_{tile_size_str}"
                
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{TS_CPT_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                                                    options=f"{size}")
                add_command_run_MAA(directory=f"{TS_RSLT_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                    checkpoint=f"{TS_CPT_DIR}/{kernel}/MAA/{size_str}/{tile_size_str}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                                    options=f"{size}",
                                    mode="MAA",
                                    tile_size=tile_size)
    
    # Scaling experiments
    if DO_SCALING_EXP:
        for kernel in all_UME_kernels:
            for mode in all_scaling_modes:
                for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
                    file_name = f"{kernel}_maa_{num_cores}C" if mode == "MAA" else f"{kernel}_base"
                    csize = size_per_core * num_cores
                    csize_str = f"{int(csize/1000000)}M"
                    checkpoint_id = None
                    checkpoint_id = add_command_checkpoint(directory=f"{SC_CPT_DIR}/{kernel}/{mode}/{csize_str}/{num_cores}",
                                                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                                                            options=f"{csize}",
                                                            num_cores=num_cores)
                    add_command_run_MAA(directory=f"{SC_RSLT_DIR}/{kernel}/{mode}/{csize_str}/{num_cores}/{num_maas}",
                                        checkpoint=f"{SC_CPT_DIR}/{kernel}/{mode}/{csize_str}/{num_cores}",
                                        checkpoint_id = checkpoint_id,
                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
                                        options=f"{csize}",
                                        mode=mode,
                                        num_cores=num_cores,
                                        num_maas=num_maas)

########################################## GAPB ##########################################

if RUN_GAPB:
    # General experiments
    if DO_GENERAL_EXP:
        for kernel in all_GAPB_kernels:
            for mode in all_modes:
                size = 20 if kernel == "bc" else 22
                file_name = f"{kernel}_maa" if mode == "MAA" else kernel
                graph_ext = "wsg" if kernel == "sssp" else "sg"
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                                                        options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1")
                add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size}",
                                    checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                                    options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                    mode=mode)
            
    # Reordering and force cache experiments
    if DO_REORDER_FORCE_CACHE_EXP:
        for kernel in all_GAPB_kernels:
            size = 20 if kernel == "bc" else 22
            file_name = f"{kernel}_maa"
            graph_ext = "wsg" if kernel == "sssp" else "sg"
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{RC_CPT_DIR}/{kernel}/MAA/{size}",
                                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                                                    options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1")
            for MAA_config in all_MAA_configs:
                reordering = "REORDER" if MAA_config['do_reorder'] else "NOREORDER"
                force_cache = "FCACHE" if MAA_config['force_cache'] else "NOFCACHE"
                add_command_run_MAA(directory=f"{RC_RSLT_DIR}/{kernel}/MAA/{size}/{reordering}/{force_cache}",
                                    checkpoint=f"{RC_CPT_DIR}/{kernel}/MAA/{size}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                                    options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                    mode="MAA",
                                    do_reorder=MAA_config['do_reorder'],
                                    force_cache=MAA_config['force_cache'])

    # Tile size experiments
    if DO_TILE_SIZE_EXP:
        for kernel in all_GAPB_kernels:
            for tile_size, tile_size_str in zip(all_tile_sizes, all_tile_sizes_str):
                size = 20 if kernel == "bc" else 22
                file_name = f"{kernel}_maa_{tile_size_str}"
                graph_ext = "wsg" if kernel == "sssp" else "sg"
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{TS_CPT_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                                                        options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1")
                add_command_run_MAA(directory=f"{TS_RSLT_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                    checkpoint=f"{TS_CPT_DIR}/{kernel}/MAA/{size}/{tile_size_str}",
                                    checkpoint_id = checkpoint_id,
                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                                    options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                    mode="MAA",
                                    tile_size=tile_size)
    
    # Scaling experiments
    if DO_SCALING_EXP:
        for kernel in all_GAPB_kernels:
            for mode in all_scaling_modes:
                for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
                    lsize = 0 if num_cores == 4 else 1 if num_cores == 8 else 2
                    size = (20+lsize) if kernel == "bc" else (21+lsize) if kernel == "sssp" else (22+lsize)
                    file_name = f"{kernel}_maa_{num_cores}C" if mode == "MAA" else kernel
                    graph_ext = "wsg" if kernel == "sssp" else "sg"
                    checkpoint_id = None
                    checkpoint_id = add_command_checkpoint(directory=f"{SC_CPT_DIR}/{kernel}/{mode}/{size}/{num_cores}",
                                                            command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                                                            options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                                            num_cores=num_cores)
                    add_command_run_MAA(directory=f"{SC_RSLT_DIR}/{kernel}/{mode}/{size}/{num_cores}/{num_maas}",
                                        checkpoint=f"{SC_CPT_DIR}/{kernel}/{mode}/{size}/{num_cores}",
                                        checkpoint_id = checkpoint_id,
                                        command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
                                        options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
                                        mode=mode,
                                        num_cores=num_cores,
                                        num_maas=num_maas)

########################################## MICRO ##########################################

if RUN_MICRO:
    all_dtypes = ["f32", "f64"]
    # Reordering and force cache experiments
    if DO_REORDER_FORCE_CACHE_EXP:
        for kernel in all_MICRO_kernels:
            for dtype in all_dtypes:
                checkpoint_id = None
                checkpoint_id = add_command_checkpoint(directory=f"{RC_CPT_DIR}/tests/{kernel}/{dtype}/MAA",
                                                        command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_double_T16K.o",
                                                        options=f"2000000 CMP {dtype} {kernel}")
                for MAA_config in all_MAA_configs:
                    reordering = "REORDER" if MAA_config['do_reorder'] else "NOREORDER"
                    force_cache = "FCACHE" if MAA_config['force_cache'] else "NOFCACHE"
                    add_command_run_MAA(directory=f"{RC_RSLT_DIR}/tests/{kernel}/{dtype}/MAA/{reordering}/{force_cache}",
                                        checkpoint=f"{RC_CPT_DIR}/tests/{kernel}/{dtype}/MAA",
                                        checkpoint_id = checkpoint_id,
                                        command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_double_T16K.o",
                                        options=f"2000000 CMP {dtype} {kernel}",
                                        mode="MAA",
                                        do_reorder=MAA_config['do_reorder'],
                                        force_cache=MAA_config['force_cache'])
    if DO_SCALING_EXP:
        for kernel in all_MICRO_kernels:
            for dtype in all_dtypes:
                for mode in all_scaling_modes:
                    for num_cores, num_maas in zip(all_scaling_cores, all_scaling_maas):
                        options = f"{2000000*num_maas} {mode} {dtype} {kernel}"
                        checkpoint_id = None
                        checkpoint_id = add_command_checkpoint(directory=f"{SC_CPT_DIR}/tests/{kernel}/{dtype}/{mode}/{num_cores}",
                                                                command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_double_{num_cores}C.o",
                                                                options=options,
                                                                num_cores=num_cores)
                        add_command_run_MAA(directory=f"{SC_RSLT_DIR}/tests/{kernel}/{dtype}/{mode}/{num_cores}/{num_maas}",
                                            checkpoint=f"{SC_CPT_DIR}/tests/{kernel}/{dtype}/{mode}/{num_cores}",
                                            checkpoint_id = checkpoint_id,
                                            command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_double_{num_cores}C.o",
                                            options=options,
                                            mode=mode,
                                            num_cores=num_cores,
                                            num_maas=num_maas)

# ########################################## RUN SELECTED EXPERIMENTS ##########################################
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