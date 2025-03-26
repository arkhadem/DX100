import argparse
import os
from threading import Thread, Lock

parallelism = 32
GEM5_DIR = "/home/arkhadem/gem5-hpc"
DATA_DIR = "/data4/arkhadem/gem5-hpc"
CPT_DIR = f"{DATA_DIR}/checkpoints_valgrind"
RSLT_DIR = f"{DATA_DIR}/results_valgrind"
LOG_DIR = f"{DATA_DIR}/logs"

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
mem_size = "16GB"
sys_clock = "3.2GHz"
l1d_size = "32kB"
l1d_assoc = 8
l1d_hwp_type = "StridePrefetcher"
l1d_mshrs = 16
l1i_size = "32kB"
l1i_assoc = 8
l1i_hwp_type = "StridePrefetcher"
l1i_mshrs = 16
l2_size = "256kB"
l2_assoc = 4
l2_mshrs = 32
l3_mshrs = 256
mem_type = "Ramulator2"
ramulator_config = f"{GEM5_DIR}/ext/ramulator2/ramulator2/example_gem5_config.yaml"
mem_channels = 2
program_interval = 1000
debug_type = "MAATrace" #,Exec,-ExecSymbol" # ,MAACpuPort,MAAIndirect"
#,TLB,MMU" #,MAAAll" #" #,MAAAll,TLB,MMU" #,XBar,Ramulator2" # "MAAAll,MAATrace,XBar,Cache,CacheVerbose,MSHR" # "MAAAll,MAATrace" # "XBar,Cache,MAAAll" # "MAAAll" # "XBar,Cache,MAAAll,HWPrefetch" # PacketQueue
# debug_type = "LSQ,CacheAll,PseudoInst"
# debug_type = "O3CPUAll,CacheAll,PseudoInst"
# MemoryAccess,XBar,Cache,MAACpuPort,XBar,MemoryAccess,Cache,
# if sim_type == "MAA": 
#     debug_type = "SPD,MAARangeFuser,MAAALU,MAAController,MAACachePort,MAAMemPort,MAAIndirect,MAAStream,MAAInvalidator"
    # debug_type = "MAACachePort,MAAIndirect,MAAStream,Cache"

def add_command_checkpoint(directory, command, options, num_cores = 4):
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

def add_command_run_MAA(directory, checkpoint, checkpoint_id, command, options, mode, tile_size = 16384, reconfigurable_RT = False, maa_warmer = False, num_cores = 4, do_prefetch = True, do_reorder = True):
    have_maa = False
    l2_hwp_type = "StridePrefetcher"
    l3_size = "8MB"
    l3_assoc = 16
    if mode == "DMP":
        l2_hwp_type = "DiffMatchingPrefetcher"
        l3_size = "10MB"
        l3_assoc = 20
    elif mode in ["MAA", "CMP"]:
        have_maa = True
    elif mode == "BASE":
        l3_size = "10MB"
        l3_assoc = 20
    else:
        raise ValueError("Unknown mode")

    COMMAND = f"OMP_PROC_BIND=false OMP_NUM_THREADS={num_cores} "
    COMMAND += "valgrind --leak-check=yes --suppressions=util/valgrind-suppressions "
    COMMAND += f"{GEM5_DIR}/build/X86/gem5.opt "
    # if debug_type != None: # and mode == "MAA":
    COMMAND += f"--debug-flags={debug_type} "
    COMMAND += f"--outdir={directory} "
    COMMAND += f"{GEM5_DIR}/configs/deprecated/example/se.py "
    COMMAND += f"--cpu-type {cpu_type} "
    COMMAND += f"-n {num_cores} "
    COMMAND += f"--mem-size '{mem_size}' "
    COMMAND += f"--sys-clock '{sys_clock}' "
    COMMAND += f"--cpu-clock '{sys_clock}' "
    COMMAND += f"--caches "
    COMMAND += f"--l1d_size={l1d_size} "
    COMMAND += f"--l1d_assoc={l1d_assoc} "
    if do_prefetch:
        COMMAND += f"--l1d-hwp-type={l1d_hwp_type} "
    COMMAND += f"--l1d_mshrs={l1d_mshrs} "
    COMMAND += f"--l1i_size={l1i_size} "
    COMMAND += f"--l1i_assoc={l1i_assoc} "
    if do_prefetch:
        COMMAND += f"--l1i-hwp-type={l1i_hwp_type} "
    COMMAND += f"--l1i_mshrs={l1i_mshrs} "
    COMMAND += f"--l2cache "
    COMMAND += f"--l2_size={l2_size} "
    COMMAND += f"--l2_assoc={l2_assoc} "
    if do_prefetch:
        COMMAND += f"--l2-hwp-type={l2_hwp_type} "
        if l2_hwp_type == "DiffMatchingPrefetcher":
            COMMAND += f"--dmp-notify l1 "
    COMMAND += f"--l2_mshrs={l2_mshrs} "
    COMMAND += f"--l3cache "
    COMMAND += f"--l3_size={l3_size} "
    COMMAND += f"--l3_assoc={l3_assoc} "
    COMMAND += f"--l3_mshrs={l3_mshrs} "
    COMMAND += "--cacheline_size=64 "
    COMMAND += f"--mem-type {mem_type} "
    COMMAND += f"--ramulator-config {ramulator_config} "
    COMMAND += f"--mem-channels {mem_channels} "
    if have_maa or maa_warmer:
        COMMAND += "--maa "
        COMMAND += f"--maa_num_tile_elements {tile_size} "
        COMMAND += "--maa_l2_uncacheable "
        COMMAND += "--maa_l3_uncacheable "
        COMMAND += "--maa_num_spd_read_ports 4 "
        COMMAND += "--maa_num_spd_write_ports 4 "
        COMMAND += "--maa_num_ALU_lanes 16 "
        # if reconfigurable_RT:
        #     COMMAND += "--maa_reconfigure_row_table "
        # else:
        #     COMMAND += "--maa_num_initial_row_table_slices 4 "
        COMMAND += "--maa_num_initial_row_table_slices 32 "
        if do_reorder == False:
            COMMAND += "--maa_no_reorder "
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


