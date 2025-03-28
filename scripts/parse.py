import os
import argparse
from MCPAT_converter import E2EConvertor
import subprocess
import matplotlib.pyplot as plt
import statistics

# P_PRE_STBY = 54.9
# P_ACT_STBY = 75.0
# P_ACT = 25.1
# P_RD = 150.4
# P_WR = 127.7
# P_DQ = 85.6

# x8 -- 3200 -- Rev B
# https://www.mouser.com/datasheet/2/671/Micron_05092023_8gb_ddr4_sdram-3175546.pdf?srsltid=AfmBOorxWdf6393VEklwSz6hS_D2snTlD1BFveSa1q4XOGWcUWcsct2i
# MAX PRE STBY CURRENT (mA)
IDD2N = 37.0
IPP2N = 3.0
# MAX ACT STBY CURRENT (mA)
IDD3N = 52.0
IPP3N = 3.0
# ACT/PRE BANK CURRENT (mA)
IDD0 = 57.0
IPP0 = 3.0
# MAX RD BURST CURRENT (mA)
IDD4R = 168.0
IPP4R = 3.0
# MAX WR BURST CURRENT (mA)
IDD4W = 150.0
IPP4W = 3.0
# DQ OUTPUT DRIVER POWER (mW)
PDSDQ = 85.6
VDD = 1.2
VPP = 2.5

GIGA = 1000000000.000

# clock time (ns)
tCK = 0.625
# PRE-ACT time (ns)
tRP = 20.0 * tCK
nRP = 20.0
# ACT-RD time (ns)
tRCD = 20.0 * tCK
nRCD = 20.0
# RD/WR time (ns)
tBL = 4.0 * tCK
nBL = 4.0
# ACT to PRE time (ns)
tRAS = 52.0 * tCK
nRAS = 52.0


CORE_FREQUENCY = 3.2 * GIGA

all_maa_cycles = ["INDRD", "INDWR", "INDRMW", "STRRD", "RANGE", "ALUS", "ALUV", "INV", "IDLE", "Total"]

