import os
import yaml  # YAML parsing provided with the PyYAML package
from threading import Thread, Lock

baseline_config_file = "./example_config.yaml"

parallelism = 64

base_config = None
with open(baseline_config_file, 'r') as f:
    base_config = yaml.safe_load(f)

file_location = "/data/arkhadem/UME_MEM/"

files = [
        {"func": "gradzatp", "ROI": "loop1", "access": "corner_type", "type": "streaming"},
        {"func": "gradzatp", "ROI": "loop1", "access": "corner_volume", "type": "streaming"},
        {"func": "gradzatp", "ROI": "loop1", "access": "csurf", "type": "streaming"},
        {"func": "gradzatp", "ROI": "loop1", "access": "c_to_p_map", "type": "streaming"},
        {"func": "gradzatp", "ROI": "loop1", "access": "c_to_z_map", "type": "streaming"},
        {"func": "gradzatp", "ROI": "loop1", "access": "point_gradient", "type": "indirect"},
        {"func": "gradzatp", "ROI": "loop1", "access": "point_volume", "type": "indirect"},
        {"func": "gradzatp", "ROI": "loop1", "access": "zone_field", "type": "indirect"},
        {"func": "gradzatp", "ROI": "loop1", "access": "total"},
        
        {"func": "gradzatp", "ROI": "loop2", "access": "point_gradient", "type": "streaming"},
        {"func": "gradzatp", "ROI": "loop2", "access": "point_normal", "type": "streaming"},
        {"func": "gradzatp", "ROI": "loop2", "access": "point_type", "type": "streaming"},
        {"func": "gradzatp", "ROI": "loop2", "access": "point_volume", "type": "streaming"},
        {"func": "gradzatp", "ROI": "loop2", "access": "total"},

        {"func": "gradzatp_invert", "ROI": "loop1", "access": "point_gradient", "type": "streaming"},
        {"func": "gradzatp_invert", "ROI": "loop1", "access": "point_volume", "type": "streaming"},
        {"func": "gradzatp_invert", "ROI": "loop1", "access": "p_to_c_map", "type": "streaming"},
        {"func": "gradzatp_invert", "ROI": "loop1", "access": "corner_volume", "type": "indirect"},
        {"func": "gradzatp_invert", "ROI": "loop1", "access": "csurf", "type": "indirect"},
        {"func": "gradzatp_invert", "ROI": "loop1", "access": "c_to_z_map", "type": "indirect"},
        {"func": "gradzatp_invert", "ROI": "loop1", "access": "zone_field", "type": "indirect"},
        {"func": "gradzatp_invert", "ROI": "loop1", "access": "total"},

        {"func": "gradzatp_invert", "ROI": "loop2", "access": "point_gradient", "type": "streaming"},
        {"func": "gradzatp_invert", "ROI": "loop2", "access": "point_normal", "type": "streaming"},
        {"func": "gradzatp_invert", "ROI": "loop2", "access": "point_type", "type": "streaming"},
        {"func": "gradzatp_invert", "ROI": "loop2", "access": "point_volume", "type": "streaming"},
        {"func": "gradzatp_invert", "ROI": "loop2", "access": "total"},

        {"func": "gradzatz", "ROI": "loop1", "access": "corner_type", "type": "streaming"},
        {"func": "gradzatz", "ROI": "loop1", "access": "corner_volume", "type": "streaming"},
        {"func": "gradzatz", "ROI": "loop1", "access": "c_to_z_map", "type": "streaming"},
        {"func": "gradzatz", "ROI": "loop1", "access": "zone_volume", "type": "indirect"},
        {"func": "gradzatz", "ROI": "loop1", "access": "total"},

        {"func": "gradzatz", "ROI": "loop2", "access": "corner_type", "type": "streaming"},
        {"func": "gradzatz", "ROI": "loop2", "access": "corner_volume", "type": "streaming"},
        {"func": "gradzatz", "ROI": "loop2", "access": "c_to_p_map", "type": "streaming"},
        {"func": "gradzatz", "ROI": "loop2", "access": "c_to_z_map", "type": "streaming"},
        {"func": "gradzatz", "ROI": "loop2", "access": "zone_gradient", "type": "indirect"},
        {"func": "gradzatz", "ROI": "loop2", "access": "zone_volume", "type": "indirect"},
        {"func": "gradzatz", "ROI": "loop2", "access": "point_gradient", "type": "indirect"},
        {"func": "gradzatz", "ROI": "loop2", "access": "total"},

        {"func": "gradzatz_invert", "ROI": "loop2", "access": "zone_gradient", "type": "streaming"},
        {"func": "gradzatz_invert", "ROI": "loop2", "access": "zone_type", "type": "streaming"},
        {"func": "gradzatz_invert", "ROI": "loop2", "access": "z_to_c_map", "type": "streaming"},
        {"func": "gradzatz_invert", "ROI": "loop2", "access": "corner_volume", "type": "indirect"},
        {"func": "gradzatz_invert", "ROI": "loop2", "access": "c_to_p_map", "type": "indirect"},
        {"func": "gradzatz_invert", "ROI": "loop2", "access": "point_gradient", "type": "indirect"},
        {"func": "gradzatz_invert", "ROI": "loop2", "access": "total"}
        ]

