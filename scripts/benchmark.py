import argparse
from sim import run_simulation
from parse import parse_results
from parse import parse_header
from parse import depict_results
import os

DATA_DIR = "/data4/arkhadem/gem5-hpc"
RSLT_DIR = f"{DATA_DIR}/results_AE"

parser = argparse.ArgumentParser(description='Benchmarking script')
parser.add_argument('-j', type=int, default=8, help='Number of parallel simulations')
parser.add_argument('-a', type=str, default='all', choices=['simulate', 'parse', 'depict', 'all'], help='Action to perform')
parser.add_argument('-f', action='store_true', help='Rerun finished simulations')
args = parser.parse_args()

if args.a == 'simulate' or args.a == 'all':
    print(f'Simulating with {args.j} parallel processes')
    if args.f:
        print('Rerunning finished simulations too')
    run_simulation(args.j, args.f)

if args.a == 'parse' or args.a == 'all':
    print(f'Processing results')
    os.system("mkdir -p results")
    with open("results/results.csv", "w") as f:
        f.write("kernel,mode,size," + parse_header() + "\n")
        f.write("BFS,BASE,22," + parse_results(f"{RSLT_DIR}/bfs/BASE/22", 1, "BASE",  2) + "\n")
        f.write("BFS,MAA,22," + parse_results(f"{RSLT_DIR}/bfs/MAA/22", 1, "MAA",  2) + "\n")
        f.write("PR,BASE,22," + parse_results(f"{RSLT_DIR}/pr/BASE/22", 1, "BASE",  2) + "\n")
        f.write("PR,MAA,22," + parse_results(f"{RSLT_DIR}/pr/MAA/22", 1, "MAA",  2) + "\n")
        f.write("SSSP,BASE,21," + parse_results(f"{RSLT_DIR}/sssp/BASE/21", 1, "BASE",  2) + "\n")
        f.write("SSSP,MAA,21," + parse_results(f"{RSLT_DIR}/sssp/MAA/21", 1, "MAA",  2) + "\n")
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

if args.a == "depict" or args.a == "all":
    depict_results("results/results.csv", "results", 2)