all_maa_indirect_cycles = ["Fill", "Drain", "Build", "Request"]
all_cache_stats = {"Avg-SPD-Latency": {"PRE": "system.switch_cpus", "POST": ".lsq0.loadToUse_0::mean"},
                   "L1-SPD-Hits": {"PRE": "dcache.demandHits_0::switch_cpus", "POST": ".data"},
                   "L1-SPD-Misses": {"PRE": "dcache.demandMisses_0::switch_cpus", "POST": ".data"},
                   "M1-SPD-Hits": {"PRE": "dcache.demandMshrHits_0::switch_cpus", "POST": ".data"},
                   "M1-SPD-Misses": {"PRE": "dcache.demandMshrMisses_0::switch_cpus", "POST": ".data"},
                   "L2-SPD-Hits": {"PRE": "l2cache.demandHits_0::switch_cpus", "POST": ".data"},
                   "L2-SPD-Misses": {"PRE": "l2cache.demandMisses_0::switch_cpus", "POST": ".data"},
                   "M2-SPD-Hits": {"PRE": "l2cache.demandMshrHits_0::switch_cpus", "POST": ".data"},
                   "M2-SPD-Misses": {"PRE": "l2cache.demandMshrMisses_0::switch_cpus", "POST": ".data"},
                   "L3-SPD-Hits": {"PRE": "system.l3.demandHits_0::switch_cpus", "POST": ".data"},
                   "L3-SPD-Misses": {"PRE": "system.l3.demandMisses_0::switch_cpus", "POST": ".data"},
                   "M3-SPD-Hits": {"PRE": "system.l3.demandMshrHits_0::switch_cpus", "POST": ".data"},
                   "M3-SPD-Misses": {"PRE": "system.l3.demandMshrMisses_0::switch_cpus", "POST": ".data"},
                   "Avg-6-Latency": {"PRE": "system.switch_cpus", "POST": ".lsq0.loadToUse_6::mean"},
                   "L1-6-Hits": {"PRE": "dcache.demandHits_6::switch_cpus", "POST": ".data"},
                   "L1-6-Misses": {"PRE": "dcache.demandMisses_6::switch_cpus", "POST": ".data"},
                   "M1-6-Hits": {"PRE": "dcache.demandMshrHits_6::switch_cpus", "POST": ".data"},
                   "M1-6-Misses": {"PRE": "dcache.demandMshrMisses_6::switch_cpus", "POST": ".data"},
                   "L2-6-Hits": {"PRE": "l2cache.demandHits_6::switch_cpus", "POST": ".data"},
                   "L2-6-Misses": {"PRE": "l2cache.demandMisses_6::switch_cpus", "POST": ".data"},
                   "M2-6-Hits": {"PRE": "l2cache.demandMshrHits_6::switch_cpus", "POST": ".data"},
                   "M2-6-Misses": {"PRE": "l2cache.demandMshrMisses_6::switch_cpus", "POST": ".data"},
                   "L3-6-Hits": {"PRE": "system.l3.demandHits_6::switch_cpus", "POST": ".data"},
                   "L3-6-Misses": {"PRE": "system.l3.demandMisses_6::switch_cpus", "POST": ".data"},
                   "M3-6-Hits": {"PRE": "system.l3.demandMshrHits_6::switch_cpus", "POST": ".data"},
                   "M3-6-Misses": {"PRE": "system.l3.demandMshrMisses_6::switch_cpus", "POST": ".data"},
                   "Avg-7-Latency": {"PRE": "system.switch_cpus", "POST": ".lsq0.loadToUse_7::mean"},
                   "L1-7-Hits": {"PRE": "dcache.demandHits_7::switch_cpus", "POST": ".data"},
                   "L1-7-Misses": {"PRE": "dcache.demandMisses_7::switch_cpus", "POST": ".data"},
                   "M1-7-Hits": {"PRE": "dcache.demandMshrHits_7::switch_cpus", "POST": ".data"},
                   "M1-7-Misses": {"PRE": "dcache.demandMshrMisses_7::switch_cpus", "POST": ".data"},
                   "L2-7-Hits": {"PRE": "l2cache.demandHits_7::switch_cpus", "POST": ".data"},
                   "L2-7-Misses": {"PRE": "l2cache.demandMisses_7::switch_cpus", "POST": ".data"},
                   "M2-7-Hits": {"PRE": "l2cache.demandMshrHits_7::switch_cpus", "POST": ".data"},
                   "M2-7-Misses": {"PRE": "l2cache.demandMshrMisses_7::switch_cpus", "POST": ".data"},
                   "L3-7-Hits": {"PRE": "system.l3.demandHits_7::switch_cpus", "POST": ".data"},
                   "L3-7-Misses": {"PRE": "system.l3.demandMisses_7::switch_cpus", "POST": ".data"},
                   "M3-7-Hits": {"PRE": "system.l3.demandMshrHits_7::switch_cpus", "POST": ".data"},
                   "M3-7-Misses": {"PRE": "system.l3.demandMshrMisses_7::switch_cpus", "POST": ".data"},
                   "Avg-8-Latency": {"PRE": "system.switch_cpus", "POST": ".lsq0.loadToUse_8::mean"},
                   "L1-8-Hits": {"PRE": "dcache.demandHits_8::switch_cpus", "POST": ".data"},
                   "L1-8-Misses": {"PRE": "dcache.demandMisses_8::switch_cpus", "POST": ".data"},
                   "M1-8-Hits": {"PRE": "dcache.demandMshrHits_8::switch_cpus", "POST": ".data"},
                   "M1-8-Misses": {"PRE": "dcache.demandMshrMisses_8::switch_cpus", "POST": ".data"},
                   "L2-8-Hits": {"PRE": "l2cache.demandHits_8::switch_cpus", "POST": ".data"},
                   "L2-8-Misses": {"PRE": "l2cache.demandMisses_8::switch_cpus", "POST": ".data"},
                   "M2-8-Hits": {"PRE": "l2cache.demandMshrHits_8::switch_cpus", "POST": ".data"},
                   "M2-8-Misses": {"PRE": "l2cache.demandMshrMisses_8::switch_cpus", "POST": ".data"},
                   "L3-8-Hits": {"PRE": "system.l3.demandHits_8::switch_cpus", "POST": ".data"},
                   "L3-8-Misses": {"PRE": "system.l3.demandMisses_8::switch_cpus", "POST": ".data"},
                   "M3-8-Hits": {"PRE": "system.l3.demandMshrHits_8::switch_cpus", "POST": ".data"},
                   "M3-8-Misses": {"PRE": "system.l3.demandMshrMisses_8::switch_cpus", "POST": ".data"},
                   "Avg-9-Latency": {"PRE": "system.switch_cpus", "POST": ".lsq0.loadToUse_9::mean"},
                   "L1-9-Hits": {"PRE": "dcache.demandHits_9::switch_cpus", "POST": ".data"},
                   "L1-9-Misses": {"PRE": "dcache.demandMisses_9::switch_cpus", "POST": ".data"},
                   "M1-9-Hits": {"PRE": "dcache.demandMshrHits_9::switch_cpus", "POST": ".data"},
                   "M1-9-Misses": {"PRE": "dcache.demandMshrMisses_9::switch_cpus", "POST": ".data"},
                   "L2-9-Hits": {"PRE": "l2cache.demandHits_9::switch_cpus", "POST": ".data"},
                   "L2-9-Misses": {"PRE": "l2cache.demandMisses_9::switch_cpus", "POST": ".data"},
                   "M2-9-Hits": {"PRE": "l2cache.demandMshrHits_9::switch_cpus", "POST": ".data"},
                   "M2-9-Misses": {"PRE": "l2cache.demandMshrMisses_9::switch_cpus", "POST": ".data"},
                   "L3-9-Hits": {"PRE": "system.l3.demandHits_9::switch_cpus", "POST": ".data"},
                   "L3-9-Misses": {"PRE": "system.l3.demandMisses_9::switch_cpus", "POST": ".data"},
                   "M3-9-Hits": {"PRE": "system.l3.demandMshrHits_9::switch_cpus", "POST": ".data"},
                   "M3-9-Misses": {"PRE": "system.l3.demandMshrMisses_9::switch_cpus", "POST": ".data"},
                   "Avg-10-Latency": {"PRE": "system.switch_cpus", "POST": ".lsq0.loadToUse_10::mean"},
                   "L1-10-Hits": {"PRE": "dcache.demandHits_10::switch_cpus", "POST": ".data"},
                   "L1-10-Misses": {"PRE": "dcache.demandMisses_10::switch_cpus", "POST": ".data"},
                   "M1-10-Hits": {"PRE": "dcache.demandMshrHits_10::switch_cpus", "POST": ".data"},
                   "M1-10-Misses": {"PRE": "dcache.demandMshrMisses_10::switch_cpus", "POST": ".data"},
                   "L2-10-Hits": {"PRE": "l2cache.demandHits_10::switch_cpus", "POST": ".data"},
                   "L2-10-Misses": {"PRE": "l2cache.demandMisses_10::switch_cpus", "POST": ".data"},
                   "M2-10-Hits": {"PRE": "l2cache.demandMshrHits_10::switch_cpus", "POST": ".data"},
                   "M2-10-Misses": {"PRE": "l2cache.demandMshrMisses_10::switch_cpus", "POST": ".data"},
                   "L3-10-Hits": {"PRE": "system.l3.demandHits_10::switch_cpus", "POST": ".data"},
                   "L3-10-Misses": {"PRE": "system.l3.demandMisses_10::switch_cpus", "POST": ".data"},
                   "M3-10-Hits": {"PRE": "system.l3.demandMshrHits_10::switch_cpus", "POST": ".data"},
                   "M3-10-Misses": {"PRE": "system.l3.demandMshrMisses_10::switch_cpus", "POST": ".data"},
                   "Avg-11-Latency": {"PRE": "system.switch_cpus", "POST": ".lsq0.loadToUse_11::mean"},
                   "L1-11-Hits": {"PRE": "dcache.demandHits_11::switch_cpus", "POST": ".data"},
                   "L1-11-Misses": {"PRE": "dcache.demandMisses_11::switch_cpus", "POST": ".data"},
                   "M1-11-Hits": {"PRE": "dcache.demandMshrHits_11::switch_cpus", "POST": ".data"},
                   "M1-11-Misses": {"PRE": "dcache.demandMshrMisses_11::switch_cpus", "POST": ".data"},
                   "L2-11-Hits": {"PRE": "l2cache.demandHits_11::switch_cpus", "POST": ".data"},
                   "L2-11-Misses": {"PRE": "l2cache.demandMisses_11::switch_cpus", "POST": ".data"},
                   "M2-11-Hits": {"PRE": "l2cache.demandMshrHits_11::switch_cpus", "POST": ".data"},
                   "M2-11-Misses": {"PRE": "l2cache.demandMshrMisses_11::switch_cpus", "POST": ".data"},
                   "L3-11-Hits": {"PRE": "system.l3.demandHits_11::switch_cpus", "POST": ".data"},
                   "L3-11-Misses": {"PRE": "system.l3.demandMisses_11::switch_cpus", "POST": ".data"},
                   "M3-11-Hits": {"PRE": "system.l3.demandMshrHits_11::switch_cpus", "POST": ".data"},
                   "M3-11-Misses": {"PRE": "system.l3.demandMshrMisses_11::switch_cpus", "POST": ".data"},
                   "Avg-T-Latency": {"PRE": "system.switch_cpus", "POST": ".lsq0.loadToUse_T::mean"},
                   "L1-T-Hits": {"PRE": "dcache.demandHits_T::switch_cpus", "POST": ".data"},
                   "L1-T-Misses": {"PRE": "dcache.demandMisses_T::switch_cpus", "POST": ".data"},
                   "M1-T-Hits": {"PRE": "dcache.demandMshrHits_T::switch_cpus", "POST": ".data"},
                   "M1-T-Misses": {"PRE": "dcache.demandMshrMisses_T::switch_cpus", "POST": ".data"},
                   "L2-T-Hits": {"PRE": "l2cache.demandHits_T::switch_cpus", "POST": ".data"},
                   "L2-T-Misses": {"PRE": "l2cache.demandMisses_T::switch_cpus", "POST": ".data"},
                   "M2-T-Hits": {"PRE": "l2cache.demandMshrHits_T::switch_cpus", "POST": ".data"},
                   "M2-T-Misses": {"PRE": "l2cache.demandMshrMisses_T::switch_cpus", "POST": ".data"},
                   "L3-T-Hits": {"PRE": "system.l3.demandHits_T::switch_cpus", "POST": ".data"},
                   "L3-T-Misses": {"PRE": "system.l3.demandMisses_T::switch_cpus", "POST": ".data"},
                   "M3-T-Hits": {"PRE": "system.l3.demandMshrHits_T::switch_cpus", "POST": ".data"},
                   "M3-T-Misses": {"PRE": "system.l3.demandMshrMisses_T::switch_cpus", "POST": ".data"}}

