# Copyright (c) 2012-2013, 2015-2016 ARM Limited
# Copyright (c) 2020 Barkhausen Institut
# All rights reserved
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2010 Advanced Micro Devices, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Configure the M5 cache hierarchy config in one place
#

from common import ObjectList
from common.Caches import *

import m5
from m5.objects import *

from gem5.isas import ISA


def _get_hwp(hwp_option):
    if hwp_option == None:
        return NULL

    hwpClass = ObjectList.hwp_list.get(hwp_option)
    return hwpClass()


def _get_cache_opts(level, options):
    opts = {}

    size_attr = f"{level}_size"
    if hasattr(options, size_attr):
        opts["size"] = getattr(options, size_attr)

    assoc_attr = f"{level}_assoc"
    if hasattr(options, assoc_attr):
        opts["assoc"] = getattr(options, assoc_attr)
    
    mshrs_attr = f"{level}_mshrs"
    if hasattr(options, mshrs_attr):
        opts["mshrs"] = getattr(options, mshrs_attr)
    
    write_buffers_attr = f"{level}_write_buffers"
    if hasattr(options, write_buffers_attr):
        opts["write_buffers"] = getattr(options, write_buffers_attr)

    prefetcher_attr = f"{level}_hwp_type"
    if hasattr(options, prefetcher_attr):
        opts["prefetcher"] = _get_hwp(getattr(options, prefetcher_attr))

    return opts



