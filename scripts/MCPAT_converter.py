from math import log2
import xml.etree.ElementTree as ET
import sys
import json
import copy
import argparse

VERBOSE_DEBUG = False

def DEBUG(msg):
    if VERBOSE_DEBUG:
        print(msg)

def countCores(configFile):
    try:
        file = open(configFile)
    except IOError:
        DEBUG ("******ERROR: File not found or can't open config file******")
        sys.exit(1)

    configfile = json.load(file)

    #global variable we will use this elsewhere in the code
    global noCores

    #we are taking size of cpu array from config file
    noCores = len(configfile["system"]["cpu"])

    file.close()


def changeXML():
    #this will return root element
    root = tree.getroot()

    #subelement of root will be system
    systemElement = root.find("./component")

    #we are taking all the subelement of system
    core0 = tree.find("./component/component[1]")
    L1Directory0 = tree.find("./component/component[2]")
    L2Directory0 = tree.find("./component/component[3]")
    L20 = tree.find("./component/component[4]")
    L30 = tree.find("./component/component[5]")
    NoC0 = tree.find("./component/component[6]")
    mc = tree.find("./component/component[7]")
    niu = tree.find("./component/component[8]")
    pcie = tree.find("./component/component[9]")
    flashc = tree.find("./component/component[10]")

    #we need multicore in template-xml so first we are removing all the element of system
    for x in range(0,10):
        systemElement.remove(tree.find("./component/component[1]"))

    # system1 = copy.deepcopy(system)

    #first we copy core element then change id and name according to no-wise then append each core into system
    for no in range(0,noCores):
        core = copy.deepcopy(core0)
        core.attrib['id'] = "system.core"+str(no)
        core.attrib['name'] = "core"+str(no)

        #we also need to change id of sub component also
        for com in core.iter("component"):
            Id = com.attrib['id']
            IdArray = Id.split(".")
            Id = ""
            IdArray[1] = "core"+str(no)

            for e in range(0,len(IdArray)):
                Id += IdArray[e]
                Id += "."
            com.attrib['id'] = Id[:-1]

        systemElement.append(core)

    #append other component, if there are more than one make a loop here
    systemElement.append(L1Directory0)
    systemElement.append(L2Directory0)

    #In our case L2 cache is in each core...
    for no in range(0, noCores):
        L2 = copy.deepcopy(L20)
        L2.attrib['id'] = "system.L2"+str(no)
        L2.attrib['name'] = "L2"+str(no)
        systemElement.append(L2)

    #append other component, if there are more than one make a loop here
    systemElement.append(L30)
    systemElement.append(NoC0)
    systemElement.append(mc)
    systemElement.append(niu)
    systemElement.append(pcie)
    systemElement.append(flashc)

    #systemElement.append(system1)

    #tree.write("out.xml")



def readStatsFile(statFile):
    DEBUG (f"Reading Stat File: {statFile}")

    try:
        File = open(statFile)
    except IOError:
        DEBUG ("******ERROR: File not found or can't open stat file******")
        sys.exit(1)

    #Ignoring line starting with "---"
    ignore = "---"
    count = 2

    #for each line in stat file
    for line in File:
        if not ignore in line:

            lineArray = line.split(" ")
            Name = lineArray[0]             #we got name from stat file
            val = ''

            for e in lineArray:
                try:
                    val = int(e)            #int value from each line
                except ValueError:
                    try:
                        val = float(e)      #float value from each line
                    except ValueError:
                        continue

            #DEBUG "%d Name: %s \tValue: %s" %(count,Name ,val)
            count += 1

            stats[Name] = val               #storing the value in stat array
    #DEBUG stats["system.cpu0.commit.loads"]

    File.close()
    DEBUG ("Done")


