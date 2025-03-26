MAGNUM=/data4/arkhadem/gem5-hpc

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/bfs/BASE/22/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new/bfs/DMP/22/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/bfs/MAA/22/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/pr/BASE/22/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/pr/DMP/22/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/pr/MAA/22/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/sssp/BASE/22/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new/sssp/DMP/22/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/sssp/MAA/22/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/bc/BASE/20/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/bc/DMP/20/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/bc/MAA/20/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/is/BASE/b/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/is/DMP/b/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/is/MAA/b/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/cg/BASE/c/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/cg/DMP/c/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/cg/MAA/c/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/PRH/BASE/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/PRH/DMP/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/PRH/MAA/2M/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/PRO/BASE/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/PRO/DMP/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/PRO/MAA/2M/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/spatter/xrage/BASE/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/spatter/xrage/DMP/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/spatter/xrage/MAA/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatp/BASE/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/gradzatp/DMP/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatp/MAA/2M/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatz/BASE/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/gradzatz/DMP/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatz/MAA/2M/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatp_invert/BASE/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/gradzatp_invert/DMP/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatp_invert/MAA/2M/  --mode maa --target 1 --no_mcpat_run

python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatz_invert/BASE/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_DAE_new2/gradzatz_invert/DMP/2M/  --mode base --target 1 --no_mcpat_run
python3 configs/parse_gem5.py --dir ${MAGNUM}/results_new/gradzatz_invert/MAA/2M/  --mode maa --target 1 --no_mcpat_run
