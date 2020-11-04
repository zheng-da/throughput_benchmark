#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include <vector>
#include <chrono>
#include <ctime>
#include <unordered_map>
#include <unordered_set>

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
const int CACHE_LINE_SIZE = 8;

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
    printf("#index rows: %ld\n", index.size());
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

  size_t get_space() const {
    return index.size() * sizeof(cache_line<IdType, CACHE_LINE_SIZE>);
  }

  void lookup(const std::vector<IdType> &ids, std::vector<int32_t> *locs) const {
    assert(ids.size() == locs->size());
#pragma omp parallel for
    for (size_t i = 0; i < ids.size(); i++) {
      const cache_line<IdType, CACHE_LINE_SIZE> &line = get_line(ids[i]);
      (*locs)[i] = line.find(ids[i]);
    }
  }
};

int perf_test(int cache_size, int lookup_runs) {
  const int SPACE_SIZE = 1000000000;
  SACache cache(cache_size, 400);
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
  printf("try to add %ld to a cache with %ld entries (%ld bytes), #success: %ld, #valid entries: %ld\n",
         data.size(), cache.get_capacity(), cache.get_space(), num_success, cache.get_valid_entries());

  /*
  printf("add data to STL hashtable\n");
  std::unordered_map<IdType, int32_t> hashmap;
  for (size_t i = 0; i < data.size(); i++) {
    hashmap[data[i]] = 0;
  }
  */

  printf("create workloads\n");
  std::vector<std::vector<IdType>> workloads(lookup_runs);
  for (size_t i = 0; i < workloads.size(); i++) {
    workloads[i].resize(1000000);
    for (int j = 0; j < workloads[i].size(); j++) {
      workloads[i][j] = random() % SPACE_SIZE;
    }
  }

  printf("SA cache lookup\n");
  std::vector<std::vector<int32_t>> locs(workloads.size());
  for (size_t i = 0; i < locs.size(); i++) {
    locs[i].resize(workloads[i].size(), -1);
  }
  auto start = std::chrono::system_clock::now();
  for (size_t i = 0; i < workloads.size(); i++) {
    cache.lookup(workloads[i], &locs[i]);
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
  std::vector<std::pair<IdType, int32_t>> simple_map(cache_size * 3);
  start = std::chrono::system_clock::now();
  for (size_t i = 0; i < workloads.size(); i++) {
#pragma omp parallel for
    for (size_t j = 0;  j < workloads[i].size(); j++) {
      auto off = workloads[i][j] % simple_map.size();
      locs[i][j] = simple_map[off].second;
    }
  }
  end = std::chrono::system_clock::now();
  elapsed_seconds = end-start;
  printf("lookup takes %.3f seconds. %.3f lookups/second\n", elapsed_seconds.count(),
         locs.size() * locs[0].size() / elapsed_seconds.count());
  return 0;
}

int unit_test() {
  const int SPACE_SIZE = 100000;
  const int CACHE_SIZE = 1000;
  SACache cache(CACHE_SIZE, 400);
  assert(cache.get_capacity() == CACHE_SIZE);

  std::vector<IdType> data(1000);
  for (size_t i = 0; i < data.size(); i++) {
    data[i] = random() % SPACE_SIZE;
  }

  auto success = cache.add(data);
  size_t num_success = 0;
  for (size_t i = 0; i < success.size(); i++) {
    num_success += success[i];
  }
  printf("try to add %ld to a cache with %ld entries (%ld bytes), #success: %ld, #valid entries: %ld\n",
         data.size(), cache.get_capacity(), cache.get_space(), num_success, cache.get_valid_entries());
  std::unordered_set<IdType> cached_data_set;
  std::vector<IdType> cached_data;
  for (size_t i = 0; i < success.size(); i++) {
    if (success[i]) {
      cached_data_set.insert(data[i]);
      cached_data.push_back(data[i]);
    }
  }

  std::vector<IdType> workloads(10000);
  for (size_t i = 0; i < workloads.size(); i++) {
    workloads[i] = random() % SPACE_SIZE;
  }

  std::vector<int32_t> locs(workloads.size());
  cache.lookup(workloads, &locs);
  for (size_t i = 0; i < workloads.size(); i++) {
    auto it = cached_data_set.find(workloads[i]);
    assert((it == cached_data_set.end()) == (locs[i] == -1));
  }

  locs.resize(cached_data.size());
  cache.lookup(cached_data, &locs);
  for (size_t i = 0; i < cached_data.size(); i++) {
    assert(locs[i] >= 0);
  }
  return 0;
}

int main() {
  perf_test(1000000, 100);
  perf_test(1000000, 1000);
  perf_test(10000000, 100);
  perf_test(10000000, 1000);
}
