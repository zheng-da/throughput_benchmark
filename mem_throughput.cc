#include <string.h>
#include <assert.h>
#include <immintrin.h>

#include <thread>
#include <vector>
#include <chrono>
#include <ctime>
#include <functional>

long mem_size = 1000L*1000*1000;
// The number of memory copies in a thread.
// It seems if we have more than one memory copies, the O3 optimization may do something
// to remove some of the copies.
int num_copies = 10;
// The number of threads should be the same as the number of physical cores.
int num_threads = 16;

auto start0 = std::chrono::system_clock::now();
typedef std::function<void(void*,void*,size_t)> MemCpyFn;

void memcpy_simd(void *dst, void *src, size_t size) {
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

template<class Reg, class StoreT, class LoadT>
void memcpy_vec(void *pvDest, void *pvSrc, size_t nBytes, StoreT store_fn, LoadT load_fn) {
	if(nBytes<sizeof(Reg)) {
		/* this code can be faster - WIP */
		char* b = reinterpret_cast<char*>(pvSrc);
		char* e = b + nBytes;
		char* out = reinterpret_cast<char*>(pvDest);
		std::copy(b,e,out);
		return;
	}

	for(size_t i=0; i + sizeof(Reg) <= nBytes;i+=sizeof(Reg)) {
		store_fn(reinterpret_cast<Reg*>(reinterpret_cast<char*>(pvDest)+i),
				load_fn(reinterpret_cast<Reg*>(reinterpret_cast<char*>(pvSrc) + i)));
	}

	/* We don't care about this scenario at this moment - WIP */
	size_t left_bytes = nBytes % sizeof(Reg);

	if(left_bytes) {
		/* this code can be faster - WIP */
		char* e = reinterpret_cast<char*>(pvSrc) + nBytes;
		char* b = e - left_bytes;
		char* out = reinterpret_cast<char*>(pvDest) + nBytes - left_bytes;
		std::copy(b,e,out);
	}

}

void memcpy256(void *dst, void *src, size_t size) {
	memcpy_vec<__m256i>(dst, src, size, _mm256_stream_si256, _mm256_stream_load_si256);
}

void memcpy512(void *dst, void *src, size_t size) {
	memcpy_vec<__m512i>(dst, src, size, _mm512_stream_si512, _mm512_stream_load_si512);
}

struct test_copy {
	std::string name;
	MemCpyFn fn;
	test_copy(std::string name, MemCpyFn fn) {
		this->name = name;
		this->fn = fn;
	}
};

void seq_copy_mem(double &throughput, MemCpyFn memcpy1)
{
	char *src = (char *) valloc(mem_size);
	char *dst = (char *) valloc(mem_size);
	memset(src, 0, mem_size);
	memset(dst, 0, mem_size);

	auto copy_start = std::chrono::system_clock::now();
	std::chrono::duration<double> rel_start = copy_start - start0;
	
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
}

std::vector<long> offsets;
int stride = 512;


void rand_copy_mem(int thread_id, double &throughput, MemCpyFn memcpy1)
{
	long num_offsets = offsets.size() / num_threads;
	long copy_size = num_offsets * stride;
	const long *offset_start = &offsets[thread_id * num_offsets];
	char *src = (char *) valloc(mem_size);
	char *dst = (char *) valloc(copy_size);
	memset(src, 0, mem_size);
	memset(dst, 0, copy_size);

	auto copy_start = std::chrono::system_clock::now();
	std::chrono::duration<double> rel_start = copy_start - start0;
	
	auto start = std::chrono::system_clock::now();
	for (int j = 0; j < num_copies; j++) {
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
}

int main()
{
	printf("copy with %d threads\n", num_threads);
	std::vector<std::thread *> threads(num_threads);
	std::vector<double> throughputs(num_threads);

	std::vector<test_copy> tests;
	tests.push_back(test_copy("memcpy", memcpy));
	tests.push_back(test_copy("memcpy_simd", memcpy_simd));
	tests.push_back(test_copy("memcpy512", memcpy512));
	tests.push_back(test_copy("memcpy256", memcpy256));

	for (auto &test : tests) {
		for (int i = 0; i < num_threads; i++) {
			threads[i] = new std::thread(seq_copy_mem, std::ref(throughputs[i]), test.fn);
		}
		double throughput = 0;
		for (int i = 0; i < num_threads; i++) {
			threads[i]->join();
			delete threads[i];
			throughput += throughputs[i];
		}
		printf("sequential copy throughput of %s: %f GB/s\n", test.name.c_str(), throughput / 1024 / 1024 / 1024);

		// Calculate the total number of strides in the memory.
		size_t num_strides = mem_size / stride;
		// Calculate the location of the strides that we want to copy.
		offsets.resize(num_strides * num_threads);
		for (int thread_id = 0; thread_id < num_threads; thread_id++) {
			long *offset_start = &offsets[thread_id * num_strides];
			for (size_t i = 0; i < num_strides; i++)
				offset_start[i] = i;
			for (size_t i = 0; i < num_strides; i++) {
				// shuffle between i and 'rand() % num_strides'
				off_t rand_idx = rand() % num_strides;
				auto tmp = offset_start[i];
				offset_start[i] = offset_start[rand_idx];
				offset_start[rand_idx] = tmp;
			}
		}
		for (int i = 0; i < num_threads; i++) {
			threads[i] = new std::thread(rand_copy_mem, i, std::ref(throughputs[i]), test.fn);
		}
		throughput = 0;
		for (int i = 0; i < num_threads; i++) {
			threads[i]->join();
			delete threads[i];
			throughput += throughputs[i];
		}
		printf("random copy throughput of %s: %f GB/s\n", test.name.c_str(), throughput / 1024 / 1024 / 1024);
	}
}
