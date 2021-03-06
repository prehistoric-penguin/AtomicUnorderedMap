/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <unistd.h>

#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

#include "AtomicUnorderedMap.h"

template <class T>
struct non_atomic {
  T value;

  non_atomic() = default;
  non_atomic(const non_atomic &) = delete;
  constexpr /* implicit */ non_atomic(T desired) : value(desired) {}

  T operator+=(T arg) {
    value += arg;
    return load();
  }

  T load(std::memory_order /* order */ = std::memory_order_seq_cst) const {
    return value;
  }

  /* implicit */
  operator T() const { return load(); }

  void store(T desired,
             std::memory_order /* order */ = std::memory_order_seq_cst) {
    value = desired;
  }

  T exchange(T desired,
             std::memory_order /* order */ = std::memory_order_seq_cst) {
    T old = load();
    store(desired);
    return old;
  }

  bool compare_exchange_weak(
      T &expected, T desired,
      std::memory_order /* success */ = std::memory_order_seq_cst,
      std::memory_order /* failure */ = std::memory_order_seq_cst) {
    if (value == expected) {
      value = desired;
      return true;
    }

    expected = value;
    return false;
  }

  bool compare_exchange_strong(
      T &expected, T desired,
      std::memory_order /* success */ = std::memory_order_seq_cst,
      std::memory_order /* failure */ = std::memory_order_seq_cst) {
    if (value == expected) {
      value = desired;
      return true;
    }

    expected = value;
    return false;
  }

  bool is_lock_free() const { return true; }
};
using namespace folly;

template <typename Key, typename Value, typename IndexType,
          template <typename> class Atom = std::atomic,
          typename Allocator = std::allocator<char>>
using UIM =
    AtomicUnorderedInsertMap<Key, Value, std::hash<Key>, std::equal_to<Key>,
                             (std::is_trivially_destructible<Key>::value &&
                              std::is_trivially_destructible<Value>::value),
                             Atom, IndexType, Allocator>;

namespace {
template <typename T>
struct AtomicUnorderedInsertMapTest : public ::testing::Test {};
}  // namespace

// uint16_t doesn't make sense for most platforms, but we might as well
// test it
using IndexTypesToTest = ::testing::Types<uint16_t, uint32_t, uint64_t>;
TYPED_TEST_CASE(AtomicUnorderedInsertMapTest, IndexTypesToTest);

TYPED_TEST(AtomicUnorderedInsertMapTest, basic) {
  UIM<std::string, std::string, TypeParam, std::atomic,
      folly::detail::MMapAlloc>
      m(100);

  m.emplace("abc", "ABC");
  EXPECT_TRUE(m.find("abc") != m.cend());
  EXPECT_EQ(m.find("abc")->first, "abc");
  EXPECT_EQ(m.find("abc")->second, "ABC");
  EXPECT_TRUE(m.find("def") == m.cend());
  auto iter = m.cbegin();
  EXPECT_TRUE(iter != m.cend());
  EXPECT_TRUE(iter == m.find("abc"));
  auto a = iter;
  EXPECT_TRUE(a == iter);
  auto b = iter;
  ++iter;
  EXPECT_TRUE(iter == m.cend());
  EXPECT_TRUE(a == b);
  EXPECT_TRUE(a != iter);
  a++;
  EXPECT_TRUE(a == iter);
  EXPECT_TRUE(a != b);
}

TEST(AtomicUnorderedInsertMap, load_factor) {
  AtomicUnorderedInsertMap<int, bool> m(5000, 0.5f);
  EXPECT_GT(m.SlotsNum(), 5000);

  // we should be able to put in much more than 5000 things because of
  // our load factor request
  for (int i = 0; i < 10000; ++i) {
    m.emplace(i, true);
  }
}

TEST(AtomicUnorderedInsertMap, capacity_exceeded) {
  AtomicUnorderedInsertMap<int, bool> m(5000, 1.0f);

  EXPECT_THROW(
      {
        for (int i = 0; i < 6000; ++i) {
          m.emplace(i, false);
        }
      },
      std::bad_alloc);
}

