all: mem_throughput
	g++ -O3 mem_throughput.cc -o mem_throughput -lpthread
