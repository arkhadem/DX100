from common import ObjectList
from common.MAA import *

import m5
from m5.objects import *

from gem5.isas import ISA

def _get_maa_opts(options):
    opts = {}

    if hasattr(options, "maa_num_tiles_per_core"):
        opts["num_tiles_per_core"] = getattr(options, "maa_num_tiles_per_core")

    if hasattr(options, "maa_num_tile_elements"):
        opts["num_tile_elements"] = getattr(options, "maa_num_tile_elements")

    if hasattr(options, "maa_num_regs_per_core"):
        opts["num_regs_per_core"] = getattr(options, "maa_num_regs_per_core")

    if hasattr(options, "maa_num_instructions_per_core"):
        opts["num_instructions_per_core"] = getattr(options, "maa_num_instructions_per_core")
    
    if hasattr(options, "maa_num_row_table_rows_per_slice"):
        opts["num_row_table_rows_per_slice"] = getattr(options, "maa_num_row_table_rows_per_slice")

    if hasattr(options, "maa_num_row_table_entries_per_subslice_row"):
        opts["num_row_table_entries_per_subslice_row"] = getattr(options, "maa_num_row_table_entries_per_subslice_row")

    if hasattr(options, "maa_num_row_table_config_cache_entries"):
        opts["num_row_table_config_cache_entries"] = getattr(options, "maa_num_row_table_config_cache_entries")
    
    if(hasattr(options, "maa_reconfigure_row_table")):
        opts["reconfigure_row_table"] = getattr(options, "maa_reconfigure_row_table")

    if(hasattr(options, "maa_no_reorder")):
        opts["no_reorder"] = getattr(options, "maa_no_reorder")

    if(hasattr(options, "maa_force_cache_access")):
        opts["force_cache_access"] = getattr(options, "maa_force_cache_access")

    if(hasattr(options, "maa_num_initial_row_table_slices")):
        opts["num_initial_row_table_slices"] = getattr(options, "maa_num_initial_row_table_slices")
    
    if(hasattr(options, "maa_num_request_table_addresses")):
        opts["num_request_table_addresses"] = getattr(options, "maa_num_request_table_addresses")
    
    if hasattr(options, "maa_num_request_table_entries_per_address"):
        opts["num_request_table_entries_per_address"] = getattr(options, "maa_num_request_table_entries_per_address")

    if hasattr(options, "maa_spd_read_latency"):
        opts["spd_read_latency"] = getattr(options, "maa_spd_read_latency")

    if hasattr(options, "maa_spd_write_latency"):
        opts["spd_write_latency"] = getattr(options, "maa_spd_write_latency")

    if hasattr(options, "maa_num_spd_read_ports_per_maa"):
        opts["num_spd_read_ports_per_maa"] = getattr(options, "maa_num_spd_read_ports_per_maa")

    if hasattr(options, "maa_num_spd_write_ports_per_maa"):
        opts["num_spd_write_ports_per_maa"] = getattr(options, "maa_num_spd_write_ports_per_maa")
    
    if hasattr(options, "maa_rowtable_latency"):
        opts["rowtable_latency"] = getattr(options, "maa_rowtable_latency")
    
    if hasattr(options, "maa_ALU_lane_latency"):
        opts["ALU_lane_latency"] = getattr(options, "maa_ALU_lane_latency")

    if hasattr(options, "maa_num_ALU_lanes"):
        opts["num_ALU_lanes"] = getattr(options, "maa_num_ALU_lanes")
    
    if hasattr(options, "maa_num_maas"):
        opts["num_maas"] = getattr(options, "maa_num_maas")
    
    opts["num_memory_channels"] = options.mem_channels
    opts["num_cores"] = options.num_cpus
    
    addr_ranges = []
    start = options.mem_size

    SPD_data_size = opts["num_tiles_per_core"] * opts["num_cores"] * opts["num_tile_elements"] * 4

    # scratchpad data (cacheable) (4 bytes each)
    addr_ranges.append(AddrRange(start=start, size=SPD_data_size))
    start = addr_ranges[-1].end

    # scratchpad data (noncacheable) (4 bytes each)
    addr_ranges.append(AddrRange(start=start, size=SPD_data_size))
    start = addr_ranges[-1].end

    # scratchpad size (noncacheable) (2 bytes each)
    SPD_size_size = opts["num_tiles_per_core"] * opts["num_cores"] * 2
    addr_ranges.append(AddrRange(start=start, size=SPD_size_size))
    start = addr_ranges[-1].end

    # scratchpad ready (noncacheable) (2 bytes each)
    SPD_ready_size = opts["num_tiles_per_core"] * opts["num_cores"] * 2
    addr_ranges.append(AddrRange(start=start, size=SPD_ready_size))
    start = addr_ranges[-1].end

    # scalar registers (noncacheable) (4 bytes each)
    scalar_regs_size = opts["num_regs_per_core"] * opts["num_cores"] * 4
    addr_ranges.append(AddrRange(start=start, size=scalar_regs_size))
    start = addr_ranges[-1].end

    # instruction file (noncacheable)
    instruction_file_size = 64
    addr_ranges.append(AddrRange(start=start, size=instruction_file_size))
    start = addr_ranges[-1].end

    opts["addr_ranges"] = addr_ranges

    return opts