all_instruction_types = {"LDFP": "commitStats0.committedInstType::FloatMemRead",
                         "STFP": "commitStats0.committedInstType::FloatMemWrite",
                         "VFP": "commitStats0.committedInstType::SIMDFloat",
                         "SFP": "commitStats0.committedInstType::Float",
                         "SINT": "commitStats0.committedInstType::Int",
                         "VINT": "commitStats0.committedInstType::SIMD",
                         "LDINT": "commitStats0.committedInstType::MemRead",
                         "STINT": "commitStats0.committedInstType::MemWrite",
                         "VEC": "commitStats0.committedInstType::Vector",
                         "Total": "commitStats0.committedInstType::total"}

SPD_load_latencies = ""

all_tiles = [1024, 2048, 4096, 8192, 16384]
all_tiles_str = ["1K", "2K", "4K", "8K", "16K"]
all_sizes = [2000000] #, ]
all_sizes_str = ["2M"] #, ""]
all_distances = [256, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304]
all_distances_str = ["256", "1K", "4K", "16K", "64K", "256K", "1M", "4M"]
all_modes = ["BASE", "MAA", "DMP"]

def parse_gem5_stats(stats, mode, target_stats):
    cycles = 0
    maa_cycles = {}
    maa_indirect_cycles = {}
    cache_stats = {}
    instruction_types = {}
    for maa_cycle in all_maa_cycles:
        maa_cycles[maa_cycle] = 0
    for maa_indirect_cycle in all_maa_indirect_cycles:
        maa_indirect_cycles[maa_indirect_cycle] = 0
    for cache_stat in all_cache_stats.keys():
        cache_stats[cache_stat] = 0
    for instruction_type in all_instruction_types.keys():
        instruction_types[instruction_type] = 0

    if os.path.exists(stats):    
        with open(stats, "r") as f:
            lines = f.readlines()
            current_num_tests = 0
            for line in lines:
                if "Begin Simulation Statistics" in line:
                    current_num_tests += 1
                    if current_num_tests > target_stats:
                        break
                    else:
                        continue
                if current_num_tests < target_stats:
                    continue
                words = line.split(" ")
                words = [word for word in words if word != ""]
                found = False
                if "simTicks" == words[0]:
                    cycles = int(words[1]) / 313
                    continue
                if "cpus" in words[0]:
                    pre = words[0].split("cpus")[0] + "cpus"
                    post = words[0].split("cpus")[1][1:]
                    if "system.cpu" in pre:
                        pre = pre.split("system.cpu")[1][2:]
                    for cache_stat in all_cache_stats.keys():
                        if all_cache_stats[cache_stat]["PRE"] == pre and all_cache_stats[cache_stat]["POST"] == post:
                            try:
                                cache_stats[cache_stat] = int(words[1])
                            except:
                                cache_stats[cache_stat] = float(words[1])
                            found = True
                            break
                if found:
                    continue
                if "system.switch_cpus" in words[0]:
                    for instruction_type in all_instruction_types.keys():
                        if all_instruction_types[instruction_type] in words[0]:
                            instruction_types[instruction_type] += int(words[1])
                            found = True
                            break
                if found:
                    continue
                if mode == "MAA" or mode == "maa":
                    if "system.maa.cycles" == words[0]:
                        maa_cycles["Total"] = int(words[1])
                        continue
                    for maa_cycle in all_maa_cycles:
                        if f"system.maa.cycles_{maa_cycle}" == words[0]:
                            maa_cycles[maa_cycle] = int(words[1])
                            found = True
                            break
                    if found:
                        continue
                    for maa_indirect_cycle in all_maa_indirect_cycles:
                        if f"system.maa.I0_IND_Cycles{maa_indirect_cycle}" == words[0]:
                            maa_indirect_cycles[maa_indirect_cycle] = int(words[1])
                            break
    # else:
    #     print(f"File not found: {stats}")

    return cycles, maa_cycles, maa_indirect_cycles, cache_stats, instruction_types