TYPED_TEST(AtomicUnorderedInsertMapTest, value_mutation) {
  UIM<int, MutableAtom<int>, TypeParam> m(100);

  for (int i = 0; i < 50; ++i) {
    m.emplace(i, i);
  }

  m.find(1)->second.data++;
}

TEST(UnorderedInsertMap, struct_value) {
  UIM<int, MutableData<std::pair<int, int>>, uint32_t, non_atomic> m(100000);

  for (int i = 0; i < 50; ++i) {
    m.emplace(i, std::make_pair(i, i));
  }
  auto it = m.find(48);
  auto it2 = m.find(49);

  for (int i = 50; i < 1000; ++i) {
    m.emplace(i, std::make_pair(i, i));
  }

  m.find(1)->second.data.first++;
  EXPECT_EQ(m.find(1)->second.data.first, 2);
  EXPECT_EQ(it->second.data.first, 48);
  EXPECT_EQ(it2->second.data.first, 49);

  m.find(1)->second.data.first--;
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(m.find(i)->second.data.first, i);
  }
}

void process_mem_usage(double &vm_usage, double &resident_set) {
  using std::ifstream;
  using std::ios_base;
  using std::string;

  vm_usage = 0.0;
  resident_set = 0.0;

  // 'file' stat seems to give the most reliable results
  //
  ifstream stat_stream("/proc/self/stat", ios_base::in);

  // dummy vars for leading entries in stat that we don't care about
  //
  string pid, comm, state, ppid, pgrp, session, tty_nr;
  string tpgid, flags, minflt, cminflt, majflt, cmajflt;
  string utime, stime, cutime, cstime, priority, nice;
  string O, itrealvalue, starttime;

  // the two fields we want
  //
  unsigned long vsize;
  long rss;

  stat_stream >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >>
      tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >>
      stime >> cutime >> cstime >> priority >> nice >> O >> itrealvalue >>
      starttime >> vsize >> rss;  // don't care about the rest

  stat_stream.close();

  long page_size_kb = sysconf(_SC_PAGE_SIZE) /
                      1024;  // in case x86-64 is configured to use 2MB pages
  vm_usage = vsize / 1024.0;
  resident_set = rss * page_size_kb;
}

struct Counter {
  Counter() = default;
  Counter(const Counter &other) {
    a.store(other.a.load());
    b.store(other.b.load());
    c.store(other.c.load());
  }

  std::atomic_int a{0};
  std::atomic_int b{0};
  std::atomic_int c{0};
};

struct Frame {
  uintptr_t frame[32];
};

bool operator==(const Frame &lhs, const Frame &rhs) {
  return std::equal(lhs.frame, lhs.frame + 32, rhs.frame, rhs.frame + 32);
}
struct FrameHash {
  size_t operator()(const Frame &f) const {
    size_t res = 0;
    for (size_t i = 0; i < 32; ++i) {
      res ^= f.frame[i];
    }
    return res;
  }
};

// Memory useage overlook
// VM: 348,332 KB; RSS: 345,676 KB
TEST(UnorderedInsertMap, memory_occupy) {
  folly::AtomicUnorderedInsertMap<Frame, MutableData<Counter>, FrameHash> m(
      1000000);
  Frame f1;
  f1.frame[0] = 0x1;
  f1.frame[1] = 0x2;
  typename folly::AtomicUnorderedInsertMap<Frame, MutableData<Counter>, FrameHash>::IndexType_t a;
  Counter c1;
  m.emplace(f1, c1);

  auto itr = m.find(f1);
  EXPECT_NE(itr, m.cend());
  itr->second.data.a++;

  m.emplace(f1, c1);
  itr->second.data.a++;
  EXPECT_EQ(m.find(f1)->second.data.a, 2);
  double vm, rss;
  process_mem_usage(vm, rss);

  std::cout << "VM: " << vm << "; RSS: " << rss << std::endl;
}