def config_3L_cache(options, system):
    if options.external_memory_system:
        print("External caches and internal caches are exclusive options.\n")
        sys.exit(1)
    
    if not options.caches or not options.l2cache or not options.l3cache:
        print("config_3L_cache must be called only for 3 levels of the cache.\n")
        sys.exit(1)
    
    if options.memchecker:
        print("config_3L_cache must be called without memchecker.\n")
        sys.exit(1)

    if options.cpu_type == "O3_ARM_v7a_3":
        print("L3 cache not supported for the O3_ARM_v7a_3 CPU type.\n")
        sys.exit(1)
        try:
            import cores.arm.O3_ARM_v7a as core
        except:
            print("O3_ARM_v7a_3 is unavailable. Did you compile the O3 model?")
            sys.exit(1)

        dcache_class, icache_class, l2_cache_class, walk_cache_class = (
            core.O3_ARM_v7a_DCache,
            core.O3_ARM_v7a_ICache,
            core.O3_ARM_v7aL2,
            None,
        )
    elif options.cpu_type == "HPI":
        print("L3 cache not supported for the HPI CPU type.\n")
        sys.exit(1)
        try:
            import cores.arm.HPI as core
        except:
            print("HPI is unavailable.")
            sys.exit(1)

        dcache_class, icache_class, l2_cache_class, walk_cache_class = (
            core.HPI_DCache,
            core.HPI_ICache,
            core.HPI_L2,
            None,
        )
    else:
        dcache_class, icache_class, l2_cache_class, l3_cache_class, walk_cache_class = (
            L1_DCache,
            L1_ICache,
            L2Cache,
            L3Cache,
            None,
        )

    # Set the cache line size of the system
    system.cache_line_size = options.cacheline_size

    if options.l3cache:
        # Provide a clock for the L3 and the L2-to-L3 bus here as they
        # are not connected using addTwoLevelCacheHierarchy. Use the
        # same clock as the CPUs.
        system.l3 = l3_cache_class(
            clk_domain=system.cpu_clk_domain, **_get_cache_opts("l3", options)
        )

        if options.cpu_buffer_enlarge_factor != 1:
            print("Enlarging Cache buffers by a factor of %d" % options.cpu_buffer_enlarge_factor)
            system.l3.mshrs = system.l3.mshrs * options.cpu_buffer_enlarge_factor
            system.l3.write_buffers = system.l3.write_buffers * options.cpu_buffer_enlarge_factor

        system.tol3bus = L3XBar(clk_domain=system.cpu_clk_domain)
        for _ in range(options.l3_ports):
            print(f"Creating {options.l3_ports} L3 ports")
            system.l3.cpu_sides = system.tol3bus.mem_side_ports
            system.membus.cpu_side_ports = system.l3.mem_sides

    for i in range(options.num_cpus):
        icache = icache_class(**_get_cache_opts("l1i", options))
        dcache = dcache_class(**_get_cache_opts("l1d", options))
        l2cache = l2_cache_class(**_get_cache_opts("l2", options))

        if options.cpu_buffer_enlarge_factor != 1:
            icache.mshrs = icache.mshrs * options.cpu_buffer_enlarge_factor
            icache.write_buffers = icache.write_buffers * options.cpu_buffer_enlarge_factor
            dcache.mshrs = dcache.mshrs * options.cpu_buffer_enlarge_factor
            dcache.write_buffers = dcache.write_buffers * options.cpu_buffer_enlarge_factor
            l2cache.mshrs = l2cache.mshrs * options.cpu_buffer_enlarge_factor
            l2cache.write_buffers = l2cache.write_buffers * options.cpu_buffer_enlarge_factor

        if options.l1d_hwp_type == "StridePrefetcher":
            if options.maa:
                dcache.prefetcher.degree = 16 # getattr(options, "stride_degree", 16)
            else:
                dcache.prefetcher.degree = getattr(options, "stride_degree", 4)
        if options.l1d_hwp_type == "DiffMatchingPrefetcher":
            dcache.prefetcher.set_probe_obj(
                dcache, dcache, dcache
            )
            dcache.prefetcher.degree = getattr(options, "stride_degree", 4)
            dcache.prefetcher.stream_ahead_dist = getattr(options, "dmp_stream_ahead_dist", 64)
            dcache.prefetcher.indir_range = getattr(options, "dmp_indir_range", 4)
            # l2cache.prefetcher.queue_size = 1024*1024*16
            # l2cache.prefetcher.max_prefetch_requests_with_pending_translation = 1024
            l2cache.prefetcher.queue_size = 64
            l2cache.prefetcher.max_prefetch_requests_with_pending_translation = 64

        # enable VA for all prefetcher
        if options.l1d_hwp_type:
            dcache.prefetcher.prefetch_on_access = True
            dcache.prefetcher.use_virtual_addresses = True
            dcache.prefetcher.tag_vaddr = True
            # dcache.prefetcher.latency = 3
            dcache.prefetcher.latency = 5
            dcache.prefetcher.registerMMU(system.cpu[i].mmu)

        if options.l2_hwp_type == "StridePrefetcher":
            l2cache.prefetcher.degree = getattr(options, "stride_degree", 4)

        if options.l2_hwp_type == "IrregularStreamBufferPrefetcher":
            l2cache.prefetcher.degree = getattr(options, "stride_degree", 4)

        if options.l2_hwp_type == "DiffMatchingPrefetcher":

            if options.dmp_notify == "l1":
                l2cache.prefetcher.set_probe_obj(
                    dcache, dcache, l2cache
                )
            if options.dmp_notify == "l2":
                l2cache.prefetcher.set_probe_obj(dcache, l2cache, l2cache)

            if options.l1d_hwp_type == "StridePrefetcher":
                print("Add L1 StridePrefetcher as L2 DMP helper.")
                l2cache.prefetcher.set_pf_helper(dcache.prefetcher)
            else:
                l2cache.prefetcher.degree = getattr(options, "stride_degree", 4)

            l2cache.prefetcher.stream_ahead_dist = getattr(options, "dmp_stream_ahead_dist", 64)
            l2cache.prefetcher.range_ahead_dist = getattr(options, "dmp_range_ahead_dist", 0)
            l2cache.prefetcher.indir_range = getattr(options, "dmp_indir_range", 4)

            l2cache.prefetcher.auto_detect = True

            # l2cache.prefetcher.queue_size = 1024*1024*16
            # l2cache.prefetcher.max_prefetch_requests_with_pending_translation = 1024
            l2cache.prefetcher.queue_size = 64
            l2cache.prefetcher.max_prefetch_requests_with_pending_translation = 64

        # enable VA for all prefetcher
        if options.l2_hwp_type:
            l2cache.prefetcher.prefetch_on_access = True
            l2cache.prefetcher.use_virtual_addresses = True
            l2cache.prefetcher.tag_vaddr = True
            # l2cache.prefetcher.latency = 15
            l2cache.prefetcher.latency = 17
            l2cache.prefetcher.registerMMU(system.cpu[i].mmu)

        # If we are using ISA.X86 or ISA.RISCV, we set walker caches.
        if ObjectList.cpu_list.get_isa(options.cpu_type) in [
            ISA.RISCV,
            ISA.X86,
        ]:
            iwalkcache = PageTableWalkerCache()
            dwalkcache = PageTableWalkerCache()
        else:
            iwalkcache = None
            dwalkcache = None

        # When connecting the caches, the clock is also inherited
        # from the CPU in question
        system.cpu[i].addTwoLevelCacheHierarchy(
            icache, dcache, l2cache, iwalkcache, dwalkcache
        )

        system.cpu[i].createInterruptController()
        system.cpu[i].connectAllPorts(
            system.tol3bus.cpu_side_ports,
            system.membus.cpu_side_ports,
            system.membus.mem_side_ports if not options.maa else system.membusnc.mem_side_ports,
        )

    return system



