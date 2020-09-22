#include <string.h>
#include <assert.h>

#include <thread>
#include <vector>
#include <chrono>
#include <ctime>

long mem_size = 4000L*1000*1000;
// The number of memory copies in a thread.
// It seems if we have more than one memory copies, the O3 optimization may do something
// to remove some of the copies.
int num_copies = 10;
// The number of threads should be the same as the number of physical cores.
int num_threads = 16;

auto start0 = std::chrono::system_clock::now();

void memcpy1(void *dst, void *src, size_t size) {
	assert(((long) src) % sizeof(long) == 0);
	assert(((long) dst) % sizeof(long) == 0);
	assert(size % sizeof(long) == 0);
	long *dst1 = (long *) dst;
	long *src1 = (long *) src;
#pragma simd
	for (size_t i = 0; i < size / sizeof(long); i++) {
		dst1[i] = src1[i];
	}
}

void seq_copy_mem(double &throughput)
{
	char *src = (char *) malloc(mem_size);
	char *dst = (char *) malloc(mem_size);
	memset(src, 0, mem_size);
	memset(dst, 0, mem_size);

	auto copy_start = std::chrono::system_clock::now();
	std::chrono::duration<double> rel_start = copy_start - start0;
	printf("sequential copy start: %f\n", rel_start.count());
	
	auto start = std::chrono::system_clock::now();
	for (int i = 0; i < num_copies; i++) {
		memcpy1(dst, src, mem_size);
	}
	auto end = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed_seconds = end-start;
	free(src);
	free(dst);
	throughput = (mem_size * num_copies) / elapsed_seconds.count();

	std::chrono::duration<double> rel_end = end - start0;
	printf("sequential copy end: %f\n", rel_end.count());
}

std::vector<long> offsets;
int stride = 400;

void rand_copy_mem(int thread_id, double &throughput)
{
	long num_offsets = offsets.size() / num_threads;
	long copy_size = num_offsets * stride;
	const long *offset_start = &offsets[thread_id * num_offsets];
	char *src = (char *) malloc(mem_size);
	char *dst = (char *) malloc(copy_size);
	memset(src, 0, mem_size);
	memset(dst, 0, copy_size);

	auto copy_start = std::chrono::system_clock::now();
	std::chrono::duration<double> rel_start = copy_start - start0;
	printf("random copy start: %.3f\n", rel_start.count());
	
	auto start = std::chrono::system_clock::now();
	for (int i = 0; i < num_copies; i++) {
		for (size_t i = 0; i < num_offsets; i++) {
			memcpy1(dst + i * stride, src + offset_start[i] * stride, stride);
		}
	}
	auto end = std::chrono::system_clock::now();
	std::chrono::duration<double> elapsed_seconds = end-start;
	free(src);
	free(dst);
	throughput = (copy_size * num_copies) / elapsed_seconds.count();

	std::chrono::duration<double> rel_end = end - start0;
	printf("random copy end: %f\n", rel_end.count());
}

int main()
{
	printf("copy with %d threads\n", num_threads);
	std::vector<std::thread *> threads(num_threads);
	std::vector<double> throughputs(num_threads);

	for (int i = 0; i < num_threads; i++) {
		threads[i] = new std::thread(seq_copy_mem, std::ref(throughputs[i]));
	}
	double throughput = 0;
	for (int i = 0; i < num_threads; i++) {
		threads[i]->join();
		delete threads[i];
		throughput += throughputs[i];
	}
	printf("sequential copy throughput: %f GB/s\n", throughput / 1024 / 1024 / 1024);

	// Calculate the total number of strides in the memory.
	size_t num_strides = mem_size / stride;
	// Calculate the location of the strides that we want to copy.
	offsets.resize(num_strides / 2 * num_threads);
	for (size_t i = 0; i < offsets.size(); i++) {
		offsets[i] = rand() % num_strides;
	}
	for (int i = 0; i < num_threads; i++) {
		threads[i] = new std::thread(rand_copy_mem, i, std::ref(throughputs[i]));
	}
	throughput = 0;
	for (int i = 0; i < num_threads; i++) {
		threads[i]->join();
		delete threads[i];
		throughput += throughputs[i];
	}
	printf("random copy throughput: %f GB/s\n", throughput / 1024 / 1024 / 1024);
}