TEST(UnorderedInsertMap, value_mutation) {
  UIM<int, MutableData<int>, uint32_t, non_atomic> m(100);

  for (int i = 1; i < 50; ++i) {
    m.emplace(i, i);
  }

  auto itr = m.find(1);
  EXPECT_NE(itr.get_internal_slot(), 0);

  m.find(1)->second.data++;
  EXPECT_EQ(m.find(1)->second.data, 2);

  for (auto itr = m.cbegin(); itr != m.cend(); ++itr) {
    EXPECT_NE(itr->second.data, 0);
  }
}

// This test is too expensive to run automatically.  On my dev server it
// takes about 10 minutes for dbg build, 2 for opt.
TEST(AtomicUnorderedInsertMap, mega_map) {
  size_t capacity = 2000000;
  AtomicUnorderedInsertMap64<size_t, size_t> big(capacity);
  for (size_t i = 0; i < capacity * 2; i += 2) {
    big.emplace(i, i * 10);
  }
  for (size_t i = 0; i < capacity * 3; i += capacity / 1000 + 1) {
    auto iter = big.find(i);
    if ((i & 1) == 0 && i < capacity * 2) {
      EXPECT_EQ(iter->second, i * 10);
    } else {
      EXPECT_TRUE(iter == big.cend());
    }
  }
}

/*
BENCHMARK(lookup_int_int_hit, iters) {
  std::unique_ptr<AtomicUnorderedInsertMap<int, size_t>> ptr = {};

  size_t capacity = 100000;

  BENCHMARK_SUSPEND {
    ptr = std::make_unique<AtomicUnorderedInsertMap<int, size_t>>(capacity);
    for (size_t i = 0; i < capacity; ++i) {
      auto k = 3 * ((5641 * i) % capacity);
      ptr->emplace(k, k + 1);
      EXPECT_EQ(ptr->find(k)->second, k + 1);
    }
  }

  for (size_t i = 0; i < iters; ++i) {
    size_t k = 3 * (((i * 7919) ^ (i * 4001)) % capacity);
    auto iter = ptr->find(k);
    if (iter == ptr->cend() || iter->second != k + 1) {
      auto jter = ptr->find(k);
      EXPECT_TRUE(iter == jter);
    }
    EXPECT_EQ(iter->second, k + 1);
  }

  BENCHMARK_SUSPEND {
    ptr.reset(nullptr);
  }
}

struct PairHash {
  size_t operator()(const std::pair<uint64_t, uint64_t>& pr) const {
    return pr.first ^ pr.second;
  }
};

*/
/*
void contendedRW(
    size_t itersPerThread,
    size_t capacity,
    size_t numThreads,
    size_t readsPerWrite) {
  typedef std::pair<uint64_t, uint64_t> Key;
  typedef AtomicUnorderedInsertMap<Key, MutableAtom<uint32_t>, PairHash> Map;

  std::unique_ptr<Map> ptr = {};
  std::atomic<bool> go{false};
  std::vector<std::thread> threads;

  BENCHMARK_SUSPEND {
    ptr = std::make_unique<Map>(capacity);
    while (threads.size() < numThreads) {
      threads.emplace_back([&]() {
        while (!go) {
          std::this_thread::yield();
        }

        size_t reads = 0;
        size_t writes = 0;
        while (reads + writes < itersPerThread) {
          auto r = Random::rand32();
          Key key(reads + writes, r);
          if (reads < writes * readsPerWrite ||
              writes >= capacity / numThreads) {
            // read needed
            ++reads;
            auto iter = ptr->find(key);
            EXPECT_TRUE(
                iter == ptr->cend() ||
                iter->second.data.load(std::memory_order_acquire) >= key.first);
          } else {
            ++writes;
            try {
              auto pr = ptr->emplace(key, key.first);
              if (!pr.second) {
                pr.first->second.data++;
              }
            } catch (std::bad_alloc&) {
              LOG(INFO) << "bad alloc";
            }
          }
        }
      });
    }
  }

  go = true;

  for (auto& thr : threads) {
    thr.join();
  }

  BENCHMARK_SUSPEND {
    ptr.reset(nullptr);
  }
}
*/