commands = ["REFab", "WRA", "RDA", "WR", "RD", "PREA", "PRE", "ACT"]

preset = base_config["MemorySystem"]["DRAM"]["org"]["preset"]
channel = base_config["MemorySystem"]["DRAM"]["org"]["channel"]
rank = base_config["MemorySystem"]["DRAM"]["org"]["rank"]
# queue_sizes = [32, 64, 128, 256, 512, 1024, 4096, 16384]
queue_sizes = [128, 256, 512, 1024, 2048, 4096, 8192, 16384]
experiment = "ramulator_MAA"

stats = {}

def processor(location):
    os.system(f"tail -n 50 {location} > tmp.txt 2>&1")
    lines = open("tmp.txt").readlines()
    stat = {}
    for line in lines:
        keyword = None
        if "total_num_read_requests:" in line:
            keyword = "RD_REQ"
        if "memory_system_cycles" in line:
            keyword = "cycles"
        for command in commands:
            if f"_num_{command}_commands:" in line:
                keyword = command + "_CMD"
        if keyword != None:
            words = line.split(" ")
            while "" in words:
                words.remove("")
            val = int(words[1].rstrip().lstrip())
            if keyword not in stat:
                stat[keyword] = val
            else:
                stat[keyword] += val
    return stat

for file in files:
    func = file["func"]
    ROI = file["ROI"]
    access = file["access"]
    if func not in stats:
        stats[func] = {}
    if ROI not in stats[func]:
        stats[func][ROI] = {}    
    for queue_size in queue_sizes:
        if queue_size not in stats[func][ROI]:
            stats[func][ROI][queue_size] = {}
        if access not in stats[func][ROI][queue_size]:
            stats[func][ROI][queue_size][access] = {}
        if "streaming" not in stats[func][ROI][queue_size]:
            stats[func][ROI][queue_size]["streaming"] = {}
        if "indirect" not in stats[func][ROI][queue_size]:
            stats[func][ROI][queue_size]["indirect"] = {}
        location = f"{file_location}{experiment}/{preset}/{channel}/{rank}/{queue_size}/{func}::{ROI}"
        if access != "total":
            location += f"::{access}"
        location += ".txt"
        stat = processor(location)
        stats[func][ROI][queue_size][access] = stat
        if access != "total":
            for keyword in stat.keys():
                if keyword not in stats[func][ROI][queue_size][file["type"]]:
                    stats[func][ROI][queue_size][file["type"]][keyword] = stat[keyword]
                else:
                    stats[func][ROI][queue_size][file["type"]][keyword] += stat[keyword]
        

print("func,ROI,access,type,queue,RD_REQ,cycles", end="")
for command in commands:
    print(f",{command}_CMD", end="")
print()

for file in files:
    func = file["func"]
    ROI = file["ROI"]
    access = file["access"]
    if access == "total":
        continue
    for queue_size in queue_sizes:
        print(f"{func},{ROI},{access},{file['type']},{queue_size}", end="")
        if 'RD_REQ' not in stats[func][ROI][queue_size][access]:
            print(",0,0", end="")
            for command in commands:
                print(",0", end="")
        else:
            print(f",{stats[func][ROI][queue_size][access]['RD_REQ']},{stats[func][ROI][queue_size][access]['cycles']}", end="")
            for command in commands:
                print(f",{stats[func][ROI][queue_size][access][command + '_CMD']}", end="")
        print()

print(",\n,\n,\n,")

for file in files:
    func = file["func"]
    ROI = file["ROI"]
    access = file["access"]
    if access != "total":
        continue
    for queue_size in queue_sizes:
        for access_type in ["streaming", "indirect", "total"]:
            print(f"{func},{ROI},total,{access_type},{queue_size}", end="")
            if 'RD_REQ' not in stats[func][ROI][queue_size][access_type]:
                print(",0,0", end="")
                for command in commands:
                    print(",0", end="")
            else:
                print(f",{stats[func][ROI][queue_size][access_type]['RD_REQ']},{stats[func][ROI][queue_size][access_type]['cycles']}", end="")
                for command in commands:
                    print(f",{stats[func][ROI][queue_size][access_type][command + '_CMD']}", end="")
            print()