def config_cache(options, system):
    if options.external_memory_system and (options.caches or options.l2cache):
        print("External caches and internal caches are exclusive options.\n")
        sys.exit(1)

    if options.external_memory_system:
        ExternalCache = ExternalCacheFactory(options.external_memory_system)

    if options.cpu_type == "O3_ARM_v7a_3":
        try:
            import cores.arm.O3_ARM_v7a as core
        except:
            print("O3_ARM_v7a_3 is unavailable. Did you compile the O3 model?")
            sys.exit(1)

        dcache_class, icache_class, l2_cache_class, walk_cache_class = (
            core.O3_ARM_v7a_DCache,
            core.O3_ARM_v7a_ICache,
            core.O3_ARM_v7aL2,
            None,
        )
    elif options.cpu_type == "HPI":
        try:
            import cores.arm.HPI as core
        except:
            print("HPI is unavailable.")
            sys.exit(1)

        dcache_class, icache_class, l2_cache_class, walk_cache_class = (
            core.HPI_DCache,
            core.HPI_ICache,
            core.HPI_L2,
            None,
        )
    else:
        dcache_class, icache_class, l2_cache_class, walk_cache_class = (
            L1_DCache,
            L1_ICache,
            L2Cache,
            None,
        )

    # Set the cache line size of the system
    system.cache_line_size = options.cacheline_size

    # If elastic trace generation is enabled, make sure the memory system is
    # minimal so that compute delays do not include memory access latencies.
    # Configure the compulsory L1 caches for the O3CPU, do not configure
    # any more caches.
    if options.l2cache and options.elastic_trace_en:
        fatal("When elastic trace is enabled, do not configure L2 caches.")

    if options.l2cache:
        # Provide a clock for the L2 and the L1-to-L2 bus here as they
        # are not connected using addTwoLevelCacheHierarchy. Use the
        # same clock as the CPUs.
        l2cache = l2_cache_class(
            clk_domain=system.cpu_clk_domain, **_get_cache_opts("l2", options)
        )

        system.tol2bus = L2XBar(clk_domain=system.cpu_clk_domain)
        l2cache.cpu_sides = system.tol2bus.mem_side_ports
        system.membus.cpu_side_ports = l2cache.mem_sides

    if options.memchecker:
        system.memchecker = MemChecker()

    for i in range(options.num_cpus):
        if options.caches:
            icache = icache_class(**_get_cache_opts("l1i", options))
            dcache = dcache_class(**_get_cache_opts("l1d", options))

            # If we are using ISA.X86 or ISA.RISCV, we set walker caches.
            if ObjectList.cpu_list.get_isa(options.cpu_type) in [
                ISA.RISCV,
                ISA.X86,
            ]:
                iwalkcache = PageTableWalkerCache()
                dwalkcache = PageTableWalkerCache()
            else:
                iwalkcache = None
                dwalkcache = None

            if options.memchecker:
                dcache_mon = MemCheckerMonitor(warn_only=True)
                dcache_real = dcache

                # Do not pass the memchecker into the constructor of
                # MemCheckerMonitor, as it would create a copy; we require
                # exactly one MemChecker instance.
                dcache_mon.memchecker = system.memchecker

                # Connect monitor
                dcache_mon.mem_side = dcache.cpu_sides

                # Let CPU connect to monitors
                dcache = dcache_mon

            # When connecting the caches, the clock is also inherited
            # from the CPU in question
            system.cpu[i].addPrivateSplitL1Caches(
                icache, dcache, iwalkcache, dwalkcache
            )

            if options.memchecker:
                # The mem_side ports of the caches haven't been connected yet.
                # Make sure connectAllPorts connects the right objects.
                system.cpu[i].dcache = dcache_real
                system.cpu[i].dcache_mon = dcache_mon

        elif options.external_memory_system:
            # These port names are presented to whatever 'external' system
            # gem5 is connecting to.  Its configuration will likely depend
            # on these names.  For simplicity, we would advise configuring
            # it to use this naming scheme; if this isn't possible, change
            # the names below.
            if ObjectList.cpu_list.get_isa(options.cpu_type) in [
                ISA.X86,
                ISA.ARM,
                ISA.RISCV,
            ]:
                system.cpu[i].addPrivateSplitL1Caches(
                    ExternalCache("cpu%d.icache" % i),
                    ExternalCache("cpu%d.dcache" % i),
                    ExternalCache("cpu%d.itb_walker_cache" % i),
                    ExternalCache("cpu%d.dtb_walker_cache" % i),
                )
            else:
                system.cpu[i].addPrivateSplitL1Caches(
                    ExternalCache("cpu%d.icache" % i),
                    ExternalCache("cpu%d.dcache" % i),
                )

        system.cpu[i].createInterruptController()
        if options.l2cache:
            system.cpu[i].connectAllPorts(
                system.tol2bus.cpu_side_ports,
                system.membus.cpu_side_ports,
                system.membus.mem_side_ports if not options.maa else system.membusnc.mem_side_ports,
            )
        elif options.external_memory_system:
            system.cpu[i].connectUncachedPorts(
                system.membus.cpu_side_ports, system.membus.mem_side_ports if not options.maa else system.membusnc.mem_side_ports
            )
        else:
            system.cpu[i].connectBus(system.membus)

    return system


# ExternalSlave provides a "port", but when that port connects to a cache,
# the connecting CPU SimObject wants to refer to its "cpu_side".
# The 'ExternalCache' class provides this adaptation by rewriting the name,
# eliminating distracting changes elsewhere in the config code.
class ExternalCache(ExternalSlave):
    def __getattr__(cls, attr):
        if attr == "cpu_side":
            attr = "port"
        return super(ExternalSlave, cls).__getattr__(attr)

    def __setattr__(cls, attr, value):
        if attr == "cpu_side":
            attr = "port"
        return super(ExternalSlave, cls).__setattr__(attr, value)


def ExternalCacheFactory(port_type):
    def make(name):
        return ExternalCache(
            port_data=name, port_type=port_type, addr_ranges=[AllMemory]
        )

    return make
