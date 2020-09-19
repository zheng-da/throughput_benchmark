all: mem_throughput
	g++ -O0 mem_throughput.cc -o mem_throughput -lpthread