def parse_ramulator_stats(stats, target_stats, num_channels):
    # print(stats)
    DRAM_RD = 0
    DRAM_WR = 0
    DRAM_ACT = 0
    DRAM_RD_BW = 0
    DRAM_WR_BW = 0
    DRAM_total_BW = 0
    DRAM_RB_hitrate = 0
    DRAM_CTRL_occ = 0
    DRAM_PRE_STBY_ENERGY = 0
    DRAM_ACT_STBY_ENERGY = 0
    DRAM_ACTPRE_ENERGY = 0
    DRAM_RD_ENERGY = 0
    DRAM_WR_ENERGY = 0
    DRAM_DQ_ENERGY = 0
    total_DRAM_energy = 0

    if os.path.exists(f"{stats}") == True:
        COMMAND = f"cat {stats} | sed -n \'/Dumping ramulator/,$p\' > {stats}.ramulator"
        # print(COMMAND)
        os.system(COMMAND)
        cycles = 0
        num_reads = [0 for _ in range(num_channels)]
        num_writes = [0 for _ in range(num_channels)]
        num_acts = [0 for _ in range(num_channels)]
        num_pres = [0 for _ in range(num_channels)]
        num_preas = [0 for _ in range(num_channels)]
        avg_occupancy = [0 for _ in range(num_channels)]
        with open(f"{stats}.ramulator", "r") as f:
            lines = f.readlines()
            current_num_tests = 0
            for line in lines:
                if "Dumping ramulator's stats" in line:
                    current_num_tests += 1
                    if current_num_tests > num_channels * target_stats:
                        break
                    else:
                        continue
                if current_num_tests < num_channels * target_stats - 1:
                    continue
                words = line.split(" ")
                words = [word for word in words if word != ""]
                for word, word_id in zip(words, range(len(words))):
                    if word == "#":
                        words = words[:word_id]
                        break
                if cycles == 0:
                    if "memory_system_ROI_cycles" in line:
                        cycles = int(words[-1])
                        # print(f"cycles = {cycles}")
                        continue                    
                if "num_RD_commands_T" in line:
                    channel = int(words[-2].split("_")[0].split("CH")[1])
                    if num_reads[channel] == 0:
                        num_reads[channel] = int(words[-1])
                        # print(f"num_reads[{channel}] = {num_reads[channel]}")
                    continue
                if "num_WR_commands_T" in line:
                    channel = int(words[-2].split("_")[0].split("CH")[1])
                    if num_writes[channel] == 0:
                        num_writes[channel] = int(words[-1])
                        # print(f"num_writes[{channel}] = {num_writes[channel]}")
                    continue
                if "avg_queue_occupancy_T" in line:
                    channel = int(words[-2].split("_")[0].split("CH")[1])
                    if avg_occupancy[channel] == 0:
                        avg_occupancy[channel] = float(words[-1])
                        # print(f"avg_occupancy[{channel}] = {avg_occupancy[channel]}")
                    continue
                if "num_ACT_commands_T" in line:
                    channel = int(words[-2].split("_")[0].split("CH")[1])
                    if num_acts[channel] == 0:
                        num_acts[channel] = int(words[-1])
                        # print(f"num_acts[{channel}] = {num_acts[channel]}")
                    continue
                if "num_PRE_commands_T" in line:
                    channel = int(words[-2].split("_")[0].split("CH")[1])
                    if num_pres[channel] == 0:
                        num_pres[channel] = int(words[-1])
                        # print(f"num_pres[{channel}] = {num_pres[channel]}")
                    continue
                if "num_PREA_commands_T" in line:
                    channel = int(words[-2].split("_")[0].split("CH")[1])
                    if num_preas[channel] == 0:
                        num_preas[channel] = int(words[-1])
                        # print(f"num_preas[{channel}] = {num_preas[channel]}")
                    continue
        if cycles != 0:
            # for channel in range(num_channels):
            #     if num_reads[channel] == 0 or num_writes[channel] == 0 or num_acts[channel] == 0 or avg_occupancy[channel] == 0:
            #         print(f"Error: {num_reads[channel]}, {num_writes[channel]}, {num_acts[channel]}, {avg_occupancy[channel]}")
            #         exit(-1)
            DRAM_RD = sum(num_reads)
            DRAM_WR = sum(num_writes)
            DRAM_ACT = sum(num_acts)
            DRAM_PRE = sum(num_pres) + sum(num_preas) * 16.00
            DRAM_RD_BW = (DRAM_RD * 64)  / (cycles * 625)
            DRAM_RD_BW *= 1e12 # 625 is in PS
            DRAM_RD_BW /= (1024 * 1024 * 1024) # BW is in GB/s
            DRAM_WR_BW = (DRAM_WR * 64)  / (cycles * 625)
            DRAM_WR_BW *= 1e12 # 625 is in PS
            DRAM_WR_BW /= (1024 * 1024 * 1024) # BW is in GB/s
            DRAM_total_BW = DRAM_RD_BW + DRAM_WR_BW
            DRAM_RB_hitrate = 100.00 - (DRAM_ACT * 100.00 / (DRAM_RD + DRAM_WR))
            # assert DRAM_RB_hitrate >= 0, f"DRAM_RB_hitrate: 100 - {DRAM_ACT * 100.00} / {DRAM_RD + DRAM_WR} == {DRAM_RB_hitrate}"
            DRAM_CTRL_occ = sum(avg_occupancy) / len(avg_occupancy)
            PRE_time = DRAM_PRE / 16.0000 * tRP
            ACT_time = num_channels * cycles * tCK - PRE_time
            assert ACT_time >= 0, f"ACT_time: {cycles} * {tCK} - {PRE_time} == {ACT_time} <= 0"
            DRAM_PRE_STBY_ENERGY = (PRE_time * ((VDD * IDD2N) + (VPP * IPP2N))) / GIGA
            DRAM_PRE_STBY_ENERGY *= 8.0000
            DRAM_ACT_STBY_ENERGY = (ACT_time * ((VDD * IDD3N) + (VPP * IPP3N))) / GIGA
            DRAM_ACT_STBY_ENERGY *= 8.0000
            DRAM_ACTPRE_ENERGY = DRAM_ACT * ((IDD0 - IDD3N) * VDD + (IPP0 - IPP3N) * VPP) * tRAS
            DRAM_ACTPRE_ENERGY += DRAM_PRE * ((IDD0 - IDD2N) * VDD + (IPP0 - IPP2N) * VPP) * tRP
            DRAM_ACTPRE_ENERGY /= GIGA
            DRAM_ACTPRE_ENERGY *= 8.0000
            DRAM_RD_ENERGY = DRAM_RD * ((IDD4R - IDD3N) * VDD + (IPP4R - IPP3N) * VPP) * tBL / GIGA
            DRAM_RD_ENERGY *= 8.0000
            DRAM_WR_ENERGY = DRAM_WR * ((IDD4W - IDD3N) * VDD + (IPP4W - IPP3N) * VPP) * tBL / GIGA
            DRAM_WR_ENERGY *= 8.0000
            DRAM_DQ_ENERGY = (DRAM_RD + DRAM_WR) * PDSDQ * tBL / GIGA
            DRAM_DQ_ENERGY *= 8.0000
            total_DRAM_energy = DRAM_PRE_STBY_ENERGY + DRAM_ACT_STBY_ENERGY + DRAM_ACTPRE_ENERGY + DRAM_RD_ENERGY + DRAM_WR_ENERGY + DRAM_DQ_ENERGY
    return DRAM_RD, DRAM_WR, DRAM_ACT, DRAM_RD_BW, DRAM_WR_BW, DRAM_total_BW, DRAM_RB_hitrate, DRAM_CTRL_occ, DRAM_PRE_STBY_ENERGY, DRAM_ACT_STBY_ENERGY, DRAM_ACTPRE_ENERGY, DRAM_RD_ENERGY, DRAM_WR_ENERGY, DRAM_DQ_ENERGY, total_DRAM_energy