def readWriteConfigValue(configFile):
    global config
    DEBUG (f"Reading config File: {configFile}")

    try:
        file = open(configFile)
    except IOError:
        DEBUG ("******ERROR: File not found or can't open config file******")
        sys.exit(1)

    config = json.load(file)

    #This is parent-child mapping we need parent of any child of xml-tree we will use this
    parent_map = dict((c, p) for p in tree.iter() for c in p)
    # for p in tree.iter():
    #     for c in list(p):
    #         DEBUG (f"{c}/{c.tag}/{c.attrib}/{c.text} --> {p}/{p.tag}/{p.attrib}/{p.text}")
    root = tree.getroot()

    #After getting value from config file if we have operation on the value that will goes here
    params = {}
    #This array contains default values that are not in config file but we are setting manually from this code
    defaultChangedConfigValue = {}

    defaultChangedConfigValue["system.core_tech_node"] = "28"

    defaultChangedConfigValue["system.number_of_cores"] = str(noCores)      #If you have homogeneous core make it 1
    defaultChangedConfigValue["system.number_of_L2s"] = "0"
    defaultChangedConfigValue["system.Private_L2"] = "1"                    #If you don't have L2 cache in each core make it 0 and make homo to 1
    defaultChangedConfigValue["system.homogeneous_cores"] = "0"             #we don't have homogeneous core otherwise make it 1
    defaultChangedConfigValue["system.homogeneous_L2s"] = "0"
    defaultChangedConfigValue["system.number_of_L3s"] = "1"                 #If there are more than one L3 cache then change it, also add loop in changeXML() and writeStatValue()
    defaultChangedConfigValue["system.mc.number_mcs"] = "2"                 #If more than one change it
    defaultChangedConfigValue["system.number_of_NoCs"] = "0"
    defaultChangedConfigValue["system.number_of_L1Directories"] = "0"
    defaultChangedConfigValue["system.number_of_L2Directories"] = "0"

    for no in range(0,noCores):
        defaultChangedConfigValue["system.cpu"+str(no)+".icache.cache_policy"] = "1"
        defaultChangedConfigValue["system.cpu"+str(no)+".icache.bank"] = "1"
        try:
            #Calculating throughput if we can't find the value in stat file make it 0
            defaultChangedConfigValue["system.cpu" + str(no) + ".icache.throughput"] = float(
                stats["system.cpu" + str(no) + ".icache.overallAccesses_T::switch_cpus0.inst"]) / (float(stats["simTicks"])/1000)
        except KeyError:
            defaultChangedConfigValue["system.cpu" + str(no) + ".icache.throughput"] = 0

        defaultChangedConfigValue["system.cpu" + str(no) + ".dcache.cache_policy"] = "1"
        defaultChangedConfigValue["system.cpu"+str(no)+".dcache.bank"] = "1"
        try:
            defaultChangedConfigValue["system.cpu" + str(no) + ".dcache.throughput"] = float(stats["system.cpu" + str(
                no) + ".dcache.overallAccesses_0::switch_cpus0.data"]) / (float(stats["simTicks"])/1000)
        except KeyError:
            defaultChangedConfigValue["system.cpu" + str(no) + ".dcache.throughput"] = 0



        defaultChangedConfigValue["system.cpu"+str(no)+".branchPred.bank"] = "1"
        try:
            defaultChangedConfigValue["system.cpu"+str(no)+".branchPred.throughput"] = float(stats["system.switch_cpus"
                                                                                        +str(no)+".branchPred.lookups_0::total"])/(float(stats["simTicks"])/1000)
        except KeyError:
            defaultChangedConfigValue["system.cpu"+str(no)+".branchPred.throughput"] = 0

        #if defaultChangedConfigValue["system.cpu"+str(no)+".branchPred.throughput"] == 0:
        #    defaultChangedConfigValue["system.cpu"+str(no)+".branchPred.throughput"] = 1

        defaultChangedConfigValue["system.cpu"+str(no)+".l2cache.cache_policy"] = "1"
        defaultChangedConfigValue["system.cpu" + str(no) + ".l2cache.bank"] = "1"
        try:
          defaultChangedConfigValue["system.cpu" + str(no) + ".l2cache.throughput"] = float(stats["system.cpu" + str(
              no) + ".l2cache.overallAccesses_T::total"]) / (float(stats["sim_ticks"])/1000)
        except KeyError:
           defaultChangedConfigValue["system.cpu" + str(no) + ".l2cache.throughput"] = 1

    # Fix me the default values
    defaultChangedConfigValue["system.mem_ctrls.write_buffer_size"] = "64"
    defaultChangedConfigValue["system.mem_ctrls.channels"] = "2"
    defaultChangedConfigValue["system.mem_ctrls.ranks_per_channel"] = "2"

    defaultChangedConfigValue["system.l3.cache_policy"] = "1"
    defaultChangedConfigValue["system.l3.bank"] = "1"

    #Setting default throughput to 1 for l3 cache
    try:
       defaultChangedConfigValue["system.l3.throughput"] = float(stats["system.l3.demandAccesses_T::total"])/(float(stats["simTicks"])/1000)
    except KeyError:
       defaultChangedConfigValue["system.l3.throughput"] = 1

    #Check for architecture type
    # X86 = getConfValue("system.cpu0.isa.type")
    X86 = "X86ISA"
    archType = X86

    #We are setting INT_EXE and FP_EXE that will be use for calculating pipeline depth
    if archType[:3] == "X86":
        INT_EXE = 2
        FP_EXE = 8
    elif archType[:3] == "Arm":
        INT_EXE = 3
        FP_EXE = 7
    else:
        INT_EXE = 3
        FP_EXE = 6

    if X86[:3] == "Arm":
        X86 = "0"
    else:
        X86 = "1"

    for no in range(0,noCores):
        defaultChangedConfigValue["system.core" + str(no) + ".x86"] = X86


    #To calculate pipeline depth for each core
    for no in range(0,noCores):
        base = getConfValue("system.switch_cpus"+str(no)+".fetchToDecodeDelay") + getConfValue("system.switch_cpus"+str(no)+".decodeToRenameDelay") + getConfValue("system.switch_cpus"+str(no)+".renameToIEWDelay") + getConfValue("system.switch_cpus"+str(no)+".iewToCommitDelay")

        cToDecode = getConfValue("system.switch_cpus"+str(no)+".commitToDecodeDelay")
        cToFetch = getConfValue("system.switch_cpus"+str(no)+".commitToFetchDelay")
        cToIew = getConfValue("system.switch_cpus"+str(no)+".commitToIEWDelay")
        cToRename = getConfValue("system.switch_cpus"+str(no)+".commitToRenameDelay")

        maxBase = max(cToDecode, cToFetch, cToIew, cToRename)

        pipeline_depthValue = str(INT_EXE + base + maxBase) + "," + str(FP_EXE + base + maxBase)
        defaultChangedConfigValue["system.core"+str(no)+".pipeline_depth"] = pipeline_depthValue

    #Here we have mapping from template-xml file to name from config or stat file
    mapping["system.number_of_cores"] = "system.number_of_cores"                        #we are not changing name if can't find in config or stat file
    mapping["system.number_of_L1Directories"] = "system.number_of_L1Directories"
    mapping["system.number_of_L2Directories"] = "system.number_of_L2Directories"
    mapping["system.number_of_L2s"] = "system.number_of_cores"
    mapping["system.Private_L2"] = "system.Private_L2"
    mapping["system.number_of_L3s"] = "system.number_of_L3s"
    mapping["system.number_of_NoCs"] = "system.number_of_NoCs"
    mapping["system.homogeneous_cores"] = "system.homogeneous_cores"
    mapping["system.homogeneous_L2s"] = "system.homogeneous_L2s"
    mapping["system.homogeneous_L1Directories"] = "default"
    mapping["system.homogeneous_L2Directories"] = "default"
    mapping["system.homogeneous_L3s"] = "default"
    mapping["system.homogeneous_ccs"] = "default"
    mapping["system.homogeneous_NoCs"] = "default"
    mapping["system.core_tech_node"] = "system.core_tech_node"
    mapping["system.target_core_clockrate"] = "system.clk_domain.clock"
    mapping["system.temperature"] = "default"
    mapping["system.number_cache_levels"] = "default"
    mapping["system.interconnect_projection_type"] = "default"
    mapping["system.device_type"] = "default"
    mapping["system.longer_channel_device"] = "default"
    mapping["system.Embedded"] = "system.Embedded"
    if X86 == "0":
        defaultChangedConfigValue["system.Embedded"] = "1"
    else:
        defaultChangedConfigValue["system.Embedded"] = "0"

    mapping["system.power_gating"] = "default"
    mapping["system.opt_clockrate"] = "default"
    mapping["system.machine_bits"] = "default"
    mapping["system.virtual_address_width"] = "default"
    mapping["system.physical_address_width"] = "default"
    mapping["system.virtual_memory_page_size"] = "default"

    #For multi-core we are using loop for mapping
    for no in range(0,noCores):

        mapping["system.core"+str(no)+".clock_rate"] = "system.clk_domain.clock"
        mapping["system.core"+str(no)+".vdd"] = "default"
        mapping["system.core" + str(no) + ".power_gating_vcc"] = "default"
        mapping["system.core"+str(no)+".opt_local"] = "default"
        mapping["system.core"+str(no)+".instruction_length"] = "default"
        mapping["system.core"+str(no)+".opcode_width"] = "default"
        mapping["system.core"+str(no)+".x86"] = "system.core"+str(no)+".x86"
        mapping["system.core"+str(no)+".micro_opcode_width"] = "default"
        mapping["system.core"+str(no)+".machine_type"] = "default"
        mapping["system.core"+str(no)+".number_hardware_threads"] = "system.switch_cpus"+str(no)+".numThreads"
        mapping["system.core"+str(no)+".fetch_width"] = "system.switch_cpus"+str(no)+".fetchWidth"
        mapping["system.core"+str(no)+".number_instruction_fetch_ports"] = "default"
        mapping["system.core"+str(no)+".decode_width"] = "system.switch_cpus"+str(no)+".decodeWidth"
        mapping["system.core"+str(no)+".issue_width"] = "system.switch_cpus"+str(no)+".issueWidth"
        mapping["system.core"+str(no)+".peak_issue_width"] = "default"
        mapping["system.core"+str(no)+".commit_width"] = "system.switch_cpus"+str(no)+".commitWidth"
        mapping["system.core"+str(no)+".fp_issue_width"] = "default"
        mapping["system.core"+str(no)+".prediction_width"] = "default"
        mapping["system.core"+str(no)+".pipelines_per_core"] = "default"
        mapping["system.core"+str(no)+".pipeline_depth"] = "system.core"+str(no)+".pipeline_depth"
        mapping["system.core"+str(no)+".ALU_per_core"] = "default"
        mapping["system.core"+str(no)+".MUL_per_core"] = "default"
        mapping["system.core"+str(no)+".FPU_per_core"] = "default"
        mapping["system.core"+str(no)+".instruction_buffer_size"] = "system.switch_cpus"+str(no)+".fetchBufferSize"
        mapping["system.core"+str(no)+".decoded_stream_buffer_size"] = "default"
        mapping["system.core"+str(no)+".instruction_window_scheme"] = "default"
        mapping["system.core"+str(no)+".instruction_window_size"] = "system.switch_cpus"+str(no)+".numIQEntries"
        params["system.cpu"+str(no)+".numIQEntries"] = getConfValue("system.switch_cpus"+str(no)+".numIQEntries")/2

        mapping["system.core"+str(no)+".fp_instruction_window_size"] = "system.switch_cpus"+str(no)+".numIQEntries"
        params["system.cpu"+str(no)+".numIQEntries"] = getConfValue("system.switch_cpus"+str(no)+".numIQEntries")/2

        mapping["system.core"+str(no)+".ROB_size"] = "system.switch_cpus"+str(no)+".numROBEntries"
        mapping["system.core"+str(no)+".archi_Regs_IRF_size"] = "default"
        mapping["system.core"+str(no)+".archi_Regs_FRF_size"] = "default"
        mapping["system.core"+str(no)+".phy_Regs_IRF_size"] = "system.switch_cpus"+str(no)+".numPhysIntRegs"
        mapping["system.core"+str(no)+".phy_Regs_FRF_size"] = "system.switch_cpus"+str(no)+".numPhysFloatRegs"
        mapping["system.core"+str(no)+".rename_scheme"] = "default"
        mapping["system.core"+str(no)+".checkpoint_depth"] = "default"
        mapping["system.core"+str(no)+".register_windows_size"] = "default"
        mapping["system.core"+str(no)+".LSU_order"] = "default"
        mapping["system.core"+str(no)+".store_buffer_size"] = "system.switch_cpus"+str(no)+".SQEntries"
        mapping["system.core"+str(no)+".load_buffer_size"] = "system.switch_cpus"+str(no)+".LQEntries"
        mapping["system.core"+str(no)+".memory_ports"] = "default"
        mapping["system.core"+str(no)+".itlb.number_entries"] = "system.cpu"+str(no)+".mmu.itb.size"
        mapping["system.core"+str(no)+".icache.icache_config"] = "system.cpu"+str(no)+".icache.size,"              # capacity,
        mapping["system.core"+str(no)+".icache.icache_config"] += "system.cpu"+str(no)+".icache.tags.block_size,"  # block_width, 
        mapping["system.core"+str(no)+".icache.icache_config"] += "system.cpu"+str(no)+".icache.assoc,"            # associativity, 
        mapping["system.core"+str(no)+".icache.icache_config"] += "constant1,"                                             # bank, 
        mapping["system.core"+str(no)+".icache.icache_config"] += "constant1,"                                             # throughput w.r.t. core clock, 
        mapping["system.core"+str(no)+".icache.icache_config"] += "system.cpu"+str(no)+".icache.data_latency,"     # latency w.r.t. core clock,
        mapping["system.core"+str(no)+".icache.icache_config"] += "system.cpu"+str(no)+".icache.tags.block_size,"  # output_width, 
        mapping["system.core"+str(no)+".icache.icache_config"] += "constant0"                                              # cache policy: 0: write-through, 1: write-back
        mapping["system.core"+str(no)+".icache.buffer_sizes"] = "system.cpu"+str(no)+".icache.mshrs,"                             # miss_buffer_size(MSHR),
        mapping["system.core"+str(no)+".icache.buffer_sizes"] += "system.cpu"+str(no)+".icache.mshrs,"                            # fill_buffer_size, (Not sure)
        mapping["system.core"+str(no)+".icache.buffer_sizes"] += "system.cpu"+str(no)+".icache.prefetcher.table_entries,"         # prefetch_buffer_size,
        mapping["system.core"+str(no)+".icache.buffer_sizes"] += "system.cpu"+str(no)+".icache.write_buffers"                     # wb_buffer_size
        mapping["system.core"+str(no)+".dtlb.number_entries"] = "system.cpu"+str(no)+".mmu.dtb.size"
        mapping["system.core"+str(no)+".dcache.dcache_config"] = "system.cpu"+str(no)+".dcache.size,"              # capacity,
        mapping["system.core"+str(no)+".dcache.dcache_config"] += "system.cpu"+str(no)+".dcache.tags.block_size,"  # block_width, 
        mapping["system.core"+str(no)+".dcache.dcache_config"] += "system.cpu"+str(no)+".dcache.assoc,"            # associativity, 
        mapping["system.core"+str(no)+".dcache.dcache_config"] += "constant1,"                                             # bank, 
        mapping["system.core"+str(no)+".dcache.dcache_config"] += "constant1,"                                             # throughput w.r.t. core clock, 
        mapping["system.core"+str(no)+".dcache.dcache_config"] += "system.cpu"+str(no)+".dcache.data_latency,"     # latency w.r.t. core clock,
        mapping["system.core"+str(no)+".dcache.dcache_config"] += "system.cpu"+str(no)+".dcache.tags.block_size,"  # output_width, 
        mapping["system.core"+str(no)+".dcache.dcache_config"] += "constant1"                                              # cache policy: 0: write-through, 1: write-back
        mapping["system.core"+str(no)+".dcache.buffer_sizes"] = "system.cpu"+str(no)+".dcache.mshrs,"                             # miss_buffer_size(MSHR),
        mapping["system.core"+str(no)+".dcache.buffer_sizes"] += "system.cpu"+str(no)+".dcache.mshrs,"                            # fill_buffer_size, (Not sure)
        mapping["system.core"+str(no)+".dcache.buffer_sizes"] += "system.cpu"+str(no)+".dcache.prefetcher.table_entries,"         # prefetch_buffer_size,
        mapping["system.core"+str(no)+".dcache.buffer_sizes"] += "system.cpu"+str(no)+".dcache.write_buffers"                     # wb_buffer_size

        mapping["system.core"+str(no)+".RAS_size"] = "system.switch_cpus"+str(no)+".branchPred.ras.numEntries"
        mapping["system.core"+str(no)+".number_of_BPT"] = "constant1"
        mapping["system.core"+str(no)+".predictor.local_predictor_size"] = "system.switch_cpus"+str(no)+".branchPred.localPredictorSize,"
        mapping["system.core"+str(no)+".predictor.local_predictor_size"] += "system.switch_cpus"+str(no)+".branchPred.localCtrBits"
        mapping["system.core"+str(no)+".predictor.local_predictor_entries"] = "system.switch_cpus"+str(no)+".branchPred.localHistoryTableSize"
        mapping["system.core"+str(no)+".predictor.global_predictor_entries"] = "system.switch_cpus"+str(no)+".branchPred.globalPredictorSize"
        mapping["system.core"+str(no)+".predictor.global_predictor_bits"] = "system.switch_cpus"+str(no)+".branchPred.globalCtrBits"
        mapping["system.core"+str(no)+".predictor.chooser_predictor_entries"] = "system.switch_cpus"+str(no)+".branchPred.choicePredictorSize"
        mapping["system.core"+str(no)+".predictor.chooser_predictor_bits"] = "system.switch_cpus"+str(no)+".branchPred.choiceCtrBits"
        mapping["system.core"+str(no)+".number_of_BTB"] = "constant1"
        mapping["system.core"+str(no)+".BTB.BTB_config"] = "system.switch_cpus"+str(no)+".branchPred.btb.numEntries,"  # capacity,
        mapping["system.core"+str(no)+".BTB.BTB_config"] += "system.switch_cpus"+str(no)+".branchPred.btb.tagBits,"    # block_width,
        mapping["system.core"+str(no)+".BTB.BTB_config"] += "constant1," # associativity,
        mapping["system.core"+str(no)+".BTB.BTB_config"] += "constant1," # bank, 
        mapping["system.core"+str(no)+".BTB.BTB_config"] += "constant1," # throughput w.r.t. core clock, 
        mapping["system.core"+str(no)+".BTB.BTB_config"] += "constant1" # latency w.r.t. core clock

    #If more than 1, change the value
    for no in range (0,1):
        mapping["system.L1Directory"+str(no)+".Directory_type"] = "default"
        mapping["system.L1Directory"+str(no)+".Dir_config"] = "default"
        mapping["system.L1Directory"+str(no)+".buffer_sizes"] = "default"
        mapping["system.L1Directory"+str(no)+".clockrate"] = "system.clk_domain.clock"
        mapping["system.L1Directory"+str(no)+".ports"] = "default"
        mapping["system.L1Directory"+str(no)+".device_type"] = "default"
        mapping["system.L1Directory" + str(no) + ".vdd"] = "default"
        mapping["system.L1Directory" + str(no) + ".power_gating_vcc"] = "default"

        mapping["system.L2Directory"+str(no)+".Directory_type"] = "default"
        mapping["system.L2Directory"+str(no)+".Dir_config"] = "default"
        mapping["system.L2Directory"+str(no)+".buffer_sizes"] = "default"
        mapping["system.L2Directory"+str(no)+".clockrate"] = "system.clk_domain.clock"
        mapping["system.L2Directory"+str(no)+".ports"] = "default"
        mapping["system.L2Directory"+str(no)+".device_type"] = "default"
        mapping["system.L2Directory" + str(no) + ".vdd"] = "default"
        mapping["system.L2Directory" + str(no) + ".power_gating_vcc"] = "default"

    for no in range (0,noCores):
        mapping["system.L2"+str(no)+".L2_config"] = "system.cpu"+str(no)+".l2cache.size,"              # capacity,
        mapping["system.L2"+str(no)+".L2_config"] += "system.cpu"+str(no)+".l2cache.tags.block_size,"  # block_width, 
        mapping["system.L2"+str(no)+".L2_config"] += "system.cpu"+str(no)+".l2cache.assoc,"            # associativity, 
        mapping["system.L2"+str(no)+".L2_config"] += "constant1,"                                              # bank, 
        mapping["system.L2"+str(no)+".L2_config"] += "constant1,"                                              # throughput w.r.t. core clock, 
        mapping["system.L2"+str(no)+".L2_config"] += "system.cpu"+str(no)+".l2cache.data_latency,"     # latency w.r.t. core clock,
        mapping["system.L2"+str(no)+".L2_config"] += "system.cpu"+str(no)+".l2cache.tags.block_size,"  # output_width, 
        mapping["system.L2"+str(no)+".L2_config"] += "constant1"                                               # cache policy: 0: write-through, 1: write-back

        mapping["system.L2"+str(no)+".buffer_sizes"] = "system.cpu"+str(no)+".l2cache.mshrs,"                             # miss_buffer_size(MSHR),
        mapping["system.L2"+str(no)+".buffer_sizes"] += "system.cpu"+str(no)+".l2cache.mshrs,"                            # fill_buffer_size, (Not sure)
        mapping["system.L2"+str(no)+".buffer_sizes"] += "system.cpu"+str(no)+".l2cache.prefetcher.table_entries,"         # prefetch_buffer_size,
        mapping["system.L2"+str(no)+".buffer_sizes"] += "system.cpu"+str(no)+".l2cache.write_buffers"                     # wb_buffer_size
        mapping["system.L2"+str(no)+".clockrate"] = "system.clk_domain.clock"
        mapping["system.L2"+str(no)+".ports"] = "default"
        mapping["system.L2"+str(no)+".device_type"] = "default"
        mapping["system.L2" + str(no) + ".vdd"] = "default"
        mapping["system.L2" + str(no) + ".Merged_dir"] = "default"
        mapping["system.L2" + str(no) + ".power_gating_vcc"] = "default"

    #If more than 1, change the value
    for no in range(0, 1):
        mapping["system.L3"+str(no)+".L3_config"] = "system.l3.size,"              # capacity,
        mapping["system.L3"+str(no)+".L3_config"] += "system.l3.tags.block_size,"  # block_width, 
        mapping["system.L3"+str(no)+".L3_config"] += "system.l3.assoc,"            # associativity, 
        mapping["system.L3"+str(no)+".L3_config"] += "system.l3.assoc," # f"constant{noCores},"         # bank, 
        mapping["system.L3"+str(no)+".L3_config"] += "constant1," # f"constant{noCores},"         # throughput w.r.t. core clock, 
        mapping["system.L3"+str(no)+".L3_config"] += "system.l3.data_latency,"     # latency w.r.t. core clock,
        mapping["system.L3"+str(no)+".L3_config"] += "system.l3.tags.block_size,"  # output_width, 
        mapping["system.L3"+str(no)+".L3_config"] += "constant1"                   # cache policy: 0: write-through, 1: write-back
        mapping["system.L3"+str(no)+".clockrate"] = "default"
        mapping["system.L3"+str(no)+".ports"] = "default"
        mapping["system.L3"+str(no)+".device_type"] = "default"
        mapping["system.L3" + str(no) + ".vdd"] = "default"
        mapping["system.L3"+str(no)+".buffer_sizes"] = "system.l3.mshrs,"                             # miss_buffer_size(MSHR),
        mapping["system.L3"+str(no)+".buffer_sizes"] += "system.l3.mshrs,"                            # fill_buffer_size, (Not sure)
        mapping["system.L3"+str(no)+".buffer_sizes"] += "system.l3.mshrs,"                            # prefetch_buffer_size (Not sure, fake),
        mapping["system.L3"+str(no)+".buffer_sizes"] += "system.l3.write_buffers"                     # wb_buffer_size
        mapping["system.L3" + str(no) + ".Merged_dir"] = "default"
        mapping["system.L3" + str(no) + ".power_gating_vcc"] = "default"

        mapping["system.NoC"+str(no)+".clockrate"] = "default"
        mapping["system.NoC" + str(no) + ".vdd"] = "default"
        mapping["system.NoC" + str(no) + ".power_gating_vcc"] = "default"
        mapping["system.NoC"+str(no)+".type"] = "default"
        mapping["system.NoC"+str(no)+".horizontal_nodes"] = "default"
        mapping["system.NoC"+str(no)+".vertical_nodes"] = "default"
        mapping["system.NoC"+str(no)+".has_global_link"] = "default"
        mapping["system.NoC"+str(no)+".link_throughput"] = "default"
        mapping["system.NoC"+str(no)+".link_latency"] = "default"
        mapping["system.NoC"+str(no)+".input_ports"] = "default"
        mapping["system.NoC"+str(no)+".output_ports"] = "default"
        mapping["system.NoC"+str(no)+".flit_bits"] = "default"
        mapping["system.NoC"+str(no)+".virtual_channel_per_port"] = "default"
        mapping["system.NoC" + str(no) + ".input_buffer_entries_per_vc"] = "default"
        mapping["system.NoC"+str(no)+".chip_coverage"] = "default"
        mapping["system.NoC"+str(no)+".link_routing_over_percentage"] = "default"

    mapping["system.mc.type"] = "default"
    mapping["system.mc.vdd"] = "default"
    mapping["system.mc.power_gating_vcc"] = "default"
    mapping["system.mc.mc_clock"] = "system.clk_domain.clock"
    mapping["system.mc.peak_transfer_rate"] = "default"
    mapping["system.mc.block_size"] = "system.mem_ctrls.write_buffer_size"
    mapping["system.mc.number_mcs"] = "system.mc.number_mcs"
    mapping["system.mc.memory_channels_per_mc"] = "system.mem_ctrls.channels"
    mapping["system.mc.number_ranks"] = "system.mem_ctrls.ranks_per_channel"
    mapping["system.mc.req_window_size_per_channel"] = "default"
    mapping["system.mc.IO_buffer_size_per_channel"] = "default"
    mapping["system.mc.databus_width"] = "default"
    mapping["system.mc.addressbus_width"] = "default"
    mapping["system.mc.withPHY"] = "default"
    mapping["system.niu.type"] = "default"
    mapping["system.niu.vdd"] = "default"
    mapping["system.niu.power_gating_vcc"] = "default"
    mapping["system.niu.clockrate"] = "default"
    mapping["system.niu.number_units"] = "default"
    mapping["system.pcie.type"] = "default"
    mapping["system.pcie.vdd"] = "default"
    mapping["system.pcie.power_gating_vcc"] = "default"
    mapping["system.pcie.withPHY"] = "default"
    mapping["system.pcie.clockrate"] = "default"
    mapping["system.pcie.number_units"] = "default"
    mapping["system.pcie.num_channels"] = "default"
    mapping["system.flashc.number_flashcs"] = "default"
    mapping["system.flashc.type"] = "default"
    mapping["system.flashc.vdd"] = "default"
    mapping["system.flashc.power_gating_vcc"] = "default"
    mapping["system.flashc.withPHY"] = "default"
    mapping["system.flashc.peak_transfer_rate"] = "default"

    #Writing config value into xml-tree
    for child in root.iter('param'):                            #look only for 'param' from xml-tree
        name = child.attrib['name']                             #Got the name and value of each 'param'
        val = child.attrib['value']
        name = parent_map[child].attrib['id']+"."+name          #In the name we only have like clock_rate, we are using path like system.core0.clock_rate
        # DEBUG(f"{child}/{parent_map[child]}")
        
        if name not in mapping:
            DEBUG(f"Not Found: name: {name} value: {val}")
            exit(1)
        
        foundVal = getConfValue(mapping[name])                  #Get value from config file
        if(mapping[name] == "system.clk_domain.clock"):
            foundVal = int(1000000/foundVal)

        if mapping[name] == "default":                          #If are not changing this 'param'
            continue
        elif "," in mapping[name]:                              #If ',' in the mapping get separate value for each of them
            mappingArray = mapping[name].split(",")
            ans = ""

            i = 0
            for x in mappingArray:
                findMltVal = getConfValue(x)                    #Getting value from config file if it's -1 then not found in config file

                if("local_predictor_size" in name) and i == 0:
                    assert "localPredictorSize" in x, f"localPredictorSize not in {x}"
                    findMltVal = str(int(log2(findMltVal)))

                #associativity must be power of 2 if not then we are taking previous power of 2
                if "assoc" in x:
                    if findMltVal and (findMltVal & (findMltVal-1)):
                        DEBUG(f"Warning: changing associativity of {name} from {findMltVal} to {2**int(log2(findMltVal))}")
                        findMltVal = 2**int(log2(findMltVal))

                #dcache size must be > 8kb if not we are changing it
                if "dcache.size" in x:
                    if findMltVal<8192:
                        DEBUG(f"Warning: changing size of {name} from {findMltVal} to 8192")
                        findMltVal = 8192

                #If can't find the value in config file look into default array if still not finding than make it to 1
                if findMltVal == -1:
                    if x in defaultChangedConfigValue:
                        ans += str(defaultChangedConfigValue[x])
                    elif "throughput" in x:
                        ans += str(32)
                    else:
                        ans += str(1)
                        DEBUG (f"{name} not found in config file setting default value to 1...")
                else:
                    if findMltVal is None:
                        findMltVal = 0

                    ans += str(findMltVal)
                ans += ","
                i += 1


            #DEBUG "%s\t%s" % (name, ans[:-1])
            child.attrib['value'] = str(ans[:-1])
        

        #If can't find the value in config file look into default array
        elif foundVal == -1:

            if mapping[name] in defaultChangedConfigValue:
                val = defaultChangedConfigValue[mapping[name]]
                child.attrib['value'] = str(val)
            else:
                DEBUG (f"{name} Not found in config file setting 0")
                child.attrib['value'] = "0"
        else:
            if foundVal == "" or foundVal == "[]":
                DEBUG (f"{name} Value is null in config file")

            #if we found value in config file but have done operation on the value look into params array
            elif mapping[name] in params:
                val = params[mapping[name]]
                child.attrib['value'] = str(val)

            else:
                val = foundVal

                if val is None:
                    val = 0

                child.attrib['value'] = str(val)
                #DEBUG "%s\t%s" %(name,getConfValue(mapping[name]))

    DEBUG ("Done")



