from m5.defines import buildEnv
from m5.objects import *

from gem5.isas import ISA

class SharedMAA(MAA):
    num_tiles_per_core = 8
    num_tile_elements = 16384
    num_regs_per_core = 8
    num_instructions_per_core = 8
    num_row_table_rows_per_slice = 64
    num_row_table_entries_per_subslice_row = 8
    num_row_table_config_cache_entries = 16
    reconfigure_row_table = False
    num_initial_row_table_slices = 32
    num_request_table_addresses = 128
    num_request_table_entries_per_address = 16
    spd_read_latency = 1
    spd_write_latency = 1
    num_spd_read_ports_per_maa = 4
    num_spd_write_ports_per_maa = 4
    rowtable_latency = 1
    ALU_lane_latency = 1
    num_ALU_lanes = 16
    max_outstanding_cache_side_packets = 512
    max_outstanding_cpu_side_packets = 512
    num_memory_channels = 2
    num_cores = 4
    num_maas = 1
    no_reorder = False
    force_cache_access = False