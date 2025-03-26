from m5.objects.ClockedObject import ClockedObject
from m5.params import *
from m5.proxy import *
from m5.objects.X86MMU import X86MMU
from m5.SimObject import *

class MAA(ClockedObject):
    type = "MAA"
    cxx_header = "mem/MAA/MAA.hh"
    cxx_class = "gem5::MAA"
    cxx_exports = [PyBindMethod("addRamulator")]

    num_tiles_per_core = Param.Unsigned(8, "Number of SPD tiles per core attached to the DX100 instance")
    num_tile_elements = Param.Unsigned(16384, "Number of elements in each tile")
    num_regs_per_core = Param.Unsigned(8, "Number of 32-bit scalar registers per core attached to the DX100 instance")
    num_instructions_per_core = Param.Unsigned(8, "Number of instructions in the instruction file per core attached to the DX100 instance")
    num_row_table_rows_per_slice = Param.Unsigned(64, "Number of rows in each row table slice")
    num_row_table_entries_per_subslice_row = Param.Unsigned(8, "Number of row table entries (bursts) per each sub-slice of row table")
    num_row_table_config_cache_entries = Param.Unsigned(16, "Number of row table entry history in the configuration cache")
    num_request_table_addresses = Param.Unsigned(128, "Number of addresses in the request table")
    num_request_table_entries_per_address = Param.Unsigned(16, "Number of entries in the request table per address")
    reconfigure_row_table = Param.Bool(False, "Reconfigure row table")
    no_reorder = Param.Bool(False, "Do not reorder accesses using row table")
    force_cache_access = Param.Bool(False, "Force cache access instead of direct memory access for the indirect access unit")
    num_initial_row_table_slices = Param.Unsigned(32, "Number of initial row table slices if row table is not reconfigurable")
    spd_read_latency = Param.Cycles(1, "SPD read latency")
    spd_write_latency = Param.Cycles(1, "SPD write latency")
    num_spd_read_ports_per_maa = Param.Unsigned(4, "Number of SPD read ports per DX100 instance")
    num_spd_write_ports_per_maa = Param.Unsigned(4, "Number of SPD write ports per DX100 instance")
    rowtable_latency = Param.Cycles(1, "Row table latency")
    ALU_lane_latency = Param.Cycles(1, "ALU lane latency")
    num_ALU_lanes = Param.Unsigned(16, "Number of ALU lanes")
    cache_snoop_latency = Param.Cycles(1, "Cache snoop latency")
    max_outstanding_cache_side_packets = Param.Unsigned(512, "Maximum number of outstanding cache side packets")
    max_outstanding_cpu_side_packets = Param.Unsigned(512, "Maximum number of outstanding cpu side packets")
    num_memory_channels = Param.Unsigned(2, "Number of memory channels")
    num_cores = Param.Unsigned(4, "Number of cores")
    num_maas = Param.Unsigned(1, "Number of MAA instances")


    cpu_sides = VectorResponsePort("Vector port for connecting to the CPU and/or device")
    mem_sides = VectorRequestPort("Vector port for connecting to DRAM memory")
    # master = DeprecatedParam(
    #     mem_sides, "`master` is now called `mem_sides`"
    # )
    cache_sides = VectorRequestPort("Vector port for connecting to to LLC")

    addr_ranges = VectorParam.AddrRange(
        [AllMemory], "Address range for scratchpad data, scratchpad size, scratchpad ready, scalar registers, and instruction file"
    )
    mmu = Param.BaseMMU(X86MMU(), "CPU memory management unit")

    system = Param.System(Parent.any, "System we belong to")

    def addRamulatorInstance(self, simObj):
        self.getCCObject().addRamulator(simObj.getCCObject())