# all_tiles = [1024, 2048, 4096, 8192, 16384]
# all_tiles_str = ["1K", "2K", "4K", "8K", "16K"]
# all_sizes = [200000]
# all_sizes_str = ["200K"] #, "2M"]
# all_distances = [256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304]
# all_distances_str = ["256", "1K", "4K", "16K", "64K", "256K", "1M", "4M"]
# all_kernels =   ["gather",
#                 "scatter",
#                 "rmw",
#                 "gather_scatter",
#                 "gather_rmw",
#                 "gather_rmw_cond",
#                 "gather_rmw_directrangeloop_cond",
#                 "gather_rmw_indirectrangeloop_cond",
#                 "gather_rmw_cond_indirectrangeloop_cond",
#                 "gather_rmw_indirectcond_indirectrangeloop_indirectcond"]

# for kernel in all_kernels:
#     checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/random_distance",
#                                             command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_T16K.o",
#                                             options=f"131072 2048 CMP {kernel}",
#                                             num_cores=4)
#     add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/random_distance",
#                     checkpoint=f"{CPT_DIR}/{kernel}/random_distance",
#                     checkpoint_id = checkpoint_id,
#                     command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_T16K.o",
#                     options=f"131072 2048 CMP {kernel}",
#                     mode="MAA",
#                     num_cores=4)




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

for kernel in ["gather_rmw_dst", "rmw", "scatter", "gather", "gather_full"]:
    for mode in ["MAA", "MAANO"]:
        new_mode = "MAA" if mode == "MAANO" else mode
        checkpoint_id = None
        checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/random/256K_{mode}",
                                                command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_T16K.o",
                                                options=f"262144 {new_mode} {kernel} random 32768", num_cores=4)
        add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/random/reorder/256K_{mode}",
                            checkpoint=f"{CPT_DIR}/{kernel}/random/256K_{mode}",
                            checkpoint_id = checkpoint_id,
                            command=f"{GEM5_DIR}/tests/test-progs/MAA/CISC/test_T16K.o",
                            options=f"262144 {new_mode} {kernel} random 32768",
                            mode=new_mode,
                            num_cores=4,
                            do_reorder=False if mode == "MAANO" else True)

