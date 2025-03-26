MAGNUM=/data4/arkhadem/gem5-hpc

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/bfs/BASE/22/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/bfs/MAA/22/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/bfs/BASE/23/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/bfs/MAA/23/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/bfs/BASE/23/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/bfs/MAA/23/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/pr/BASE/22/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/pr/MAA/22/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/pr/BASE/23/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/pr/MAA/23/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/pr/BASE/23/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/pr/MAA/23/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/sssp/BASE/22/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/sssp/MAA/22/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/sssp/BASE/22/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/sssp/MAA/22/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/sssp/BASE/22/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/sssp/MAA/22/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/bc/BASE/20/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/bc/MAA/20/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/bc/BASE/21/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/bc/MAA/21/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/bc/BASE/21/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/bc/MAA/21/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/is/BASE/b/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/is/MAA/b/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/is/BASE/b/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/is/MAA/b/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/is/BASE/b/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/is/MAA/b/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/cg/BASE/c/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/cg/MAA/c/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/cg/BASE/c/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/cg/MAA/c/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/cg/BASE/c/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/cg/MAA/c/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/PRH/BASE/2M/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/PRH/MAA/2M/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/PRH/BASE/4M/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/PRH/MAA/4M/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/PRH/BASE/4M/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/PRH/MAA/4M/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/PRO/BASE/2M/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/PRO/MAA/2M/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/PRO/BASE/4M/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/PRO/MAA/4M/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/PRO/BASE/4M/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/PRO/MAA/4M/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatp/BASE/2M/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatp/MAA/2M/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatp/BASE/4M/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatp/MAA/4M/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatp/BASE/4M/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatp/MAA/4M/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatz/BASE/2M/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatz/MAA/2M/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatz/BASE/4M/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatz/MAA/4M/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatz/BASE/4M/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatz/MAA/4M/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatp_invert/BASE/2M/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatp_invert/MAA/2M/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatp_invert/BASE/4M/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatp_invert/MAA/4M/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatp_invert/BASE/4M/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatp_invert/MAA/4M/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatz_invert/BASE/2M/ --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatz_invert/MAA/2M/ --mode maa --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatz_invert/BASE/4M/8/1/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatz_invert/MAA/4M/8/1/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatz_invert/BASE/4M/8/ --mode base --target 1 --no_mcpat_run --mem_channels 4
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_SC_new/gradzatz_invert/MAA/4M/8/ --mode maa --target 1 --no_mcpat_run --mem_channels 4
