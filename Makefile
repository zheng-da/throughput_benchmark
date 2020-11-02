main: mem_throughput.cc
	g++ -O3 -march=native mem_throughput.cc -o mem_throughput -lpthread