########################################## NAS ##########################################
# checkpoint_id = None
# checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/cga/MAA",
#                                         command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/NPB3.4-OMP/CG_CPP/B.16384",
#                                         options=f"MAA")
# add_command_run_MAA(directory=f"{RSLT_DIR}/cga/MAA",
#                     checkpoint=f"{CPT_DIR}/cga/MAA",
#                     checkpoint_id = checkpoint_id,
#                     command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/NAS/NPB3.4-OMP/CG_CPP/B.16384",
#                     options=f"MAA",
#                     mode="MAA")

# all_kernels = ["isb", "cgb"] # , "isc", "cgb", "cgc"] #, "isa", "isb"]
                 
# all_test_dirs = {"cga": "NAS/NPB3.4-OMP/CG_CPP",
#                  "cgb": "NAS/NPB3.4-OMP/CG_CPP",
#                  "cgc": "NAS/NPB3.4-OMP/CG_CPP",
#                  "isa": "NAS/NPB3.4-OMP/IS",
#                  "isb": "NAS/NPB3.4-OMP/IS",
#                  "isc": "NAS/NPB3.4-OMP/IS"}

# all_file_names = {"cga": "A.16384",
#                  "cgb": "B.16384",
#                  "cgc": "C.16384",
#                  "isa": "A.16384",
#                  "isb": "B.16384",
#                  "isc": "C.16384"}

# all_modes = ["BASE", "DMP", "MAA", "MAANO"] # , "BASE"] #"MAA",  ["BASE", "DMP"] # ["BASE", "MAA", "DMP"]

# for kernel in all_kernels:
#     for mode in all_modes:
#         test_dir = all_test_dirs[kernel]
#         file_name = all_file_names[kernel]
#         checkpoint_id = None
#         option = "BASE" if mode == "DMP" else "MAA" if mode == "MAANO" else mode
#         checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}",
#                                                 command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/{test_dir}/{file_name}",
#                                                 options=option)
#         add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}",
#                             checkpoint=f"{CPT_DIR}/{kernel}/{mode}",
#                             checkpoint_id = checkpoint_id,
#                             command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/{test_dir}/{file_name}",
#                             options=option,
#                             mode="MAA" if mode == "MAANO" else mode,
#                             do_reorder=False if mode == "MAANO" else True)
        
########################################## GAPB ##########################################

# add_command_run_MAA(directory=f"{RSLT_DIR}/pr/MAANO/22",
#                                 checkpoint=f"{CPT_DIR}/pr/MAANO/22",
#                                 checkpoint_id = None,
#                                 command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/pr_maa",
#                                 options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_22.sg -l -n 1",
#                                 mode="MAA",
#                                 do_reorder=False)

# all_modes = ["MAA", "BASE", "DMP", "MAANO"]
# all_kernels = ["bfs", "pr", "bc", "sssp"]
# for kernel in all_kernels:
#     for mode in all_modes:
#         for size in [22]:
#             file_name = f"{kernel}_maa" if (mode == "MAA" or mode == "MAANO") else f"{kernel}"
#             graph_ext = "wsg" if kernel == "sssp" else "sg"
#             checkpoint_id = None
#             checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size}",
#                                                     command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
#                                                     options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1")
#             add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size}",
#                                 checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size}",
#                                 checkpoint_id = checkpoint_id,
#                                 command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/{file_name}",
#                                 options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/gapbs/serialized_graph_{size}.{graph_ext} -l -n 1",
#                                 mode="MAA" if mode == "MAANO" else mode,
#                                 do_reorder=False if mode == "MAANO" else True)

# ########################################## SPATTER ##########################################
# add_command_run_MAA(directory=f"{RSLT_DIR}/spatter/flag/MAA",
#                     checkpoint=f"{CPT_DIR}/spatter/flag/MAA",
#                     checkpoint_id = None,
#                     command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/spatter_maa",
#                     options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/flag/all.json",
#                     mode="MAA")

# all_modes = ["BASE", "MAA", "DMP", "MAANO"]
# all_kernels = ["xrage", "flag"]
# for kernel in all_kernels:
#     for mode in all_modes:
#         file_name = None
#         if mode == "BASE" or mode == "DMP":
#             file_name = f"spatter_base"
#         elif mode == "MAA" or mode == "MAANO":
#             file_name = f"spatter_maa"
        