def _get_cache_opts(level, options):
    opts = {}

    size_attr = f"{level}_size"
    if hasattr(options, size_attr):
        opts["size"] = getattr(options, size_attr)

    return opts

def get_maa_address(options):
    opts = _get_maa_opts(options)
    start_cacheable_addr = opts["addr_ranges"][0].start
    start_noncacheable_addr = opts["addr_ranges"][1].start
    end_cacheable_addr = Addr(opts["addr_ranges"][0].end)
    end_noncacheable_addr = Addr(opts["addr_ranges"][-1].end)
    size_cacheable_addr = end_cacheable_addr - start_cacheable_addr
    size_noncacheable_addr = end_noncacheable_addr - start_noncacheable_addr
    print(f"MAA Address: cacheable ({start_cacheable_addr}-{end_cacheable_addr} : {size_cacheable_addr}), noncacheable ({start_noncacheable_addr}-{end_noncacheable_addr} : {size_noncacheable_addr})")
    return start_cacheable_addr, size_cacheable_addr, start_noncacheable_addr, size_noncacheable_addr

def config_maa(options, system):
    assert(options.l3cache)
    opts = _get_maa_opts(options)
    system.maa = SharedMAA(clk_domain=system.cpu_clk_domain, **opts)
    
    # Increasing LLC side packets to accommodate the MAA routing table
    # Accomodating for all stream and indirect units (2)
    max_tol3_routing_table_size = (2 if "num_maas" not in opts else 2 * opts["num_maas"])
    max_tol3_routing_table_size *= (1 if "num_tile_elements" not in opts else opts["num_tile_elements"])
    max_tol3_routing_table_size = max(512, max_tol3_routing_table_size)
    print(f"MAA max tol3bus routing table size: {max_tol3_routing_table_size}")
    system.maa.max_outstanding_cache_side_packets = max_tol3_routing_table_size
    system.tol3bus.max_routing_table_size = max_tol3_routing_table_size

    # Accomodating for all invalidator and indirect units (2)
    max_mem_routing_table_size = (2 if "num_maas" not in opts else 2 * opts["num_maas"])
    max_mem_routing_table_size *= (1 if "num_tile_elements" not in opts else opts["num_tile_elements"])
    max_mem_routing_table_size = max(512, max_mem_routing_table_size)
    print(f"MAA max membus routing table size: {max_mem_routing_table_size}")
    system.maa.max_outstanding_cpu_side_packets = max_mem_routing_table_size
    system.membus.max_routing_table_size = max_mem_routing_table_size

    # Increasing snoop filter size to accommodate all LLC and MAA's SPD cachelines
    max_capacity = MemorySize("0")
    max_capacity.value += int(opts["addr_ranges"][-1].end) - int(opts["addr_ranges"][0].start)
    max_capacity.value += MemorySize(_get_cache_opts("l3", options)["size"]).value
    max_capacity.value += MemorySize(_get_cache_opts("l2", options)["size"]).value * options.num_cpus
    max_capacity.value += MemorySize(_get_cache_opts("l1i", options)["size"]).value * options.num_cpus
    max_capacity.value += MemorySize(_get_cache_opts("l1d", options)["size"]).value * options.num_cpus
    system.membus.snoop_filter.max_capacity = max_capacity
    system.tol3bus.snoop_filter.max_capacity = max_capacity
    print(f"MAA max snoop filter capacity: {system.tol3bus.snoop_filter.max_capacity}/{system.membus.snoop_filter.max_capacity}")
    
    for _ in range(options.num_cpus):
        system.maa.cpu_sides = system.membus.mem_side_ports

    for _ in range(options.num_cpus):
        system.maa.cache_sides = system.tol3bus.cpu_side_ports

    for _ in range(options.mem_channels):
        system.membusnc.cpu_side_ports = system.maa.mem_sides

    if options.maa_l2_uncacheable:
        print("MAA L2 uncacheable")
        for i in range(options.num_cpus):
            for addr_range in opts["addr_ranges"]:
                system.cpu[i].l2cache.excl_addr_ranges.append(addr_range)
    if options.maa_l3_uncacheable:
        print("MAA L3 uncacheable")
        for addr_range in opts["addr_ranges"]:
            system.l3.excl_addr_ranges.append(addr_range)
