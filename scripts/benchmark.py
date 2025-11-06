import argparse
from sim import run_simulation, run_simulation_MICRO, run_simulation_DMP, run_simulation_TS, run_simulation_SC, set_data_directory, run_tasks
from parse import parse_results, parse_header, plot_results, plot_results_DMP, plot_results_TS, plot_results_SC
import os

parser = argparse.ArgumentParser(description='Benchmarking script')
parser.add_argument('-j', type=int, required=True, help='Number of parallel simulations')
parser.add_argument('-a', type=str, default='all', choices=['simulate', 'parse', 'plot', 'all'], help='Action to perform')
parser.add_argument('-b', type=str, default='main', choices=['main', 'DMP', 'TS', 'SC', 'micro', 'all'], help='Type of benchmarking')
parser.add_argument('-f', action='store_true', help='Rerun both finished simulations and checkpoints')
parser.add_argument('-fs', action='store_true', help='Rerun finished simulations')
parser.add_argument('-fc', action='store_true', help='Rerun finished checkpoints')
parser.add_argument('-dir', type=str, required=True, help='Data directory used for storing Gem5 simulation results')
args = parser.parse_args()

DATA_DIR = args.dir
RSLT_DIR = f"{DATA_DIR}/results"

set_data_directory(DATA_DIR)

if args.a == 'simulate' or args.a == 'all':
    print(f'Simulating with {args.j} parallel processes')
    rerun_cpt = args.fc or args.f
    rerun_sim = args.fs or args.f
    if rerun_sim:
        print('Rerunning finished simulations too')
    if rerun_cpt:
        print('Rerunning finished checkpoints too')
    if args.b == 'main' or args.b == 'all':
        run_simulation(args.j, rerun_cpt, rerun_sim)
    if args.b == 'DMP' or args.b == 'all':
        run_simulation_DMP(args.j, rerun_cpt, rerun_sim)
    if args.b == 'TS' or args.b == 'all':
        run_simulation_TS(args.j, rerun_cpt, rerun_sim)
    if args.b == 'SC' or args.b == 'all':
        run_simulation_SC(args.j, rerun_cpt, rerun_sim)
    if args.b == 'micro':
        run_simulation_MICRO(args.j, rerun_cpt, rerun_sim)
    ########################################## RUN SELECTED EXPERIMENTS ##########################################
    run_tasks(args.j)