#         checkpoint_id = None
#         checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/spatter/{kernel}/{mode}",
#                                                 command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
#                                                 options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json")
#         add_command_run_MAA(directory=f"{RSLT_DIR}/spatter/{kernel}/{mode}",
#                             checkpoint=f"{CPT_DIR}/spatter/{kernel}/{mode}",
#                             checkpoint_id = checkpoint_id,
#                             command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/build/{file_name}",
#                             options=f"-f {GEM5_DIR}/tests/test-progs/MAABenchmarks/spatter/tests/test-data/{kernel}/all.json",
#                             mode="MAA" if mode == "MAANO" else mode,
#                             do_reorder=False if mode == "MAANO" else True)

# ######################################### HASHJOIN ##########################################

# checkpoint_id = None
# checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/PRO/MAA",
#                                         command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/src/bin/x86/hj_maa",
#                                         options=f"-a PRO -n 4 -r 2000000 -s 2000000",
#                                         num_cores=5)
# add_command_run_MAA(directory=f"{RSLT_DIR}/PRO/MAA",
#                     checkpoint=f"{CPT_DIR}/PRO/MAA",
#                     checkpoint_id = checkpoint_id,
#                     command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/src/bin/x86/hj_maa",
#                     options=f"-a PRO -n 4 -r 2000000 -s 2000000",
#                     mode="MAA",
#                     num_cores=5)


# os.system(f"cp {GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/relR_2M.dat ./")
# os.system(f"cp {GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/relS_2M.dat ./")
# os.system(f"cp {GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/relR_8M.dat ./")
# os.system(f"cp {GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/relS_8M.dat ./")

all_modes = ["MAA", "MAANO"] # ["MAA", "BASE", "DMP", "MAANO"]
all_kernels = ["PRH" , "PRO"]
all_sizes = [2000000] # , 8000000]
all_sizes_str = ["2M"] # , "8M"]
for kernel in all_kernels:
    for mode in all_modes:
        for size, size_str in zip(all_sizes, all_sizes_str):
            file_name = None
            if mode == "BASE":
                file_name = f"hj_base"
            if mode == "DMP":
                file_name = f"hj_base"
            if mode == "MAA" or mode == "MAANO":
                file_name = f"hj_maa"
            
            checkpoint_id = None
            checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                                    command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/src/bin/x86/{file_name}",
                                                    options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                                    num_cores=5)
            add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
                                checkpoint_id = checkpoint_id,
                                command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/hashjoin-ph-2/src/bin/x86/{file_name}",
                                options=f"-a {kernel} -n 4 -r {size} -s {size}",
                                mode="MAA" if mode == "MAANO" else mode,
                                do_reorder=False if mode == "MAANO" else True,
                                num_cores=5)

########################################## UME ##########################################

# all_modes = ["BASE", "DMP", "MAA", "MAANO"]
# all_kernels = ["gradzatp", "gradzatz", "gradzatz_invert", "gradzatp_invert"]
# all_sizes = [2000000] #, 8000000]
# all_sizes_str =  ["2M"] #, "8M"]
# for kernel in all_kernels:
#     for mode in all_modes:
#         for size, size_str in zip(all_sizes, all_sizes_str):
#             file_name = f"{kernel}_maa" if (mode == "MAA" or mode == "MAANO") else f"{kernel}_base"
            
#             checkpoint_id = None
#             checkpoint_id = add_command_checkpoint(directory=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
#                                                     command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
#                                                     options=f"{size}")
#             add_command_run_MAA(directory=f"{RSLT_DIR}/{kernel}/{mode}/{size_str}",
#                                 checkpoint=f"{CPT_DIR}/{kernel}/{mode}/{size_str}",
#                                 checkpoint_id = checkpoint_id,
#                                 command=f"{GEM5_DIR}/tests/test-progs/MAABenchmarks/UME/{file_name}",
#                                 options=f"{size}",
#                                 mode="MAA" if mode == "MAANO" else mode,
#                                 do_reorder=False if mode == "MAANO" else True)

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

if parallelism != 0:
    threads = []
    for i in range(parallelism):
        threads.append(Thread(target=workerthread, args=(i,)))
    
    for i in range(parallelism):
        threads[i].start()
    
    for i in range(parallelism):
        print("T[M]: Waiting for T[{}] to join!".format(i))
        threads[i].join()