def getConfValue(confStr):

    #don't allow ',' in input string
    if "," in confStr:
        return -1
    
    if "constant" == confStr[:8]:
        return int(confStr[8:])

    confStrArray = confStr.split(".")
    currentConfig = config                                          #whole config file

    # switch_cpus

    try:
        if(confStrArray[1][:11] == "switch_cpus"):
            coreNo = confStrArray[1][11:]                                #check if like cpu0 than get the core no.
        elif(confStrArray[1][:3] == "cpu"): 
            coreNo = confStrArray[1][3:]
    except IndexError:
        return -1

    currentPath = ""

    # DEBUG "Fetch width is : ", currentConfig["system"]["switch_cpus"][0]["fetchWidth"]
    for com in confStrArray:
        currentPath += com

        if not currentConfig:
            break

        #system.cpu0 will not be found we have make it like [system][cpu][0]
        if com not in currentConfig:
           if com[:11] == "switch_cpus":
               currentConfig = currentConfig["switch_cpus"][int(coreNo)]
           elif com[:3] == "cpu":
               currentConfig = currentConfig["cpu"][int(coreNo)]
           else:
               return -1                                            #value not found in config file
        elif com == "mem_ctrls":
            currentConfig = currentConfig[com][0]
        elif com == "isa":
            currentConfig = currentConfig[com][0]
        elif currentConfig:
            currentConfig = currentConfig[com]                      #every time assign sub component

        currentPath += "."
    #DEBUG currentConfig["system"]["cpu"][0]
    
    #clock will return array like [555] so we are taking value
    if confStrArray[2] == "clock":
        return currentConfig[0]
    else:
        return currentConfig

