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

stats = {}

preset = base_config["MemorySystem"]["DRAM"]["org"]["preset"]
channel = base_config["MemorySystem"]["DRAM"]["org"]["channel"]
rank = base_config["MemorySystem"]["DRAM"]["org"]["rank"]

commands = ["REFab", "WRA", "RDA", "WR", "RD", "PREA", "PRE", "ACT"]

def processor(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size):
    global stats
    location = f"{trace_location}{experiment_name}/{preset}/{channel}/{rank}/q{queue_size}/b{bubble_count}/i{inst_window_depth}/m{llc_num_mshr_per_core}/{file}.txt"
    os.system(f"tail -n 50 {location} > tmp.txt 2>&1")
    lines = open("tmp.txt").readlines()
    stat = {}
    for line in lines:
        keyword = None
        if "total_num_read_requests:" in line:
            keyword = "DRAM_RD_REQ"
        if "memory_system_cycles" in line:
            keyword = "cycles"
        if "llc_read_access" in line:
            keyword = "MEM_RD_REQ"
        if "max_queue_occupancy" in line:
            keyword = "max_occupancy"
        if "avg_queue_occupancy" in line:
            keyword = "avg_occupancy"
        for command in commands:
            if f"_num_{command}_commands:" in line:
                keyword = command + "_CMD"
        if keyword != None:
            words = line.split(" ")
            while "" in words:
                words.remove("")
            val = float(words[1].rstrip().lstrip())
            if keyword not in stat:
                stat[keyword] = val
            else:
                stat[keyword] += val
    stat["DRAM_BW"] = float(stat["DRAM_RD_REQ"] * 64) * 1.6 / float(stat["cycles"])
    stat["MEM_BW"] = float(stat["MEM_RD_REQ"] * 64) * 1.6 / float(stat["cycles"])

    if file not in stats:
        stats[file] = {}
    if bubble_count not in stats[file]:
        stats[file][bubble_count] = {}
    if inst_window_depth not in stats[file][bubble_count]:
        stats[file][bubble_count][inst_window_depth] = {}
    if llc_num_mshr_per_core not in stats[file][bubble_count][inst_window_depth]:
        stats[file][bubble_count][inst_window_depth][llc_num_mshr_per_core] = {}
    if queue_size not in stats[file][bubble_count][inst_window_depth][llc_num_mshr_per_core]:
        stats[file][bubble_count][inst_window_depth][llc_num_mshr_per_core][queue_size] = {}
    stats[file][bubble_count][inst_window_depth][llc_num_mshr_per_core][queue_size] = stat

# realistic
for file in files.keys():
    bubble_count = int(files[file])
    inst_window_depth = 128
    llc_num_mshr_per_core = 16
    queue_size = 32
    processor(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size)

# realistic - no bubble
for file in files.keys():
    bubble_count = 0
    inst_window_depth = 128
    llc_num_mshr_per_core = 16
    queue_size = 32
    processor(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size)

# unlimited everything
for file in files.keys():
    bubble_count = int(files[file])
    for queue_size in [32, 64, 128, 256, 512, 1024, 4096, 16384]:
        inst_window_depth = max(128, bubble_count * queue_size)
        llc_num_mshr_per_core = max(16, queue_size)
        processor(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size)

# unlimited everything - no bubble
for file in files.keys():
    bubble_count = 0
    for queue_size in [32, 64, 128, 256, 512, 1024, 4096, 16384]:
        inst_window_depth = max(128, (bubble_count + 1) * queue_size)
        llc_num_mshr_per_core = max(16, queue_size)
        processor(file, bubble_count, inst_window_depth, llc_num_mshr_per_core, queue_size)

print("ROI,Bubble,ROB,MSHR,queue,MEM_RD_REQ,DRAM_RD_REQ,cycles,max_occupancy,avg_occupancy,DRAM_BW,MEM_BW", end="")
for command in commands:
    print(f",{command}_CMD", end="")
print()

for file in stats.keys():
    for bubble_count in stats[file].keys():
        for inst_window_depth in stats[file][bubble_count].keys():
            for llc_num_mshr_per_core in stats[file][bubble_count][inst_window_depth].keys():
                for queue_size in stats[file][bubble_count][inst_window_depth][llc_num_mshr_per_core].keys():
                    stat = stats[file][bubble_count][inst_window_depth][llc_num_mshr_per_core][queue_size]
                    print(f"{file},{bubble_count},{inst_window_depth},{llc_num_mshr_per_core},{queue_size},{stat['MEM_RD_REQ']},{stat['DRAM_RD_REQ']},{stat['cycles']},{stat['max_occupancy']},{stat['avg_occupancy']},{stat['DRAM_BW']},{stat['MEM_BW']}", end="")
                    for command in commands:
                        print(f",{stat[command + '_CMD']}", end="")
                    print()
