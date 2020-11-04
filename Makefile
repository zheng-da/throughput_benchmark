main: mem_throughput.cc
	g++ -O3 -march=native mem_throughput.cc -o mem_throughput -lpthread

cache: cache.cc
	g++ -O3 -march=native -fopenmp cache.cc -o cache -lpthread