def getCacheReadAccesses(name):
    res = f"{name}.ReadReq_T.accesses::total+"
    res += f"{name}.ReadExReq_T.accesses::total+"
    res += f"{name}.ReadCleanReq_T.accesses::total+"
    res += f"{name}.ReadSharedReq_T.accesses::total"
    return res

def getCacheReadMisses(name):
    res = f"{name}.ReadReq_T.misses::total+"
    res += f"{name}.ReadExReq_T.misses::total+"
    res += f"{name}.ReadCleanReq_T.misses::total+"
    res += f"{name}.ReadSharedReq_T.misses::total"
    return res

def getCacheWriteAccesses(name):
    res = f"{name}.WriteReq_T.accesses::total+"
    res += f"{name}.WritebackDirty_T.accesses::total+"
    res += f"{name}.WritebackClean_T.accesses::total+"
    res += f"{name}.WriteLineReq_T.accesses::total+"
    res += f"{name}.WriteClean_T.accesses::total"
    return res

def getCacheWriteMisses(name):
    res = f"{name}.WriteReq_T.misses::total+"
    res += f"{name}.WritebackDirty_T.misses::total+"
    res += f"{name}.WritebackClean_T.misses::total+"
    res += f"{name}.WriteLineReq_T.misses::total+"
    res += f"{name}.WriteClean_T.misses::total"
    return res

