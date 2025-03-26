mkdir build_logs 2>&1 > /dev/null
sleep 2
echo "###### Building GAPBS ######"
cd gapbs
bash build.sh 2>&1 | tee ../build_logs/gapbs.txt
cd ../
echo "###### Building HASH-JOIN ######"
cd hashjoin
bash build.sh 2>&1 | tee ../build_logs/hashjoin.txt
cd ../
echo "###### Building UME ######"
cd UME
bash build.sh 2>&1 | tee ../build_logs/UME.txt
cd ../
echo "###### Building SPATTER ######"
cd spatter
bash build.sh 2>&1 | tee ../build_logs/spatter.txt
cd ../
echo "###### Building NAS ######"
cd NAS
bash build.sh 2>&1 | tee ../build_logs/NAS.txt
cd ../