if args.a == 'parse' or args.a == 'all':
    print(f'Processing results')
    os.system("mkdir -p results")
    if args.b == 'main' or args.b == 'all':
        with open("results/results_main.csv", "w") as f:
            f.write("kernel,mode,size," + parse_header() + "\n")
            f.write("BFS,BASE,22," + parse_results(f"{RSLT_DIR}/bfs/BASE/22", 1, "BASE",  2) + "\n")
            f.write("BFS,MAA,22," + parse_results(f"{RSLT_DIR}/bfs/MAA/22", 1, "MAA",  2) + "\n")
            f.write("PR,BASE,22," + parse_results(f"{RSLT_DIR}/pr/BASE/22", 1, "BASE",  2) + "\n")
            f.write("PR,MAA,22," + parse_results(f"{RSLT_DIR}/pr/MAA/22", 1, "MAA",  2) + "\n")
            # f.write("SSSP,BASE,21," + parse_results(f"{RSLT_DIR}/sssp/BASE/21", 1, "BASE",  2) + "\n")
            # f.write("SSSP,MAA,21," + parse_results(f"{RSLT_DIR}/sssp/MAA/21", 1, "MAA",  2) + "\n")
            f.write("BC,BASE,20," + parse_results(f"{RSLT_DIR}/bc/BASE/20", 1, "BASE",  2) + "\n")
            f.write("BC,MAA,20," + parse_results(f"{RSLT_DIR}/bc/MAA/20", 1, "MAA",  2) + "\n")
            f.write("IS,BASE,b," + parse_results(f"{RSLT_DIR}/is/BASE/b", 1, "BASE",  2) + "\n")
            f.write("IS,MAA,b," + parse_results(f"{RSLT_DIR}/is/MAA/b", 1, "MAA",  2) + "\n")
            f.write("CG,BASE,c," + parse_results(f"{RSLT_DIR}/cg/BASE/c", 1, "BASE",  2) + "\n")
            f.write("CG,MAA,c," + parse_results(f"{RSLT_DIR}/cg/MAA/c", 1, "MAA",  2) + "\n")
            f.write("PRH,BASE,2M," + parse_results(f"{RSLT_DIR}/PRH/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("PRH,MAA,2M," + parse_results(f"{RSLT_DIR}/PRH/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("PRO,BASE,2M," + parse_results(f"{RSLT_DIR}/PRO/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("PRO,MAA,2M," + parse_results(f"{RSLT_DIR}/PRO/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("XRAGE,BASE,--," + parse_results(f"{RSLT_DIR}/spatter/xrage/BASE", 1, "BASE",  2) + "\n")
            f.write("XRAGE,MAA,--," + parse_results(f"{RSLT_DIR}/spatter/xrage/MAA", 1, "MAA",  2) + "\n")
            f.write("GZP,BASE,2M," + parse_results(f"{RSLT_DIR}/gradzatp/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZP,MAA,2M," + parse_results(f"{RSLT_DIR}/gradzatp/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZZ,BASE,2M," + parse_results(f"{RSLT_DIR}/gradzatz/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZZ,MAA,2M," + parse_results(f"{RSLT_DIR}/gradzatz/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZPI,BASE,2M," + parse_results(f"{RSLT_DIR}/gradzatp_invert/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZPI,MAA,2M," + parse_results(f"{RSLT_DIR}/gradzatp_invert/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZZI,BASE,2M," + parse_results(f"{RSLT_DIR}/gradzatz_invert/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZZI,MAA,2M," + parse_results(f"{RSLT_DIR}/gradzatz_invert/MAA/2M", 1, "MAA",  2) + "\n")
    
    if args.b == 'DMP' or args.b == 'all':
        with open("results/results_DMP.csv", "w") as f:
            f.write("kernel,mode,size," + parse_header() + "\n")
            f.write("BFS,DMP,22," + parse_results(f"{RSLT_DIR}/bfs/DMP/22", 1, "BASE",  2) + "\n")
            f.write("BFS,MAA,22," + parse_results(f"{RSLT_DIR}/bfs/MAA/22", 1, "MAA",  2) + "\n")
            f.write("PR,DMP,22," + parse_results(f"{RSLT_DIR}/pr/DMP/22", 1, "BASE",  2) + "\n")
            f.write("PR,MAA,22," + parse_results(f"{RSLT_DIR}/pr/MAA/22", 1, "MAA",  2) + "\n")
            # f.write("SSSP,DMP,21," + parse_results(f"{RSLT_DIR}/sssp/DMP/21", 1, "BASE",  2) + "\n")
            # f.write("SSSP,MAA,21," + parse_results(f"{RSLT_DIR}/sssp/MAA/21", 1, "MAA",  2) + "\n")
            f.write("BC,DMP,20," + parse_results(f"{RSLT_DIR}/bc/DMP/20", 1, "BASE",  2) + "\n")
            f.write("BC,MAA,20," + parse_results(f"{RSLT_DIR}/bc/MAA/20", 1, "MAA",  2) + "\n")
            f.write("IS,DMP,b," + parse_results(f"{RSLT_DIR}/is/DMP/b", 1, "BASE",  2) + "\n")
            f.write("IS,MAA,b," + parse_results(f"{RSLT_DIR}/is/MAA/b", 1, "MAA",  2) + "\n")
            f.write("CG,DMP,c," + parse_results(f"{RSLT_DIR}/cg/DMP/c", 1, "BASE",  2) + "\n")
            f.write("CG,MAA,c," + parse_results(f"{RSLT_DIR}/cg/MAA/c", 1, "MAA",  2) + "\n")
            f.write("PRH,DMP,2M," + parse_results(f"{RSLT_DIR}/PRH/DMP/2M", 1, "BASE",  2) + "\n")
            f.write("PRH,MAA,2M," + parse_results(f"{RSLT_DIR}/PRH/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("PRO,DMP,2M," + parse_results(f"{RSLT_DIR}/PRO/DMP/2M", 1, "BASE",  2) + "\n")
            f.write("PRO,MAA,2M," + parse_results(f"{RSLT_DIR}/PRO/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("XRAGE,DMP,--," + parse_results(f"{RSLT_DIR}/spatter/xrage/DMP", 1, "BASE",  2) + "\n")
            f.write("XRAGE,MAA,--," + parse_results(f"{RSLT_DIR}/spatter/xrage/MAA", 1, "MAA",  2) + "\n")
            f.write("GZP,DMP,2M," + parse_results(f"{RSLT_DIR}/gradzatp/DMP/2M", 1, "BASE",  2) + "\n")
            f.write("GZP,MAA,2M," + parse_results(f"{RSLT_DIR}/gradzatp/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZZ,DMP,2M," + parse_results(f"{RSLT_DIR}/gradzatz/DMP/2M", 1, "BASE",  2) + "\n")
            f.write("GZZ,MAA,2M," + parse_results(f"{RSLT_DIR}/gradzatz/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZPI,DMP,2M," + parse_results(f"{RSLT_DIR}/gradzatp_invert/DMP/2M", 1, "BASE",  2) + "\n")
            f.write("GZPI,MAA,2M," + parse_results(f"{RSLT_DIR}/gradzatp_invert/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZZI,DMP,2M," + parse_results(f"{RSLT_DIR}/gradzatz_invert/DMP/2M", 1, "BASE",  2) + "\n")
            f.write("GZZI,MAA,2M," + parse_results(f"{RSLT_DIR}/gradzatz_invert/MAA/2M", 1, "MAA",  2) + "\n")

    if args.b == 'TS' or args.b == 'all':
        with open("results/results_TS.csv", "w") as f:
            f.write("kernel,mode/TS,size," + parse_header() + "\n")

            f.write("BFS,BASE,22," + parse_results(f"{RSLT_DIR}/bfs/BASE/22", 1, "BASE",  2) + "\n")
            f.write("BFS,MAA/1K,22," + parse_results(f"{RSLT_DIR}_TS/bfs/MAA/22/1K", 1, "MAA",  2) + "\n")
            f.write("BFS,MAA/2K,22," + parse_results(f"{RSLT_DIR}_TS/bfs/MAA/22/2K", 1, "MAA",  2) + "\n")
            f.write("BFS,MAA/4K,22," + parse_results(f"{RSLT_DIR}_TS/bfs/MAA/22/4K", 1, "MAA",  2) + "\n")
            f.write("BFS,MAA/8K,22," + parse_results(f"{RSLT_DIR}_TS/bfs/MAA/22/8K", 1, "MAA",  2) + "\n")
            f.write("BFS,MAA/16K,22," + parse_results(f"{RSLT_DIR}_TS/bfs/MAA/22/16K", 1, "MAA",  2) + "\n")
            f.write("BFS,MAA/32K,22," + parse_results(f"{RSLT_DIR}_TS/bfs/MAA/22/32K", 1, "MAA",  2) + "\n")
                        
            f.write("PR,BASE,22," + parse_results(f"{RSLT_DIR}/pr/BASE/22", 1, "BASE",  2) + "\n")
            f.write("PR,MAA/1K,22," + parse_results(f"{RSLT_DIR}_TS/pr/MAA/22/1K", 1, "MAA",  2) + "\n")
            f.write("PR,MAA/2K,22," + parse_results(f"{RSLT_DIR}_TS/pr/MAA/22/2K", 1, "MAA",  2) + "\n")
            f.write("PR,MAA/4K,22," + parse_results(f"{RSLT_DIR}_TS/pr/MAA/22/4K", 1, "MAA",  2) + "\n")
            f.write("PR,MAA/8K,22," + parse_results(f"{RSLT_DIR}_TS/pr/MAA/22/8K", 1, "MAA",  2) + "\n")
            f.write("PR,MAA/16K,22," + parse_results(f"{RSLT_DIR}_TS/pr/MAA/22/16K", 1, "MAA",  2) + "\n")
            f.write("PR,MAA/32K,22," + parse_results(f"{RSLT_DIR}_TS/pr/MAA/22/32K", 1, "MAA",  2) + "\n")
                        
            # f.write("SSSP,BASE,21," + parse_results(f"{RSLT_DIR}/sssp/BASE/21", 1, "BASE",  2) + "\n")
            # f.write("SSSP,MAA/1K,21," + parse_results(f"{RSLT_DIR}_TS/sssp/MAA/21/1K", 1, "MAA",  2) + "\n")
            # f.write("SSSP,MAA/2K,21," + parse_results(f"{RSLT_DIR}_TS/sssp/MAA/21/2K", 1, "MAA",  2) + "\n")
            # f.write("SSSP,MAA/4K,21," + parse_results(f"{RSLT_DIR}_TS/sssp/MAA/21/4K", 1, "MAA",  2) + "\n")
            # f.write("SSSP,MAA/8K,21," + parse_results(f"{RSLT_DIR}_TS/sssp/MAA/21/8K", 1, "MAA",  2) + "\n")
            # f.write("SSSP,MAA/16K,21," + parse_results(f"{RSLT_DIR}_TS/sssp/MAA/21/16K", 1, "MAA",  2) + "\n")
            # f.write("SSSP,MAA/32K,21," + parse_results(f"{RSLT_DIR}_TS/sssp/MAA/21/32K", 1, "MAA",  2) + "\n")
                        
            f.write("BC,BASE,20," + parse_results(f"{RSLT_DIR}/bc/BASE/20", 1, "BASE",  2) + "\n")
            f.write("BC,MAA/1K,20," + parse_results(f"{RSLT_DIR}_TS/bc/MAA/20/1K", 1, "MAA",  2) + "\n")
            f.write("BC,MAA/2K,20," + parse_results(f"{RSLT_DIR}_TS/bc/MAA/20/2K", 1, "MAA",  2) + "\n")
            f.write("BC,MAA/4K,20," + parse_results(f"{RSLT_DIR}_TS/bc/MAA/20/4K", 1, "MAA",  2) + "\n")
            f.write("BC,MAA/8K,20," + parse_results(f"{RSLT_DIR}_TS/bc/MAA/20/8K", 1, "MAA",  2) + "\n")
            f.write("BC,MAA/16K,20," + parse_results(f"{RSLT_DIR}_TS/bc/MAA/20/16K", 1, "MAA",  2) + "\n")
            f.write("BC,MAA/32K,20," + parse_results(f"{RSLT_DIR}_TS/bc/MAA/20/32K", 1, "MAA",  2) + "\n")
                        
            f.write("IS,BASE,b," + parse_results(f"{RSLT_DIR}/is/BASE/b", 1, "BASE",  2) + "\n")
            f.write("IS,MAA/1K,b," + parse_results(f"{RSLT_DIR}_TS/is/MAA/b/1K", 1, "MAA",  2) + "\n")
            f.write("IS,MAA/2K,b," + parse_results(f"{RSLT_DIR}_TS/is/MAA/b/2K", 1, "MAA",  2) + "\n")
            f.write("IS,MAA/4K,b," + parse_results(f"{RSLT_DIR}_TS/is/MAA/b/4K", 1, "MAA",  2) + "\n")
            f.write("IS,MAA/8K,b," + parse_results(f"{RSLT_DIR}_TS/is/MAA/b/8K", 1, "MAA",  2) + "\n")
            f.write("IS,MAA/16K,b," + parse_results(f"{RSLT_DIR}_TS/is/MAA/b/16K", 1, "MAA",  2) + "\n")
            f.write("IS,MAA/32K,b," + parse_results(f"{RSLT_DIR}_TS/is/MAA/b/32K", 1, "MAA",  2) + "\n")
                        
            f.write("CG,BASE,c," + parse_results(f"{RSLT_DIR}/cg/BASE/c", 1, "BASE",  2) + "\n")
            f.write("CG,MAA/1K,c," + parse_results(f"{RSLT_DIR}_TS/cg/MAA/c/1K", 1, "MAA",  2) + "\n")
            f.write("CG,MAA/2K,c," + parse_results(f"{RSLT_DIR}_TS/cg/MAA/c/2K", 1, "MAA",  2) + "\n")
            f.write("CG,MAA/4K,c," + parse_results(f"{RSLT_DIR}_TS/cg/MAA/c/4K", 1, "MAA",  2) + "\n")
            f.write("CG,MAA/8K,c," + parse_results(f"{RSLT_DIR}_TS/cg/MAA/c/8K", 1, "MAA",  2) + "\n")
            f.write("CG,MAA/16K,c," + parse_results(f"{RSLT_DIR}_TS/cg/MAA/c/16K", 1, "MAA",  2) + "\n")
            f.write("CG,MAA/32K,c," + parse_results(f"{RSLT_DIR}_TS/cg/MAA/c/32K", 1, "MAA",  2) + "\n")
                        
            f.write("PRH,BASE,2M," + parse_results(f"{RSLT_DIR}/PRH/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("PRH,MAA/1K,2M," + parse_results(f"{RSLT_DIR}_TS/PRH/MAA/2M/1K", 1, "MAA",  2) + "\n")
            f.write("PRH,MAA/2K,2M," + parse_results(f"{RSLT_DIR}_TS/PRH/MAA/2M/2K", 1, "MAA",  2) + "\n")
            f.write("PRH,MAA/4K,2M," + parse_results(f"{RSLT_DIR}_TS/PRH/MAA/2M/4K", 1, "MAA",  2) + "\n")
            f.write("PRH,MAA/8K,2M," + parse_results(f"{RSLT_DIR}_TS/PRH/MAA/2M/8K", 1, "MAA",  2) + "\n")
            f.write("PRH,MAA/16K,2M," + parse_results(f"{RSLT_DIR}_TS/PRH/MAA/2M/16K", 1, "MAA",  2) + "\n")
            f.write("PRH,MAA/32K,2M," + parse_results(f"{RSLT_DIR}_TS/PRH/MAA/2M/32K", 1, "MAA",  2) + "\n")
                        
            f.write("PRO,BASE,2M," + parse_results(f"{RSLT_DIR}/PRO/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("PRO,MAA/1K,2M," + parse_results(f"{RSLT_DIR}_TS/PRO/MAA/2M/1K", 1, "MAA",  2) + "\n")
            f.write("PRO,MAA/2K,2M," + parse_results(f"{RSLT_DIR}_TS/PRO/MAA/2M/2K", 1, "MAA",  2) + "\n")
            f.write("PRO,MAA/4K,2M," + parse_results(f"{RSLT_DIR}_TS/PRO/MAA/2M/4K", 1, "MAA",  2) + "\n")
            f.write("PRO,MAA/8K,2M," + parse_results(f"{RSLT_DIR}_TS/PRO/MAA/2M/8K", 1, "MAA",  2) + "\n")
            f.write("PRO,MAA/16K,2M," + parse_results(f"{RSLT_DIR}_TS/PRO/MAA/2M/16K", 1, "MAA",  2) + "\n")
            f.write("PRO,MAA/32K,2M," + parse_results(f"{RSLT_DIR}_TS/PRO/MAA/2M/32K", 1, "MAA",  2) + "\n")
                        
            f.write("XRAGE,BASE,--," + parse_results(f"{RSLT_DIR}/spatter/xrage/BASE", 1, "BASE",  2) + "\n")
            f.write("XRAGE,MAA/1K,--," + parse_results(f"{RSLT_DIR}_TS/spatter/xrage/MAA/1K", 1, "MAA",  2) + "\n")
            f.write("XRAGE,MAA/2K,--," + parse_results(f"{RSLT_DIR}_TS/spatter/xrage/MAA/2K", 1, "MAA",  2) + "\n")
            f.write("XRAGE,MAA/4K,--," + parse_results(f"{RSLT_DIR}_TS/spatter/xrage/MAA/4K", 1, "MAA",  2) + "\n")
            f.write("XRAGE,MAA/8K,--," + parse_results(f"{RSLT_DIR}_TS/spatter/xrage/MAA/8K", 1, "MAA",  2) + "\n")
            f.write("XRAGE,MAA/16K,--," + parse_results(f"{RSLT_DIR}_TS/spatter/xrage/MAA/16K", 1, "MAA",  2) + "\n")
            f.write("XRAGE,MAA/32K,--," + parse_results(f"{RSLT_DIR}_TS/spatter/xrage/MAA/32K", 1, "MAA",  2) + "\n")
                        
            f.write("GZP,BASE,2M," + parse_results(f"{RSLT_DIR}/gradzatp/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZP,MAA/1K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp/MAA/2M/1K", 1, "MAA",  2) + "\n")
            f.write("GZP,MAA/2K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp/MAA/2M/2K", 1, "MAA",  2) + "\n")
            f.write("GZP,MAA/4K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp/MAA/2M/4K", 1, "MAA",  2) + "\n")
            f.write("GZP,MAA/8K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp/MAA/2M/8K", 1, "MAA",  2) + "\n")
            f.write("GZP,MAA/16K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp/MAA/2M/16K", 1, "MAA",  2) + "\n")
            f.write("GZP,MAA/32K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp/MAA/2M/32K", 1, "MAA",  2) + "\n")
                        
            f.write("GZZ,BASE,2M," + parse_results(f"{RSLT_DIR}/gradzatz/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZZ,MAA/1K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz/MAA/2M/1K", 1, "MAA",  2) + "\n")
            f.write("GZZ,MAA/2K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz/MAA/2M/2K", 1, "MAA",  2) + "\n")
            f.write("GZZ,MAA/4K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz/MAA/2M/4K", 1, "MAA",  2) + "\n")
            f.write("GZZ,MAA/8K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz/MAA/2M/8K", 1, "MAA",  2) + "\n")
            f.write("GZZ,MAA/16K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz/MAA/2M/16K", 1, "MAA",  2) + "\n")
            f.write("GZZ,MAA/32K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz/MAA/2M/32K", 1, "MAA",  2) + "\n")
                        
            f.write("GZPI,BASE,2M," + parse_results(f"{RSLT_DIR}/gradzatp_invert/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZPI,MAA/1K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp_invert/MAA/2M/1K", 1, "MAA",  2) + "\n")
            f.write("GZPI,MAA/2K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp_invert/MAA/2M/2K", 1, "MAA",  2) + "\n")
            f.write("GZPI,MAA/4K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp_invert/MAA/2M/4K", 1, "MAA",  2) + "\n")
            f.write("GZPI,MAA/8K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp_invert/MAA/2M/8K", 1, "MAA",  2) + "\n")
            f.write("GZPI,MAA/16K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp_invert/MAA/2M/16K", 1, "MAA",  2) + "\n")
            f.write("GZPI,MAA/32K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatp_invert/MAA/2M/32K", 1, "MAA",  2) + "\n")
                        
            f.write("GZZI,BASE,2M," + parse_results(f"{RSLT_DIR}/gradzatz_invert/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZZI,MAA/1K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz_invert/MAA/2M/1K", 1, "MAA",  2) + "\n")
            f.write("GZZI,MAA/2K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz_invert/MAA/2M/2K", 1, "MAA",  2) + "\n")
            f.write("GZZI,MAA/4K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz_invert/MAA/2M/4K", 1, "MAA",  2) + "\n")
            f.write("GZZI,MAA/8K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz_invert/MAA/2M/8K", 1, "MAA",  2) + "\n")
            f.write("GZZI,MAA/16K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz_invert/MAA/2M/16K", 1, "MAA",  2) + "\n")
            f.write("GZZI,MAA/32K,2M," + parse_results(f"{RSLT_DIR}_TS/gradzatz_invert/MAA/2M/32K", 1, "MAA",  2) + "\n")
    
    if args.b == 'SC' or args.b == 'all':
        with open("results/results_SC.csv", "w") as f:
            f.write("kernel,mode,size,cores,DX100s," + parse_header() + "\n")
            f.write("BFS,BASE,22,4,0," + parse_results(f"{RSLT_DIR}/bfs/BASE/22", 1, "BASE",  2) + "\n")
            f.write("BFS,MAA,22,4,1," + parse_results(f"{RSLT_DIR}/bfs/MAA/22", 1, "MAA",  2) + "\n")
            f.write("BFS,BASE,23,8,0," + parse_results(f"{RSLT_DIR}_SC/bfs/BASE/23/8", 1, "BASE",  4) + "\n")
            f.write("BFS,MAA,23,8,1," + parse_results(f"{RSLT_DIR}_SC/bfs/MAA/23/8/1", 1, "MAA",  4) + "\n")
            f.write("BFS,MAA,23,8,2," + parse_results(f"{RSLT_DIR}_SC/bfs/MAA/23/8/2", 1, "MAA",  4) + "\n")
            
            f.write("PR,BASE,22,4,0," + parse_results(f"{RSLT_DIR}/pr/BASE/22", 1, "BASE",  2) + "\n")
            f.write("PR,MAA,22,4,1," + parse_results(f"{RSLT_DIR}/pr/MAA/22", 1, "MAA",  2) + "\n")
            f.write("PR,BASE,23,8,0," + parse_results(f"{RSLT_DIR}_SC/pr/BASE/23/8", 1, "BASE",  4) + "\n")
            f.write("PR,MAA,23,8,1," + parse_results(f"{RSLT_DIR}_SC/pr/MAA/23/8/1", 1, "MAA",  4) + "\n")
            f.write("PR,MAA,23,8,2," + parse_results(f"{RSLT_DIR}_SC/pr/MAA/23/8/2", 1, "MAA",  4) + "\n")
            
            # f.write("SSSP,BASE,21,4,0," + parse_results(f"{RSLT_DIR}/sssp/BASE/21", 1, "BASE",  2) + "\n")
            # f.write("SSSP,MAA,21,4,1," + parse_results(f"{RSLT_DIR}/sssp/MAA/21", 1, "MAA",  2) + "\n")
            # f.write("SSSP,BASE,22,8,0," + parse_results(f"{RSLT_DIR}_SC/sssp/BASE/22/8", 1, "BASE",  4) + "\n")
            # f.write("SSSP,MAA,22,8,1," + parse_results(f"{RSLT_DIR}_SC/sssp/MAA/22/8/1", 1, "MAA",  4) + "\n")
            # f.write("SSSP,MAA,22,8,2," + parse_results(f"{RSLT_DIR}_SC/sssp/MAA/22/8/2", 1, "MAA",  4) + "\n")
            
            f.write("BC,BASE,20,4,0," + parse_results(f"{RSLT_DIR}/bc/BASE/20", 1, "BASE",  2) + "\n")
            f.write("BC,MAA,20,4,1," + parse_results(f"{RSLT_DIR}/bc/MAA/20", 1, "MAA",  2) + "\n")
            f.write("BC,BASE,21,8,0," + parse_results(f"{RSLT_DIR}_SC/bc/BASE/21/8", 1, "BASE",  4) + "\n")
            f.write("BC,MAA,21,8,1," + parse_results(f"{RSLT_DIR}_SC/bc/MAA/21/8/1", 1, "MAA",  4) + "\n")
            f.write("BC,MAA,21,8,2," + parse_results(f"{RSLT_DIR}_SC/bc/MAA/21/8/2", 1, "MAA",  4) + "\n")
            
            f.write("IS,BASE,b,4,0," + parse_results(f"{RSLT_DIR}/is/BASE/b", 1, "BASE",  2) + "\n")
            f.write("IS,MAA,b,4,1," + parse_results(f"{RSLT_DIR}/is/MAA/b", 1, "MAA",  2) + "\n")
            f.write("IS,BASE,b,8,0," + parse_results(f"{RSLT_DIR}_SC/is/BASE/b/8", 1, "BASE",  4) + "\n")
            f.write("IS,MAA,b,8,1," + parse_results(f"{RSLT_DIR}_SC/is/MAA/b/8/1", 1, "MAA",  4) + "\n")
            f.write("IS,MAA,b,8,2," + parse_results(f"{RSLT_DIR}_SC/is/MAA/b/8/2", 1, "MAA",  4) + "\n")
            
            f.write("CG,BASE,c,4,0," + parse_results(f"{RSLT_DIR}/cg/BASE/c", 1, "BASE",  2) + "\n")
            f.write("CG,MAA,c,4,1," + parse_results(f"{RSLT_DIR}/cg/MAA/c", 1, "MAA",  2) + "\n")
            f.write("CG,BASE,c,8,0," + parse_results(f"{RSLT_DIR}_SC/cg/BASE/c/8", 1, "BASE",  4) + "\n")
            f.write("CG,MAA,c,8,1," + parse_results(f"{RSLT_DIR}_SC/cg/MAA/c/8/1", 1, "MAA",  4) + "\n")
            f.write("CG,MAA,c,8,2," + parse_results(f"{RSLT_DIR}_SC/cg/MAA/c/8/2", 1, "MAA",  4) + "\n")
            
            f.write("PRH,BASE,2M,4,0," + parse_results(f"{RSLT_DIR}/PRH/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("PRH,MAA,2M,4,1," + parse_results(f"{RSLT_DIR}/PRH/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("PRH,BASE,4M,8,0," + parse_results(f"{RSLT_DIR}_SC/PRH/BASE/4M/8", 1, "BASE",  4) + "\n")
            f.write("PRH,MAA,4M,8,1," + parse_results(f"{RSLT_DIR}_SC/PRH/MAA/4M/8/1", 1, "MAA",  4) + "\n")
            f.write("PRH,MAA,4M,8,2," + parse_results(f"{RSLT_DIR}_SC/PRH/MAA/4M/8/2", 1, "MAA",  4) + "\n")
            
            f.write("PRO,BASE,2M,4,0," + parse_results(f"{RSLT_DIR}/PRO/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("PRO,MAA,2M,4,1," + parse_results(f"{RSLT_DIR}/PRO/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("PRO,BASE,4M,8,0," + parse_results(f"{RSLT_DIR}_SC/PRO/BASE/4M/8", 1, "BASE",  4) + "\n")
            f.write("PRO,MAA,4M,8,1," + parse_results(f"{RSLT_DIR}_SC/PRO/MAA/4M/8/1", 1, "MAA",  4) + "\n")
            f.write("PRO,MAA,4M,8,2," + parse_results(f"{RSLT_DIR}_SC/PRO/MAA/4M/8/2", 1, "MAA",  4) + "\n")
            
            f.write("XRAGE,BASE,--,4,0," + parse_results(f"{RSLT_DIR}/spatter/xrage/BASE", 1, "BASE",  2) + "\n")
            f.write("XRAGE,MAA,--,4,1," + parse_results(f"{RSLT_DIR}/spatter/xrage/MAA", 1, "MAA",  2) + "\n")
            f.write("XRAGE,BASE,--,8,0," + parse_results(f"{RSLT_DIR}_SC/spatter/xrage/BASE/8", 1, "BASE",  4) + "\n")
            f.write("XRAGE,MAA,--,8,1," + parse_results(f"{RSLT_DIR}_SC/spatter/xrage/MAA/8/1", 1, "MAA",  4) + "\n")
            f.write("XRAGE,MAA,--,8,2," + parse_results(f"{RSLT_DIR}_SC/spatter/xrage/MAA/8/2", 1, "MAA",  4) + "\n")
            
            f.write("GZP,BASE,2M,4,0," + parse_results(f"{RSLT_DIR}/gradzatp/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZP,MAA,2M,4,1," + parse_results(f"{RSLT_DIR}/gradzatp/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZP,BASE,4M,8,0," + parse_results(f"{RSLT_DIR}_SC/gradzatp/BASE/4M/8", 1, "BASE",  4) + "\n")
            f.write("GZP,MAA,4M,8,1," + parse_results(f"{RSLT_DIR}_SC/gradzatp/MAA/4M/8/1", 1, "MAA",  4) + "\n")
            f.write("GZP,MAA,4M,8,2," + parse_results(f"{RSLT_DIR}_SC/gradzatp/MAA/4M/8/2", 1, "MAA",  4) + "\n")
            
            f.write("GZZ,BASE,2M,4,0," + parse_results(f"{RSLT_DIR}/gradzatz/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZZ,MAA,2M,4,1," + parse_results(f"{RSLT_DIR}/gradzatz/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZZ,BASE,4M,8,0," + parse_results(f"{RSLT_DIR}_SC/gradzatz/BASE/4M/8", 1, "BASE",  4) + "\n")
            f.write("GZZ,MAA,4M,8,1," + parse_results(f"{RSLT_DIR}_SC/gradzatz/MAA/4M/8/1", 1, "MAA",  4) + "\n")
            f.write("GZZ,MAA,4M,8,2," + parse_results(f"{RSLT_DIR}_SC/gradzatz/MAA/4M/8/2", 1, "MAA",  4) + "\n")
            
            f.write("GZPI,BASE,2M,4,0," + parse_results(f"{RSLT_DIR}/gradzatp_invert/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZPI,MAA,2M,4,1," + parse_results(f"{RSLT_DIR}/gradzatp_invert/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZPI,BASE,4M,8,0," + parse_results(f"{RSLT_DIR}_SC/gradzatp_invert/BASE/4M/8", 1, "BASE",  4) + "\n")
            f.write("GZPI,MAA,4M,8,1," + parse_results(f"{RSLT_DIR}_SC/gradzatp_invert/MAA/4M/8/1", 1, "MAA",  4) + "\n")
            f.write("GZPI,MAA,4M,8,2," + parse_results(f"{RSLT_DIR}_SC/gradzatp_invert/MAA/4M/8/2", 1, "MAA",  4) + "\n")
            
            f.write("GZZI,BASE,2M,4,0," + parse_results(f"{RSLT_DIR}/gradzatz_invert/BASE/2M", 1, "BASE",  2) + "\n")
            f.write("GZZI,MAA,2M,4,1," + parse_results(f"{RSLT_DIR}/gradzatz_invert/MAA/2M", 1, "MAA",  2) + "\n")
            f.write("GZZI,BASE,4M,8,0," + parse_results(f"{RSLT_DIR}_SC/gradzatz_invert/BASE/4M/8", 1, "BASE",  4) + "\n")
            f.write("GZZI,MAA,4M,8,1," + parse_results(f"{RSLT_DIR}_SC/gradzatz_invert/MAA/4M/8/1", 1, "MAA",  4) + "\n")
            f.write("GZZI,MAA,4M,8,2," + parse_results(f"{RSLT_DIR}_SC/gradzatz_invert/MAA/4M/8/2", 1, "MAA",  4) + "\n")
            
if args.a == "plot" or args.a == "all":
    if args.b == 'main' or args.b == 'all':
        plot_results("results/results_main.csv", "results", 2)
    if args.b == 'DMP' or args.b == 'all':
        plot_results_DMP("results/results_DMP.csv", "results", 2)
    if args.b == 'TS' or args.b == 'all':
        plot_results_TS("results/results_TS.csv", "results", 2)
    if args.b == 'SC' or args.b == 'all':
        plot_results_SC("results/results_SC.csv", "results", 2, 4)