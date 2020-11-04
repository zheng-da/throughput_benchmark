#include <stdlib.h>
#include <stdio.h>

#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>

template<class IdType, int Size>
class cache_line {
  char data[(sizeof(IdType) + sizeof(int32_t)) * Size];
 public:
  cache_line() {
    for (int i = 0; i < Size; i++) {
      set(i, -1, -1);
    }
  }

  void set(int idx, IdType id, int32_t loc) {
    reinterpret_cast<IdType *>(data)[idx] = id;
    reinterpret_cast<int32_t *>(data + sizeof(IdType) * Size)[idx] = loc;
  }

  IdType get_id(int idx) const {
    return reinterpret_cast<const IdType *>(data)[idx];
  }

  int32_t get_loc(int idx) const {
    return reinterpret_cast<const int32_t *>(data + sizeof(IdType) * Size)[idx];
  }

  bool is_init(int idx) const {
    return this->get_id(idx) != -1;
  }

  int get_valid_entries() const {
    int valid = 0;
    for (int i = 0; i < Size; i++) {
      valid += is_init(i);
    }
    return valid;
  }

  int find(IdType id) const {
    for (int i = 0; i < Size; i++) {
      if (get_id(i) == id)
        return i;
    }
    return -1;
  }

  int find_empty_entry() const {
    for (int i = 0; i < Size; i++) {
      if (!is_init(i))
        return i;
    }
    return -1;
  }
};

typedef int32_t IdType;
const int CACHE_LINE_SIZE = 2;

class SACache {
  std::vector<cache_line<IdType, CACHE_LINE_SIZE>> index;
  std::vector<char> data;

  const cache_line<IdType, CACHE_LINE_SIZE> &get_line(IdType id) const {
    // TODO we need a better way to index it.
    return index[id % index.size()];
  }

  cache_line<IdType, CACHE_LINE_SIZE> &get_line(IdType id) {
    // TODO we need a better way to index it.
    return index[id % index.size()];
  }
 public:
  // cache_size is the number of entries in the cache.
  // entry_size is the size of each entry.
  SACache(size_t cache_size, size_t entry_size) {
    data.resize(cache_size * entry_size);
    index.resize(cache_size / CACHE_LINE_SIZE);
  }

  std::vector<bool> add(std::vector<IdType> ids) {
    std::vector<bool> success(ids.size());
    for (size_t i = 0; i < ids.size(); i++) {
      cache_line<IdType, CACHE_LINE_SIZE> &line = get_line(ids[i]);
      int loc = line.find_empty_entry();
      if (loc >= 0) {
        // TODO let's set it to 0 for now.
        line.set(loc, ids[i], 0);
        success[i] = true;
      }
    }
    return success;
  }

  size_t get_valid_entries() const  {
    size_t valid = 0;
    for (size_t i = 0; i < index.size(); i++)
      valid += index[i].get_valid_entries();
    return valid;
  }

  size_t get_capacity() const {
    return index.size() * CACHE_LINE_SIZE;
  }

  std::vector<int32_t> lookup(const std::vector<IdType> &ids) const {
    std::vector<int32_t> locs(ids.size());
#pragma omp parallel for
    for (size_t i = 0; i < ids.size(); i++) {
      const cache_line<IdType, CACHE_LINE_SIZE> &line = get_line(ids[i]);
      locs[i] = line.find(ids[i]);
    }
    return locs;
  }
};

int main() {
  const int SPACE_SIZE = 1000000000;
  SACache cache(10000000, 400);
  printf("#valid entries in the empty cache: %ld\n", cache.get_valid_entries());

  printf("create data\n");
  std::vector<IdType> data(10000000);
#pragma omp parallel for
  for (size_t i = 0; i < data.size(); i++) {
    data[i] = random() % SPACE_SIZE;
  }

  printf("add data to SA cache\n");
  auto success = cache.add(data);
  size_t num_success = 0;
  for (size_t i = 0; i < success.size(); i++) {
    num_success += success[i];
  }
  printf("try to add %ld to a cache with %ld entries, #success: %ld, #valid entries: %ld\n",
         data.size(), cache.get_capacity(), num_success, cache.get_valid_entries());

  /*
  printf("add data to STL hashtable\n");
  std::unordered_map<IdType, int32_t> hashmap;
  for (size_t i = 0; i < data.size(); i++) {
    hashmap[data[i]] = 0;
  }
  */

  printf("create workloads\n");
  std::vector<std::vector<IdType>> workloads(100);
  for (size_t i = 0; i < workloads.size(); i++) {
    workloads[i].resize(1000000);
    for (int j = 0; j < workloads[i].size(); j++) {
      workloads[i][j] = random() % SPACE_SIZE;
    }
  }

  printf("SA cache lookup\n");
  std::vector<std::vector<int32_t>> locs(100);
  auto start = std::chrono::system_clock::now();
  for (size_t i = 0; i < workloads.size(); i++) {
    locs[i] = cache.lookup(workloads[i]);
  }
  auto end = std::chrono::system_clock::now();
  std::chrono::duration<double> elapsed_seconds = end-start;
  printf("lookup takes %.3f seconds. %.3f lookups/second\n", elapsed_seconds.count(),
         locs.size() * locs[0].size() / elapsed_seconds.count());

  /*
  printf("STL hashtable lookup\n");
  start = std::chrono::system_clock::now();
  for (size_t i = 0; i < workloads.size(); i++) {
    for (size_t j = 0;  j < workloads[i].size(); j++) {
      auto it = hashmap.find(workloads[i][j]);
      if (it == hashmap.end())
        locs[i][j] = -1;
      else
        locs[i][j] = it->second;
    }
  }
  end = std::chrono::system_clock::now();
  elapsed_seconds = end-start;
  printf("lookup takes %.3f seconds. %.3f lookups/second\n", elapsed_seconds.count(),
         locs.size() * locs[0].size() / elapsed_seconds.count());
  */

  printf("simple hashtable lookup to verify random memory speed\n");
  std::vector<std::pair<IdType, int32_t>> simple_map(10000000 * 3);
  start = std::chrono::system_clock::now();
  for (size_t i = 0; i < workloads.size(); i++) {
    for (size_t j = 0;  j < workloads[i].size(); j++) {
      auto off = workloads[i][j] % simple_map.size();
      locs[i][j] = simple_map[off].second;
    }
  }
  end = std::chrono::system_clock::now();
  elapsed_seconds = end-start;
  printf("lookup takes %.3f seconds. %.3f lookups/second\n", elapsed_seconds.count(),
         locs.size() * locs[0].size() / elapsed_seconds.count());
}