def writeStatValue(mcpatTemplateFile):
    DEBUG (f"Reading mcpatTemplateFile: {mcpatTemplateFile}")

    #parent-child mapping
    parent_map = dict((c, p) for p in tree.iter() for c in p)

    root = tree.getroot()

    #if we can't find some parameter in stats file, we are adding that into stat array
    mapping["system.total_cycles"] = "system.total_cycles"
    mapping["system.idle_cycles"] = "system.idle_cycles"
    mapping["system.busy_cycles"] = "system.busy_cycles"
    stats["system.total_cycles"] = int(stats["simTicks"] / stats["system.clk_domain.clock"])
    stats["system.idle_cycles"] = 0
    stats["system.busy_cycles"] = stats["system.total_cycles"]


    # mapping["system.total_cycles"] = "system.total_cycles"
    # mapping["system.idle_cycles"] = "system.switch_cpus0.idleCycles"

    # #if we can't find some parameter in stats file, we are adding that into stat array
    # stats["system.total_cycles"] = 0
    # stats["system.idle_cycles"] = 0

    # for no in range(0,noCores):
    #     if noCores == 1:
    #         stats["system.total_cycles"] = stats["system.cpu.numCycles"]                               #If we have only 1 core so we have only 'cpu' or 'core' not 'cpu0' or 'core0'
    #         stats["system.idle_cycles"] = stats.get("system.cpu.idleCycles", stats.get("system.cpu.num_idle_cycles"))
    #     else:
    #         try:
    #             stats["system.total_cycles"] += stats["system.switch_cpus"+str(no)+".numCycles"]
    #         except KeyError:
    #             stats["system.cpu" + str(no) + ".numCycles"] = 0
    #         try:
    #             stats["system.idle_cycles"] += stats["system.switch_cpus"+str(no)+".idleCycles"]
    #         except KeyError:
    #             stats["system.cpu" + str(no) + ".idleCycles"] = 0

    # mapping["system.busy_cycles"] = "system.busy_cycles"
    # stats["system.busy_cycles"] = stats.get("system.cpu.num_busy_cycles", stats["system.total_cycles"] - stats["system.idle_cycles"])


    for no in range(0,noCores):

        # mapping["system.core"+str(no)+".total_instructions"] = "system.switch_cpus"+str(no)+".instsIssued"
        # mapping["system.core"+str(no)+".int_instructions"] = "system.switch_cpus"+str(no)+".commitStats0.numIntInsts"
        # mapping["system.core"+str(no)+".fp_instructions"] = "system.switch_cpus"+str(no)+".commitStats0.numFpInsts"

        # mapping["system.core"+str(no)+".branch_instructions"] = "system.switch_cpus"+str(no)+".branchPred.condPredicted"
        # mapping["system.core"+str(no)+".branch_mispredictions"] = "system.switch_cpus"+str(no)+".branchPred.condIncorrect"
        # mapping["system.core"+str(no)+".load_instructions"] = "system.switch_cpus"+str(no)+".commitStats0.numLoadInsts"
        # mapping["system.core"+str(no)+".store_instructions"] = "system.switch_cpus"+str(no)+".commitStats0.numStoreInsts"
        
        ialu = stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::IntAlu"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdAdd"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdAddAcc"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdAlu"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdCmp"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdCvt"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdMisc"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdShift"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdShiftAcc"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdDiv"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdSqrt"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdReduceAdd"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdReduceAlu"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdReduceCmp"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdAes"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdAesMix"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdSha1Hash"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdSha1Hash2"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdSha256Hash"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdSha256Hash2"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdShaSigma2"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdShaSigma3"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdPredAlu"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::Matrix"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::MatrixMov"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::MatrixOP"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::IprAccess"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::InstPrefetch"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorIntegerArith"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorIntegerReduce"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorMisc"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorIntegerExtension"]
        ialu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorConfig"]

        mult = stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::IntMult"]
        mult += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::IntDiv"]
        mult += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdMult"]
        mult += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdMultAcc"]
        mult += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdMatMultAcc"]

        fpu = stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatAdd"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatCmp"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatCvt"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatMult"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatMultAcc"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatDiv"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatMisc"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatSqrt"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatAdd"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatAlu"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatCmp"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatCvt"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatDiv"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatMisc"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatMult"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatMultAcc"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatMatMultAcc"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatSqrt"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatReduceAdd"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::SimdFloatReduceCmp"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorFloatArith"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorFloatConvert"]
        fpu += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorFloatReduce"]

        ld = stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::MemRead"]
        ld += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatMemRead"]
        ld += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorUnitStrideLoad"]
        ld += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorUnitStrideMaskLoad"]
        ld += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorStridedLoad"]
        ld += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorIndexedLoad"]
        ld += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorUnitStrideFaultOnlyFirstLoad"]
        ld += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorWholeRegisterLoad"]

        st = stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::MemWrite"]
        st += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::FloatMemWrite"]
        st += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorUnitStrideStore"]
        st += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorUnitStrideMaskStore"]
        st += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorStridedStore"]
        st += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorIndexedStore"]
        st += stats["system.switch_cpus"+str(no)+".statIssuedInstType_0::VectorWholeRegisterStore"]

        mapping["system.core"+str(no)+".total_instructions"] = "system.switch_cpus"+str(no)+".statIssuedInstType_0::total"
        mapping["system.core"+str(no)+".int_instructions"] = "system.core"+str(no)+".int_instructions"
        stats["system.core"+str(no)+".int_instructions"] = ialu + mult
        mapping["system.core"+str(no)+".fp_instructions"] = "system.core"+str(no)+".fp_instructions"
        stats["system.core"+str(no)+".fp_instructions"] = fpu
        mapping["system.core"+str(no)+".load_instructions"] = "system.core"+str(no)+".load_instructions"
        stats["system.core"+str(no)+".load_instructions"] = ld
        mapping["system.core"+str(no)+".store_instructions"] = "system.core"+str(no)+".store_instructions"
        stats["system.core"+str(no)+".store_instructions"] = st

        mapping["system.core"+str(no)+".branch_instructions"] = "system.switch_cpus"+str(no)+".branchPred.committed_0::total"
        mapping["system.core"+str(no)+".branch_mispredictions"] = "system.switch_cpus"+str(no)+".branchPred.mispredicted_0::total"

        mapping["system.core"+str(no)+".committed_int_instructions"] = "system.switch_cpus"+str(no)+".commitStats0.numIntInsts"
        mapping["system.core"+str(no)+".committed_fp_instructions"] = "system.switch_cpus"+str(no)+".commitStats0.numFpInsts"
        mapping["system.core"+str(no)+".committed_instructions"] = "system.switch_cpus"+str(no)+".commitStats0.numOps"

        mapping["system.core"+str(no)+".pipeline_duty_cycle"] = "constant1" # "system.switch_cpus"+str(no)+".commitStats0.ipc"
        mapping["system.core"+str(no)+".total_cycles"] = "system.switch_cpus"+str(no)+".numCycles"
        mapping["system.core"+str(no)+".idle_cycles"] = "system.switch_cpus"+str(no)+".idleCycles"
        mapping["system.core"+str(no)+".busy_cycles"] = "system.core"+str(no)+".busy_cycles"
        stats["system.core"+str(no)+".busy_cycles"] = stats["system.switch_cpus"+str(no)+".numCycles"] - stats["system.switch_cpus"+str(no)+".idleCycles"]

        mapping["system.core"+str(no)+".ROB_reads"] = "system.switch_cpus"+str(no)+".rob.reads"
        mapping["system.core"+str(no)+".ROB_writes"] = "system.switch_cpus"+str(no)+".rob.writes"

        mapping["system.core"+str(no)+".rename_reads"] = "system.switch_cpus"+str(no)+".rename.intLookups"
        mapping["system.core"+str(no)+".rename_writes"] = "system.core"+str(no)+".rename_writes"
        stats["system.core"+str(no)+".rename_writes"] = stats["system.switch_cpus"+str(no)+".rename.renamedOperands"]
        stats["system.core"+str(no)+".rename_writes"] *= stats["system.switch_cpus"+str(no)+".rename.intLookups"]
        stats["system.core"+str(no)+".rename_writes"] /= stats["system.switch_cpus"+str(no)+".rename.lookups"]

        mapping["system.core"+str(no)+".fp_rename_reads"] = "system.switch_cpus"+str(no)+".rename.fpLookups"
        mapping["system.core"+str(no)+".fp_rename_writes"] = "system.core"+str(no)+".fp_rename_writes"
        stats["system.core"+str(no)+".fp_rename_writes"] = stats["system.switch_cpus"+str(no)+".rename.renamedOperands"]
        stats["system.core"+str(no)+".fp_rename_writes"] *= stats["system.switch_cpus"+str(no)+".rename.fpLookups"]
        stats["system.core"+str(no)+".fp_rename_writes"] /= stats["system.switch_cpus"+str(no)+".rename.lookups"]

        mapping["system.core" + str(no) + ".rename_accesses"] = "system.core" + str(no) + ".rename_accesses"
        stats["system.core" + str(no) + ".rename_accesses"] = stats["system.switch_cpus"+str(no)+".rename.intLookups"]
        stats["system.core" + str(no) + ".rename_accesses"] += stats["system.core"+str(no)+".rename_writes"]

        mapping["system.core" + str(no) + ".fp_rename_accesses"] = "system.core" + str(no) + ".fp_rename_accesses"
        stats["system.core" + str(no) + ".fp_rename_accesses"] = stats["system.switch_cpus"+str(no)+".rename.fpLookups"]
        stats["system.core" + str(no) + ".fp_rename_accesses"] += stats["system.core"+str(no)+".fp_rename_writes"]

        mapping["system.core"+str(no)+".inst_window_reads"] = "system.switch_cpus"+str(no)+".intInstQueueReads"
        mapping["system.core"+str(no)+".inst_window_writes"] = "system.switch_cpus"+str(no)+".intInstQueueWrites"
        mapping["system.core"+str(no)+".inst_window_wakeup_accesses"] = "system.switch_cpus"+str(no)+".intInstQueueWakeupAccesses"

        mapping["system.core"+str(no)+".fp_inst_window_reads"] = "system.switch_cpus"+str(no)+".fpInstQueueReads"
        mapping["system.core"+str(no)+".fp_inst_window_writes"] = "system.switch_cpus"+str(no)+".fpInstQueueWrites"
        mapping["system.core"+str(no)+".fp_inst_window_wakeup_accesses"] = "system.switch_cpus"+str(no)+".fpInstQueueWakeupAccesses"

        mapping["system.core"+str(no)+".int_regfile_reads"] = "system.switch_cpus"+str(no)+".executeStats0.numIntRegReads"
        mapping["system.core"+str(no)+".int_regfile_writes"] = "system.switch_cpus"+str(no)+".executeStats0.numIntRegWrites"

        mapping["system.core"+str(no)+".float_regfile_reads"] = "system.switch_cpus"+str(no)+".executeStats0.numFpRegReads"
        mapping["system.core"+str(no)+".float_regfile_writes"] = "system.switch_cpus"+str(no)+".executeStats0.numFpRegWrites"

        mapping["system.core"+str(no)+".function_calls"] = "system.switch_cpus"+str(no)+".commit.functionCalls"

        mapping["system.core"+str(no)+".context_switches"] = "default"

        mapping["system.core"+str(no)+".ialu_accesses"] = "system.core"+str(no)+".ialu_accesses"
        mapping["system.core"+str(no)+".cdb_alu_accesses"] = "system.core"+str(no)+".ialu_accesses"
        stats["system.core"+str(no)+".ialu_accesses"] = ialu
        mapping["system.core"+str(no)+".fpu_accesses"] = "system.core"+str(no)+".fpu_accesses"
        mapping["system.core"+str(no)+".cdb_fpu_accesses"] = "system.core"+str(no)+".fpu_accesses"
        stats["system.core"+str(no)+".fpu_accesses"] = fpu
        mapping["system.core"+str(no)+".mul_accesses"] = "system.core"+str(no)+".mul_accesses"
        mapping["system.core"+str(no)+".cdb_mul_accesses"] = "system.core"+str(no)+".mul_accesses"
        stats["system.core"+str(no)+".mul_accesses"] = mult

        # mapping["system.core"+str(no)+".ialu_accesses"] = "system.switch_cpus"+str(no)+".intAluAccesses"
        # mapping["system.core"+str(no)+".fpu_accesses"] = "system.switch_cpus"+str(no)+".fpAluAccesses"
        # mapping["system.core"+str(no)+".mul_accesses"] = "system.core"+str(no)+".mul_accesses"
        # stats["system.core"+str(no)+".mul_accesses"] = stats["system.switch_cpus"+str(no)+".commitStats0.committedInstType::IntDiv "]
        # stats["system.core"+str(no)+".mul_accesses"] *= stats["system.switch_cpus"+str(no)+".commitStats0.committedInstType::No_OpClass"] + stats["system.switch_cpus"+str(no)+".commitStats0.committedInstType::IntMult"]*stats["system.switch_cpus"+str(no)+".commitStats0.committedInstType::No_OpClass"]

        # mapping["system.core"+str(no)+".cdb_alu_accesses"] = "system.switch_cpus"+str(no)+".intAluAccesses"
        # mapping["system.core"+str(no)+".cdb_mul_accesses"] = "system.core"+str(no)+".cdb_mul_accesses"
        # if noCores == 1:
        #     try:
        #         stats["system.core.cdb_mul_accesses"] = stats["system.core.mul_accesses"]
        #     except KeyError:
        #         stats["system.core.cdb_mul_accesses"] = 0
        # else:
        #     try:
        #         stats["system.core" + str(no) + ".cdb_mul_accesses"] = stats["system.core" + str(no) + ".mul_accesses"]
        #     except KeyError:
        #         stats["system.core" + str(no) + ".cdb_mul_accesses"] = 0

        # mapping["system.core"+str(no)+".cdb_fpu_accesses"] = "system.switch_cpus"+str(no)+".fpAluAccesses"

        mapping["system.core"+str(no)+".IFU_duty_cycle"] = "default"
        mapping["system.core"+str(no)+".BR_duty_cycle"] = "default"
        mapping["system.core"+str(no)+".LSU_duty_cycle"] = "default"
        mapping["system.core"+str(no)+".MemManU_I_duty_cycle"] = "default"
        mapping["system.core"+str(no)+".MemManU_D_duty_cycle"] = "default"
        mapping["system.core"+str(no)+".ALU_duty_cycle"] = "default"
        mapping["system.core"+str(no)+".MUL_duty_cycle"] = "default"
        mapping["system.core"+str(no)+".FPU_duty_cycle"] =  "default"
        mapping["system.core"+str(no)+".ALU_cdb_duty_cycle"] = "default"
        mapping["system.core"+str(no)+".MUL_cdb_duty_cycle"] = "default"
        mapping["system.core"+str(no)+".FPU_cdb_duty_cycle"] = "default"

        mapping["system.core"+str(no)+".itlb.total_accesses"] = "system.switch_cpus"+str(no)+".mmu.itb.rdAccesses"
        mapping["system.core"+str(no)+".itlb.total_misses"] = "system.switch_cpus"+str(no)+".mmu.itb.rdMisses"
        mapping["system.core"+str(no)+".itlb.conflicts"] = "default"

        mapping["system.core"+str(no)+".icache.read_accesses"] = "system.cpu"+str(no)+".icache.ReadReq_T.accesses::total"
        mapping["system.core"+str(no)+".icache.read_misses"] = "system.cpu"+str(no)+".icache.ReadReq_T.misses::total"
        mapping["system.core"+str(no)+".icache.conflicts"] = "default"
        mapping["system.core"+str(no)+".dtlb.total_accesses"] = "system.cpu"+str(no)+".mmu.dtb.rdAccesses"
        mapping["system.core"+str(no)+".dtlb.total_misses"] = "system.cpu"+str(no)+".mmu.dtb.rdMisses"
        mapping["system.core"+str(no)+".dtlb.conflicts"] = "default"
        mapping["system.core"+str(no)+".dcache.read_accesses"] = getCacheReadAccesses("system.cpu"+str(no)+".dcache")
        mapping["system.core"+str(no)+".dcache.write_accesses"] = getCacheWriteAccesses("system.cpu"+str(no)+".dcache")
        mapping["system.core"+str(no)+".dcache.read_misses"] = getCacheReadMisses("system.cpu"+str(no)+".dcache")
        mapping["system.core"+str(no)+".dcache.write_misses"] = getCacheWriteMisses("system.cpu"+str(no)+".dcache")
        mapping["system.core"+str(no)+".dcache.conflicts"] = "default"
        mapping["system.core"+str(no)+".BTB.read_accesses"] = "system.switch_cpus"+str(no)+".branchPred.BTBLookups"
        mapping["system.core"+str(no)+".BTB.write_accesses"] = "system.switch_cpus"+str(no)+".branchPred.BTBHits"
        mapping["system.L2"+str(no)+".read_accesses"] = getCacheReadAccesses("system.cpu"+str(no)+".l2cache")
        mapping["system.L2"+str(no)+".write_accesses"] = getCacheWriteAccesses("system.cpu"+str(no)+".l2cache")
        mapping["system.L2"+str(no)+".read_misses"] = getCacheReadMisses("system.cpu"+str(no)+".l2cache")
        mapping["system.L2"+str(no)+".write_misses"] = getCacheWriteMisses("system.cpu"+str(no)+".l2cache")
        mapping["system.L2"+str(no)+".conflicts"] = "default"
        mapping["system.L2"+str(no)+".duty_cycle"] = "default"
        mapping["system.L2" + str(no) + ".coherent_read_accesses"] = "default"
        mapping["system.L2" + str(no) + ".coherent_write_accesses"] = "default"
        mapping["system.L2" + str(no) + ".coherent_read_misses"] = "default"
        mapping["system.L2" + str(no) + ".coherent_write_misses"] = "default"
        mapping["system.L2" + str(no) + ".dir_duty_cycle"] = "default"

    mapping["system.L1Directory0.read_accesses"] = "default"
    mapping["system.L1Directory0.write_accesses"] = "default"
    mapping["system.L1Directory0.read_misses"] = "default"
    mapping["system.L1Directory0.write_misses"] = "default"
    mapping["system.L1Directory0.conflicts"] = "default"
    mapping["system.L1Directory0.duty_cycle"] = "default"
    mapping["system.L2Directory0.read_accesses"] = "default"
    mapping["system.L2Directory0.write_accesses"] = "default"
    mapping["system.L2Directory0.read_misses"] = "default"
    mapping["system.L2Directory0.write_misses"] = "default"
    mapping["system.L2Directory0.conflicts"] = "default"
    mapping["system.L2Directory0.duty_cycle"] = "default"

    #If there are more than one L3s add loop here
    mapping["system.L30.read_accesses"] = getCacheReadAccesses("system.l3")
    mapping["system.L30.write_accesses"] = getCacheWriteAccesses("system.l3")
    mapping["system.L30.read_misses"] = getCacheReadMisses("system.l3")
    mapping["system.L30.write_misses"] = getCacheWriteMisses("system.l3")
    mapping["system.L30.conflicts"] = "default"
    mapping["system.L30.duty_cycle"] = "default"
    mapping["system.L30.coherent_read_accesses"] = "default"
    mapping["system.L30.coherent_write_accesses"] = "default"
    mapping["system.L30.coherent_read_misses"] = "default"
    mapping["system.L30.coherent_write_misses"] = "default"
    mapping["system.L30.dir_duty_cycle"] = "default"

    mapping["system.NoC0.total_accesses"] = "default"
    mapping["system.NoC0.duty_cycle"] = "default"

    membus_name = None
    if "system.membusnc.transDist::ReadSharedReq" in stats or "system.membusnc.transDist::ReadExReq" in stats:
        membus_name = "system.membusnc"
    else:
        membus_name = "system.membus"
    mapping["system.mc.memory_reads"] = f"{membus_name}.transDist::ReadExReq+{membus_name}.transDist::ReadSharedReq"
    mapping["system.mc.memory_writes"] = f"{membus_name}.transDist::WritebackDirty"
    mapping["system.mc.memory_accesses"] = f"{membus_name}.transDist::ReadExReq+{membus_name}.transDist::ReadSharedReq+{membus_name}.transDist::WritebackDirty"

    mapping["system.niu.duty_cycle"] = "default"
    mapping["system.niu.total_load_perc"] = "default"
    mapping["system.pcie.duty_cycle"] = "default"
    mapping["system.pcie.total_load_perc"] = "default"
    mapping["system.flashc.duty_cycle"] = "default"
    mapping["system.flashc.total_load_perc"] = "default"

    #Writing stat parameter into xml-tree
    for child in root.iter('stat'):                                     #look only 'stat' parameter from xml-tree
        name = child.attrib['name']
        val = child.attrib['value']

        name = parent_map[child].attrib['id']+"."+name                  #we are using path like system.core0.clock_rate

        if mapping[name]=="default":                                    #If found default in mapping than we are not changing it
            continue

        if noCores == 1:                                                #If only 1 core but, we have mapping like 'system.core0' so we are removing 0 from core0
            assert False, "not supported yet"
            nmArray = mapping[name].split(".")
            #DEBUG nmArray[1][:3]
            if nmArray[1][:3] == "cpu":
                mapping[name] = mapping[name][:10]+mapping[name][11:]
                #DEBUG mapping[name]
            elif nmArray[1][:4] == "core":
                mapping[name] = mapping[name][:11]+mapping[name][12:]
                #DEBUG mapping[name]

        val = 0
        if "+" in mapping[name]:                              #If '+' in the mapping add up all items
            mappingArray = mapping[name].split("+")
            for x in mappingArray:
                if x in stats:
                    val += int(stats[x])
        else:        
            if mapping[name] in stats:
                val = stats[mapping[name]]                                  #Get the value from stat file
            elif mapping[name][:8] == "constant":                          #If constant than get the value from mapping
                val = int(mapping[name][8:])
            else:
                #still not found stat value make it 0
                val = 0
                DEBUG (f"Not found: mapping[{name}]: {mapping[name]} in stats, setting to 0...")

        if str(val)=="nan":
            DEBUG (f"Nan value: mapping[{name}]: {mapping[name]} in stats, setting to 0...")
            val = 0

        #change the value in xml-tree
        child.attrib['value'] = str(val)


    DEBUG ("Done")
    #tree.write("out.xml")