// clang-format off
// sudo nice -n -20 ~/fbcode/_bin/common/concurrency/experimental/atomic_unordered_map --benchmark --bm_min_iters=1000000
//
// without MAP_HUGETLB (default)
//
// ============================================================================
// common/concurrency/experimental/AtomicUnorderedMapTest.cpprelative  time/iter
//   iters/s
// ============================================================================
// lookup_int_int_hit                                          20.05ns   49.89M
// contendedRW(small_32thr_99pct)                              70.36ns   14.21M
// contendedRW(large_32thr_99pct)                             164.23ns    6.09M
// contendedRW(large_32thr_99_9pct)                           158.81ns    6.30M
// ============================================================================
//
// with MAP_HUGETLB hacked in
// ============================================================================
// lookup_int_int_hit                                          19.67ns   50.84M
// contendedRW(small_32thr_99pct)                              62.46ns   16.01M
// contendedRW(large_32thr_99pct)                             119.41ns    8.37M
// contendedRW(large_32thr_99_9pct)                           111.23ns    8.99M
// ============================================================================
// clang-format on
/*
BENCHMARK_NAMED_PARAM(contendedRW, small_32thr_99pct, 100000, 32, 99)
BENCHMARK_NAMED_PARAM(contendedRW, large_32thr_99pct, 100000000, 32, 99)
BENCHMARK_NAMED_PARAM(contendedRW, large_32thr_99_9pct, 100000000, 32, 999)

BENCHMARK_DRAW_LINE();
*/

// clang-format off
// sudo nice -n -20 ~/fbcode/_build/opt/site_integrity/quasar/experimental/atomic_unordered_map_test --benchmark --bm_min_iters=10000
// Single threaded benchmarks to test how much better we are than
// std::unordered_map and what is the cost of using atomic operations
// in the uncontended use case
// ============================================================================
// std_map                                                      1.20ms   832.58
// atomic_fast_map                                            511.35us    1.96K
// fast_map                                                   196.28us    5.09K
// ============================================================================
// clang-format on

/*
BENCHMARK(std_map) {
  std::unordered_map<long, long> m;
  m.reserve(10000);
  for (int i = 0; i < 10000; ++i) {
    m.emplace(i, i);
  }

  for (int i = 0; i < 10000; ++i) {
    auto a = m.find(i);
    folly::doNotOptimizeAway(&*a);
  }
}

BENCHMARK(atomic_fast_map) {
  UIM<long, long, uint32_t, std::atomic> m(10000);
  for (int i = 0; i < 10000; ++i) {
    m.emplace(i, i);
  }

  for (int i = 0; i < 10000; ++i) {
    auto a = m.find(i);
    folly::doNotOptimizeAway(&*a);
  }
}

BENCHMARK(fast_map) {
  UIM<long, long, uint32_t, non_atomic> m(10000);
  for (int i = 0; i < 10000; ++i) {
    m.emplace(i, i);
  }

  for (int i = 0; i < 10000; ++i) {
    auto a = m.find(i);
    folly::doNotOptimizeAway(&*a);
  }
}

BENCHMARK(atomic_fast_map_64) {
  UIM<long, long, uint64_t, std::atomic> m(10000);
  for (int i = 0; i < 10000; ++i) {
    m.emplace(i, i);
  }

  for (int i = 0; i < 10000; ++i) {
    auto a = m.find(i);
    folly::doNotOptimizeAway(&*a);
  }
}

BENCHMARK(fast_map_64) {
  UIM<long, long, uint64_t, non_atomic> m(10000);
  for (int i = 0; i < 10000; ++i) {
    m.emplace(i, i);
  }

  for (int i = 0; i < 10000; ++i) {
    auto a = m.find(i);
    folly::doNotOptimizeAway(&*a);
  }
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  int rv = RUN_ALL_TESTS();
  folly::runBenchmarksOnFlag();
  return rv;
}
*/
// TODO struct as value