def parse_mcpat_stats(sim_dir, target_stats, latency, mcpat_template, mcpat_run):
    core_static_power = 0
    core_dynamic_power = 0
    core_power = 0
    core_static_energy = 0
    core_dynamic_energy = 0
    core_energy = 0
    llc_static_power = 0
    llc_dynamic_power = 0
    llc_power = 0
    llc_static_energy = 0
    llc_dynamic_energy = 0
    llc_energy = 0

    if mcpat_run:
        E2EConvertor(f"{sim_dir}/stats.txt", f"{sim_dir}/config.json", mcpat_template, f"{sim_dir}/mcpat.xml")
        COMMAND = f"./ext/mcpat/build/mcpat --infile {sim_dir}/mcpat.xml --print_level 5"
        with open(f"{sim_dir}/mcpat.stats", "w") as f:
            subprocess.run(COMMAND, shell=True, stdout=f, stderr=f)
    space = None
    if os.path.exists(f"{sim_dir}/mcpat.stats"):
        with open(f"{sim_dir}/mcpat.stats", "r") as f:
            lines = f.readlines()
            for line in lines:
                if "Total Cores" in line:
                    space = "core"
                elif "Total L3s" in line:
                    space = "llc"
                elif "Total MCs" in line:
                    space = "core"
                elif "******************" in line:
                    space = None
                elif space != None:
                    if "Subthreshold Leakage with power gating" in line:
                        power = float(line.split(" = ")[1].split(" ")[0])
                        if space == "core":
                            core_static_power += power
                        elif space == "llc":
                            llc_static_power += power
                        else:
                            assert False
                    elif "Gate Leakage" in line:
                        power = float(line.split(" = ")[1].split(" ")[0])
                        if space == "core":
                            core_static_power += power
                        elif space == "llc":
                            llc_static_power += power
                        else:
                            assert False
                    elif "Runtime Dynamic" in line:
                        power = float(line.split(" = ")[1].split(" ")[0])
                        if space == "core":
                            core_dynamic_power += power
                        elif space == "llc":
                            llc_dynamic_power += power
                        else:
                            assert False
            core_power = core_static_power + core_dynamic_power
            llc_power = llc_static_power + llc_dynamic_power
            core_static_energy = core_static_power * latency
            core_dynamic_energy = core_dynamic_power * latency
            core_energy = core_power * latency
            llc_static_energy = llc_static_power * latency
            llc_dynamic_energy = llc_dynamic_power * latency
            llc_energy = llc_power * latency
    return core_static_power, core_dynamic_power, core_power, llc_static_power, llc_dynamic_power, llc_power, core_static_energy, core_dynamic_energy, core_energy, llc_static_energy, llc_dynamic_energy, llc_energy