def indent(elem, level=0):
    #we are spacing xml-tree with specific format
    #If we are in root and we got sub component of root that is system then we add space before system component
    #so for tail we are decreasing space before component

    i = "\n" + level*"  "
    if len(elem):
        if not elem.text or not elem.text.strip():
            elem.text = i + "  "
        if not elem.tail or not elem.tail.strip():
            elem.tail = i
        for elem in elem:
            indent(elem, level+1)
        if not elem.tail or not elem.tail.strip():
            elem.tail = i
    else:
        if level and (not elem.tail or not elem.tail.strip()):
            elem.tail = i

def E2EConvertor(stats_path, config_path, template_path, output_path):

    #Checking file type
    if stats_path[-4:] != ".txt" or config_path[-5:] != ".json" or template_path[-4:] != ".xml" or output_path[-4:] != ".xml":
        DEBUG (f"ERROR: Please use appropriate file extensions: stats.txt, config.json, template.xml, output.xml")
        sys.exit(1)

    #tree contains xml-templete file, mapping array for string from xml file to config or stat file, stats array contains all the value from stats file
    global tree, mapping, stats

    #parsing xml file into tree
    try:
        tree = ET.parse(template_path)
    except IOError:
        DEBUG ("******ERROR: Templete File not found******")
        sys.exit(1)

    mapping = {}
    stats = {}

    #First we will count no. of cores from config file
    countCores(config_path)
    #We will change xml format according to no. of cores, here we have also L2 cache in each core
    # changeXML()

    #read every value from stat file
    readStatsFile(stats_path)
    #read value from config file then write into tree
    readWriteConfigValue(config_path)
    #write stat value to tree
    writeStatValue(template_path)

    #handle spaces with specific format in tree component
    indent(tree.getroot())

    #DEBUG tree into xml file
    tree.write(output_path)

if __name__ == "__main__":
    VERBOSE_DEBUG = True
    parser = argparse.ArgumentParser(description="GEM5 to MCPAT stat and config converter.")
    parser.add_argument("--stats", help="path to the stats.txt file", required=True)
    parser.add_argument("--config", help="path to the config.json file", required=True)
    parser.add_argument("--template", help="path to the template.xml file", required=True)
    parser.add_argument("--output", help="path to the output.xml file", required=True)
    args = parser.parse_args()
    E2EConvertor(args.stats, args.config, args.template, args.output)