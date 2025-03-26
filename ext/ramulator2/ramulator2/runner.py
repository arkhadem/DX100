import os
import yaml  # YAML parsing provided with the PyYAML package
from threading import Thread, Lock

baseline_config_file = "./example_config.yaml"

parallelism = 64

base_config = None
with open(baseline_config_file, 'r') as f:
    base_config = yaml.safe_load(f)

print(str(base_config))

file_location = "/data/arkhadem/UME_MEM/"

files = ["gradzatp_invert::loop1::corner_volume",
        "gradzatp_invert::loop1::csurf",
        "gradzatp_invert::loop1::c_to_z_map",
        "gradzatp_invert::loop1::point_gradient",
        "gradzatp_invert::loop1::point_volume",
        "gradzatp_invert::loop1::p_to_c_map",
        "gradzatp_invert::loop1",
        "gradzatp_invert::loop1::zone_field",
        "gradzatp_invert::loop2::point_gradient",
        "gradzatp_invert::loop2::point_normal",
        "gradzatp_invert::loop2::point_type",
        "gradzatp_invert::loop2::point_volume",
        "gradzatp_invert::loop2",
        "gradzatp::loop1::corner_type",
        "gradzatp::loop1::corner_volume",
        "gradzatp::loop1::csurf",
        "gradzatp::loop1::c_to_p_map",
        "gradzatp::loop1::c_to_z_map",
        "gradzatp::loop1::point_gradient",
        "gradzatp::loop1::point_volume",
        "gradzatp::loop1",
        "gradzatp::loop1::zone_field",
        "gradzatp::loop2::point_gradient",
        "gradzatp::loop2::point_normal",
        "gradzatp::loop2::point_type",
        "gradzatp::loop2::point_volume",
        "gradzatp::loop2",
        "gradzatz_invert::loop2::corner_volume",
        "gradzatz_invert::loop2::c_to_p_map",
        "gradzatz_invert::loop2::point_gradient",
        "gradzatz_invert::loop2",
        "gradzatz_invert::loop2::zone_gradient",
        "gradzatz_invert::loop2::zone_type",
        "gradzatz_invert::loop2::z_to_c_map",
        "gradzatz::loop1::corner_type",
        "gradzatz::loop1::corner_volume",
        "gradzatz::loop1::c_to_z_map",
        "gradzatz::loop1",
        "gradzatz::loop1::zone_volume",
        "gradzatz::loop2::corner_type",
        "gradzatz::loop2::corner_volume",
        "gradzatz::loop2::c_to_p_map",
        "gradzatz::loop2::c_to_z_map",
        "gradzatz::loop2::point_gradient",
        "gradzatz::loop2",
        "gradzatz::loop2::zone_gradient",
        "gradzatz::loop2::zone_volume"]

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

preset = base_config["MemorySystem"]["DRAM"]["org"]["preset"]
channel = base_config["MemorySystem"]["DRAM"]["org"]["channel"]
rank = base_config["MemorySystem"]["DRAM"]["org"]["rank"]

# for file in files:
#     base_config["Frontend"]["path"] = file_location + file + ".txt"
#     base_config["Frontend"]["coalesce_trace"] = 16384
#     base_config["Frontend"]["sort_trace"] = -1
#     base_config["Frontend"]["coalesce_sort_trace"] = -1
#     for queue_size in [32, 64, 128, 256, 512, 1024, 4096, 16384]:
#         base_config["MemorySystem"]["Controller"]["queue_size"] = queue_size
#         location = f"{file_location}ramulator_CPU/{preset}/{channel}/{rank}/{queue_size}"
#         os.system(f"mkdir -p {location} > /dev/null 2>&1")
#         tasks.append(f"./build/ramulator2 -c \"{str(base_config)}\" > {location}/{file}.txt 2>&1")

for file in files:
    base_config["Frontend"]["path"] = file_location + file + ".txt"
    base_config["Frontend"]["coalesce_trace"] = -1
    base_config["Frontend"]["sort_trace"] = -1
    base_config["MemorySystem"]["Controller"]["queue_size"] = 32
    for interleave_trace in [-1, 1]:
        base_config["Frontend"]["interleave_trace"] = interleave_trace
        for coalesce_sort_trace in [-1, 256, 512, 1024, 2048, 4096, 8192, 16384]:
            base_config["Frontend"]["coalesce_sort_trace"] = coalesce_sort_trace
            location = f"{file_location}ramulator_MAA/{preset}/{channel}/{rank}/{coalesce_sort_trace}/{interleave_trace}"
            os.system(f"mkdir -p {location} > /dev/null 2>&1")
            tasks.append(f"./build/ramulator2 -c \"{str(base_config)}\" > {location}/{file}.txt 2>&1")

if parallelism != 0:
    threads = []
    for i in range(parallelism):
        threads.append(Thread(target=workerthread, args=(i,)))
    
    for i in range(parallelism):
        threads[i].start()
    
    for i in range(parallelism):
        print("T[M]: Waiting for T[{}] to join!".format(i))
        threads[i].join()