import os
import yaml  # YAML parsing provided with the PyYAML package
from threading import Thread, Lock

baseline_config_file = "./example_config_o3.yaml"
trace_location = "/data/arkhadem/UME_MEM/"
experiment_name = "ramulator_o3"

parallelism = 64

base_config = None
with open(baseline_config_file, 'r') as f:
    base_config = yaml.safe_load(f)

print(str(base_config))

files = {
        # "gradzatp_invert::loop1::zone_field": 54,
        # "gradzatp_invert::loop1::corner_volume": 54,
        # "gradzatp_invert::loop1::csurf": 54,
        # "gradzatp_invert::loop1::c_to_z_map": 54,
        # "gradzatp_invert::loop1::point_gradient": 54,
        # "gradzatp_invert::loop1::point_volume": 54,
        # "gradzatp_invert::loop1::p_to_c_map": 54,
        "gradzatp_invert::loop1": 54 / 7,

        # "gradzatp_invert::loop2::point_gradient": 59,
        # "gradzatp_invert::loop2::point_normal": 59,
        # "gradzatp_invert::loop2::point_type": 59,
        # "gradzatp_invert::loop2::point_volume": 59,
        "gradzatp_invert::loop2": 59 / 4,

        # "gradzatp::loop1::corner_type": 63,
        # "gradzatp::loop1::corner_volume": 63,
        # "gradzatp::loop1::csurf": 63,
        # "gradzatp::loop1::c_to_p_map": 63,
        # "gradzatp::loop1::c_to_z_map": 63,
        # "gradzatp::loop1::point_gradient": 63,
        # "gradzatp::loop1::point_volume": 63,
        # "gradzatp::loop1::zone_field": 63,
        "gradzatp::loop1": 63 / 8,

        # "gradzatp::loop2::point_gradient": 59,
        # "gradzatp::loop2::point_normal": 59,
        # "gradzatp::loop2::point_type": 59,
        # "gradzatp::loop2::point_volume": 59,
        "gradzatp::loop2": 59 / 4,

        # "gradzatz_invert::loop2::corner_volume": 39,
        # "gradzatz_invert::loop2::c_to_p_map": 39,
        # "gradzatz_invert::loop2::point_gradient": 39,
        # "gradzatz_invert::loop2::zone_gradient": 39,
        # "gradzatz_invert::loop2::zone_type": 39,
        # "gradzatz_invert::loop2::z_to_c_map": 39,
        "gradzatz_invert::loop2": 39 / 6,

        # "gradzatz::loop1::corner_type": 20,
        # "gradzatz::loop1::corner_volume": 20,
        # "gradzatz::loop1::c_to_z_map": 20,
        # "gradzatz::loop1::zone_volume": 20,
        "gradzatz::loop1": 20 / 4,

        # "gradzatz::loop2::corner_type": 56,
        # "gradzatz::loop2::corner_volume": 56,
        # "gradzatz::loop2::c_to_p_map": 56,
        # "gradzatz::loop2::c_to_z_map": 56,
        # "gradzatz::loop2::point_gradient": 56,
        # "gradzatz::loop2::zone_gradient": 56,
        # "gradzatz::loop2::zone_volume": 56,
        "gradzatz::loop2": 56 / 7}

tasks = []
lock = Lock()

def workerthread(my_tid):
    global lock
    global tasks
    task = None
    while True:
        with lock:
            if len(tasks) == 0:
                task = None
            else:
                task = tasks.pop(0)
        if task == None:
            print("T[{}]: tasks finished, bye!".format(my_tid))
            break
        else:
            print("T[{}]: executing a new task: {}".format(my_tid, task))
            os.system(task)
            print("T[{}]: finished this task: {}".format(my_tid, task))

preset = base_config["MemorySystem"]["DRAM"]["org"]["preset"]
channel = base_config["MemorySystem"]["DRAM"]["org"]["channel"]
rank = base_config["MemorySystem"]["DRAM"]["org"]["rank"]

def task_adder(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size):
    global tasks
    global base_config
    base_config['Frontend']['traces'] = [trace_location + file + ".txt"]
    base_config['Frontend']['bubble_count'] = bubble_count
    base_config['Frontend']['inst_window_depth'] = inst_window_depth
    base_config['Frontend']['llc_num_mshr_per_core'] = llc_num_mshr_per_core
    base_config["MemorySystem"]["Controller"]["queue_size"] = queue_size
    log_location = f"{trace_location}{experiment_name}/{preset}/{channel}/{rank}/q{queue_size}/b{bubble_count}/i{inst_window_depth}/m{llc_num_mshr_per_core}"
    os.system(f"mkdir -p {log_location} > /dev/null 2>&1")
    task = f"./build/ramulator2 -c \"{str(base_config)}\" > {log_location}/{file}.txt 2>&1"
    assert task not in tasks, f"Task {task} already exists!"
    tasks.append(task)

# realistic
for file in files.keys():
    bubble_count = int(files[file])
    inst_window_depth = 128
    llc_num_mshr_per_core = 16
    queue_size = 32
    task_adder(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size)

# realistic - no bubble
for file in files.keys():
    bubble_count = 0
    inst_window_depth = 128
    llc_num_mshr_per_core = 16
    queue_size = 32
    task_adder(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size)

# unlimited everything
for file in files.keys():
    bubble_count = int(files[file])
    for queue_size in [32, 64, 128, 256, 512, 1024, 4096, 16384]:
        inst_window_depth = max(128, (bubble_count + 1) * queue_size)
        llc_num_mshr_per_core = max(16, queue_size)
        task_adder(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size)

# unlimited everything - no bubble
for file in files.keys():
    bubble_count = 0
    for queue_size in [32, 64, 128, 256, 512, 1024, 4096, 16384]:
        inst_window_depth = max(128, (bubble_count + 1) * queue_size)
        llc_num_mshr_per_core = max(16, queue_size)
        task_adder(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size)

if parallelism != 0:
    threads = []
    for i in range(parallelism):
        threads.append(Thread(target=workerthread, args=(i,)))
    
    for i in range(parallelism):
        threads[i].start()
    
    for i in range(parallelism):
        print("T[M]: Waiting for T[{}] to join!".format(i))
        threads[i].join()