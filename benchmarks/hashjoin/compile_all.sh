bash compile_x86.sh FUNC DUMP
./src/bin/x86/hj_base -a PRH -n 4 -r 2000000 -s 2000000
./src/bin/x86/hj_base -a PRH -n 4 -r 4000000 -s 4000000
./src/bin/x86/hj_base -a PRH -n 4 -r 8000000 -s 8000000
bash compile_x86.sh $1 LOAD
