bash compile.sh GEM5
echo "############## Setting Up XRAGE Dataset ##############"
wget https://web.eecs.umich.edu/~arkhadem/projects/xrage.tar.gz
mkdir -p tests/test-data/xrage/
tar -xzf xrage.tar.gz -C tests/test-data/xrage/