def parse_results(directory, target_stats, mode, mem_channels, mcpat_run = False, mcpat_template = None):
    if directory[-1] == "/":
        directory = directory[:-1]
    if mode == "maa":
        mode = "MAA"
    elif mode == "base":
        mode = "BASE"
    stats = f"{directory}/stats.txt"
    cycles, maa_cycles, maa_indirect_cycles, cache_stats, instruction_types = parse_gem5_stats(stats, mode, target_stats)
    logs = f"{directory}/logs_run.txt"
    DRAM_RD, DRAM_WR, DRAM_ACT, DRAM_RD_BW, DRAM_WR_BW, DRAM_total_BW, DRAM_RB_hitrate, DRAM_CTRL_occ, DRAM_PRE_STBY_ENERGY, DRAM_ACT_STBY_ENERGY, DRAM_ACTPRE_ENERGY, DRAM_RD_ENERGY, DRAM_WR_ENERGY, DRAM_DQ_ENERGY, total_DRAM_energy = parse_ramulator_stats(logs, target_stats, mem_channels)
    core_static_power, core_dynamic_power, core_power, llc_static_power, llc_dynamic_power, llc_power, core_static_energy, core_dynamic_energy, core_energy, llc_static_energy, llc_dynamic_energy, llc_energy = parse_mcpat_stats(directory, target_stats, cycles / CORE_FREQUENCY, mcpat_template, mcpat_run)
    res = f"{cycles},"
    for maa_cycle in all_maa_cycles:
        res += f"{maa_cycles[maa_cycle]},"
    for maa_indirect_cycle in all_maa_indirect_cycles:
        res += f"{maa_indirect_cycles[maa_indirect_cycle]},"
    for cache_stat in all_cache_stats.keys():
        res += f"{cache_stats[cache_stat]},"
    for instruction_type in all_instruction_types.keys():
        res += f"{instruction_types[instruction_type]},"
    if cycles == 0:
        res += f"{0},"
    else:
        res += f"{((instruction_types['LDINT'] + instruction_types['LDFP']) * cache_stats['Avg-T-Latency']) / cycles},"
    res += f"{DRAM_RD},{DRAM_WR},{DRAM_ACT},{DRAM_RD_BW},{DRAM_WR_BW},{DRAM_total_BW},{DRAM_RB_hitrate},{DRAM_CTRL_occ},{DRAM_PRE_STBY_ENERGY},{DRAM_ACT_STBY_ENERGY},{DRAM_ACTPRE_ENERGY},{DRAM_RD_ENERGY},{DRAM_WR_ENERGY},{DRAM_DQ_ENERGY},{total_DRAM_energy},"
    res += f"{core_static_power},{core_dynamic_power},{core_power},{llc_static_power},{llc_dynamic_power},{llc_power},{core_static_energy},{core_dynamic_energy},{core_energy},{llc_static_energy},{llc_dynamic_energy},{llc_energy},"
    return res

def parse_header():
    res = "cycles,"
    for maa_cycle in all_maa_cycles:
        res += f"{maa_cycle},"
    for maa_indirect_cycle in all_maa_indirect_cycles:
        res += f"IND_{maa_indirect_cycle},"
    for cache_stat in all_cache_stats.keys():
        res += f"{cache_stat},"
    for instruction_type in all_instruction_types.keys():
        res += f"{instruction_type},"
    res += "LSQ-LD-OCC,"
    res += "DRAM-RD,DRAM-WR,DRAM-ACT,DRAM-RD-BW,DRAM-WR-BW,DRAM-total-BW,DRAM-RB-hitrate,DRAM-CTRL-occ,DRAM-PRE-STB-energy,DRAM-ACT-STB-energy,DRAM-ACTPRE-energy,DRAM-RD-energy,DRAM-WR-energy,DRAM-DQ-energy,DRAM-Total-energy,CORE-ST-Power,CORE-DY-Power,CORE-Power,LLC-ST-Power,LLC-DY-Power,LLC-Power,CORE-ST-Energy,CORE-DY-Energy,CORE-Energy,LLC-ST-Energy,LLC-DY-Energy,LLC-Energy,"
    return res

def read_results(line, num_channels):
    words = line.split(",")
    kernel = words[0]
    mode = words[1]
    cycle = None
    L1_T_Misses = None
    L2_T_Misses = None
    L3_T_Misses = None
    total_instrs = None
    DRAM_total_BW = None
    DRAM_RB_hitrate = None
    DRAM_CTRL_occ = None

    offset = 3
    metrics = parse_header().split(",")
    for metric in metrics:
        if metric == "cycles":
            cycle = float(words[offset])
        elif metric == "L1-T-Misses":
            L1_T_Misses = float(words[offset])
        elif metric == "L2-T-Misses":
            L2_T_Misses = float(words[offset])
        elif metric == "L3-T-Misses":
            L3_T_Misses = float(words[offset])
        elif metric == "Total":
            total_instrs = float(words[offset])
        elif metric == "DRAM-total-BW":
            DRAM_total_BW = float(words[offset])
        elif metric == "DRAM-RB-hitrate":
            DRAM_RB_hitrate = float(words[offset])
        elif metric == "DRAM-CTRL-occ":
            DRAM_CTRL_occ = float(words[offset]) * 100.00 / 32.00
        offset += 1
    
    if cycle == 0:
        return kernel, mode, 0, 0, 0, 0, 0, 0, 0, 0
    
    L1_MPKI = (L1_T_Misses * 1000.00) / total_instrs
    L2_MPKI = (L2_T_Misses * 1000.00) / total_instrs
    L3_MPKI = (L3_T_Misses * 1000.00) / total_instrs
    DRAM_BW_util = DRAM_total_BW * 100.00 / (num_channels * 25.6)

    return kernel, mode, cycle, DRAM_BW_util, DRAM_RB_hitrate, DRAM_CTRL_occ, total_instrs, L1_MPKI, L2_MPKI, L3_MPKI


def plot_results(results_path, result_dir, num_channels):
    results = {}
    kernels = []
    with open(results_path, "r") as f:
        result_lines = f.readlines()[1:]
        for line in result_lines:
            kernel, mode, cycle, DRAM_BW_util, DRAM_RB_hitrate, DRAM_CTRL_occ, total_instrs, L1_MPKI, L2_MPKI, L3_MPKI = read_results(line, num_channels)
            if kernel not in results.keys():
                results[kernel] = {}
            assert mode not in results[kernel].keys()
            results[kernel][mode] = {"cycle": cycle, "DRAM_BW_util": DRAM_BW_util, "DRAM_RB_hitrate": DRAM_RB_hitrate, "DRAM_CTRL_occ": DRAM_CTRL_occ, "total_instrs": total_instrs, "L1_MPKI": L1_MPKI, "L2_MPKI": L2_MPKI, "L3_MPKI": L3_MPKI}
            if kernel not in kernels:
                kernels.append(kernel)

    speed_ups = []
    for kernel in kernels:
        base_cycle = results[kernel]["BASE"]["cycle"]
        maa_cycle = results[kernel]["MAA"]["cycle"]
        if base_cycle == 0 or maa_cycle == 0:
            speed_ups.append(0)
        else:
            speed_up = base_cycle / maa_cycle
            speed_ups.append(speed_up)
    plt.figure()
    plt.bar(kernels + ["GMEAN"], speed_ups + [statistics.geometric_mean([su for su in speed_ups if su != 0])], color="#91CF60", edgecolor='black')
    plt.xticks(rotation='vertical')
    plt.xlabel("Kernels")
    plt.ylabel("Speedup")
    plt.ylim(0, 4)
    plt.yticks(range(0, 5, 1))
    plt.title("DX100 speedup for different workloads")
    plt.tight_layout()
    plt.savefig(f"{result_dir}/speedup.png")
    plt.close()

    BASE_DRAM_BW_util = [x["DRAM_BW_util"] for x in [results[kernel]["BASE"] for kernel in kernels]]
    BASE_DRAM_BW_util += [statistics.mean([bw for bw in BASE_DRAM_BW_util if bw != 0])]
    MAA_DRAM_BW_util = [x["DRAM_BW_util"] for x in [results[kernel]["MAA"] for kernel in kernels]]
    MAA_DRAM_BW_util += [statistics.mean([bw for bw in MAA_DRAM_BW_util if bw != 0])]
    x = list(range(len(kernels + ["Average"])))
    bar_width = 0.4
    plt.figure()
    plt.bar([i - bar_width/2 for i in x], BASE_DRAM_BW_util, width=bar_width, color="#CE352B", label='Base', edgecolor='black')
    plt.bar([i + bar_width/2 for i in x], MAA_DRAM_BW_util, width=bar_width, color="#3E76B0", label='DX100', edgecolor='black')
    plt.xticks(x, kernels + ["Average"], rotation='vertical')
    plt.xlabel("Kernels")
    plt.ylabel("Bandwidth Utilization (%)")
    plt.ylim(0, 100)
    plt.yticks(range(0, 101, 20))
    plt.title("Bandwidth utilization")
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{result_dir}/bandwidth.png")
    plt.close()

    BASE_DRAM_RB_hitrate = [x["DRAM_RB_hitrate"] for x in [results[kernel]["BASE"] for kernel in kernels]]
    BASE_DRAM_RB_hitrate += [statistics.mean([bw for bw in BASE_DRAM_RB_hitrate if bw != 0])]
    MAA_DRAM_RB_hitrate = [x["DRAM_RB_hitrate"] for x in [results[kernel]["MAA"] for kernel in kernels]]
    MAA_DRAM_RB_hitrate += [statistics.mean([bw for bw in MAA_DRAM_RB_hitrate if bw != 0])]
    x = list(range(len(kernels + ["Average"])))
    bar_width = 0.4
    plt.figure()
    plt.bar([i - bar_width/2 for i in x], BASE_DRAM_RB_hitrate, width=bar_width, color="#F1B76E", label='Base', edgecolor='black')
    plt.bar([i + bar_width/2 for i in x], MAA_DRAM_RB_hitrate, width=bar_width, color="#8BB0D0", label='DX100', edgecolor='black')
    plt.xticks(x, kernels + ["Average"], rotation='vertical')
    plt.xlabel("Kernels")
    plt.ylabel("Row Buffer Hit Rate (%)")
    plt.ylim(0, 100)
    plt.yticks(range(0, 101, 20))
    plt.title("DRAM Row Buffer Hit Rate")
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{result_dir}/Row_Buffer_Hitrate.png")
    plt.close()

    BASE_DRAM_CTRL_occ = [x["DRAM_CTRL_occ"] for x in [results[kernel]["BASE"] for kernel in kernels]]
    BASE_DRAM_CTRL_occ += [statistics.mean([bw for bw in BASE_DRAM_CTRL_occ if bw != 0])]
    MAA_DRAM_CTRL_occ = [x["DRAM_CTRL_occ"] for x in [results[kernel]["MAA"] for kernel in kernels]]
    MAA_DRAM_CTRL_occ += [statistics.mean([bw for bw in MAA_DRAM_CTRL_occ if bw != 0])]
    x = list(range(len(kernels + ["Average"])))
    bar_width = 0.4
    plt.figure()
    plt.bar([i - bar_width/2 for i in x], BASE_DRAM_CTRL_occ, width=bar_width, color="#E98677", label='Base', edgecolor='black')
    plt.bar([i + bar_width/2 for i in x], MAA_DRAM_CTRL_occ, width=bar_width, color="#BDBAD8", label='DX100', edgecolor='black')
    plt.xticks(x, kernels + ["Average"], rotation='vertical')
    plt.xlabel("Kernels")
    plt.ylabel("Request Buffer Occupancy (%)")
    plt.ylim(0, 100)
    plt.yticks(range(0, 101, 20))
    plt.title("DRAM Controller Request Buffer Occupancy")
    plt.legend()
    plt.tight_layout()
    plt.savefig(f"{result_dir}/Request_Buffer_Occupancy.png")
    plt.close()

    instruction_red = []
    for kernel in kernels:
        base_total_instrs = results[kernel]["BASE"]["total_instrs"]
        maa_total_instrs = results[kernel]["MAA"]["total_instrs"]
        if base_total_instrs == 0 or maa_total_instrs == 0:
            instruction_red.append(0)
        else:
            speed_up = base_total_instrs / maa_total_instrs
            instruction_red.append(speed_up)
    plt.figure()
    plt.bar(kernels + ["Average"], instruction_red + [statistics.mean([ir for ir in instruction_red if ir != 0])], color="#FEE08B", edgecolor='black')
    plt.xticks(rotation='vertical')
    plt.xlabel("Kernels")
    plt.ylabel("Instruction Reduction (✕)")
    plt.ylim(0, 5)
    plt.yticks(range(0, 6, 1))
    plt.title("Instruction Reduction")
    plt.tight_layout()
    plt.savefig(f"{result_dir}/instruction_reduction.png")
    plt.close()

    Base_L1_MPKI = [x["L1_MPKI"] for x in [results[kernel]["BASE"] for kernel in kernels]]
    Base_L1_MPKI += [statistics.mean([bw for bw in Base_L1_MPKI if bw != 0])]
    Base_L2_MPKI = [x["L2_MPKI"] for x in [results[kernel]["BASE"] for kernel in kernels]]
    Base_L2_MPKI += [statistics.mean([bw for bw in Base_L2_MPKI if bw != 0])]
    Base_L3_MPKI = [x["L3_MPKI"] for x in [results[kernel]["BASE"] for kernel in kernels]]
    Base_L3_MPKI += [statistics.mean([bw for bw in Base_L3_MPKI if bw != 0])]
    MAA_L1_MPKI = [x["L1_MPKI"] for x in [results[kernel]["MAA"] for kernel in kernels]]
    MAA_L1_MPKI += [statistics.mean([bw for bw in MAA_L1_MPKI if bw != 0])]
    MAA_L2_MPKI = [x["L2_MPKI"] for x in [results[kernel]["MAA"] for kernel in kernels]]
    MAA_L2_MPKI += [statistics.mean([bw for bw in MAA_L2_MPKI if bw != 0])]
    MAA_L3_MPKI = [x["L3_MPKI"] for x in [results[kernel]["MAA"] for kernel in kernels]]
    MAA_L3_MPKI += [statistics.mean([bw for bw in MAA_L3_MPKI if bw != 0])]
    for i in range(len(x)):
        base = 0
        plt.bar(x[i] - bar_width/2, Base_L1_MPKI[i], width=bar_width, color="#FCCDE5", edgecolor='black')
        base += Base_L1_MPKI[i]
        plt.bar(x[i] - bar_width/2, Base_L2_MPKI[i], bottom=base, width=bar_width, color="#8DD3C7", edgecolor='black')
        base += Base_L2_MPKI[i]
        plt.bar(x[i] - bar_width/2, Base_L3_MPKI[i], bottom=base, width=bar_width, color="#D9D9D9", edgecolor='black')
    for i in range(len(x)):
        base = 0
        plt.bar(x[i] + bar_width/2, MAA_L1_MPKI[i], width=bar_width, color="#FCCDE5", edgecolor='black')
        base += MAA_L1_MPKI[i]
        plt.bar(x[i] + bar_width/2, MAA_L2_MPKI[i], bottom=base, width=bar_width, color="#8DD3C7", edgecolor='black')
        base += MAA_L2_MPKI[i]
        plt.bar(x[i] + bar_width/2, MAA_L3_MPKI[i], bottom=base, width=bar_width, color="#D9D9D9", edgecolor='black')
    plt.xticks(x, kernels + ["Average"], rotation='vertical')
    plt.xlabel("Kernels")
    plt.ylabel("MPKI")
    plt.ylim(0, 56)
    plt.yticks(range(0, 56, 16))
    plt.title("Miss Per Kilo Instructions(MPKI)")
    plt.tight_layout()
    plt.savefig(f"{result_dir}/MPKI.png")
    plt.close()



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Parse gem5.')
    parser.add_argument('--dir', type=str, help='Path to the result directory.', required=True)
    parser.add_argument('--mode', type=str, help='Mode of the simulation.', required=True, choices=["maa", "base"])
    parser.add_argument('--target', type=int, help='Traget stat number.', required=True)
    parser.add_argument('--mcpat_template', type=str, help='MCPAT template to use.', default="configs/template.xml")
    parser.add_argument('--no_mcpat_run', help='Do not run MCPAT, and use pre-existing files.', action="store_true")
    parser.add_argument('--mem_channels', type=int, help='Number of memory channels.', default=2, required=False)
    args = parser.parse_args()
    directory = args.dir
    mode = args.mode
    target_stats = args.target
    mcpat_template = args.mcpat_template
    mcpat_run = True if args.no_mcpat_run == None or args.no_mcpat_run == False else False
    mem_channels = args.mem_channels
    print(parse_header())
    print(parse_results(directory, target_stats, mode, mem_channels, mcpat_run, mcpat_template))