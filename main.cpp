#include "AtomicUnorderedMap.h"
#include <iostream>
struct PairHash {
  size_t operator()(const std::pair<uint64_t, uint64_t>& pr) const {
    return pr.first ^ pr.second;
  }
};

void t() {
  typedef std::pair<uint64_t, uint64_t> Key;
  using map_t  = folly::AtomicUnorderedInsertMap<Key, folly::MutableAtom<uint32_t>, PairHash>;
  map_t Map(100);
  Map.emplace(std::make_pair(1, 2), 3);

  std::cout << "Iterator size:" << sizeof(map_t::ConstIterator) << std::endl;
}

int main(int argc, char* argv[]) {
  folly::AtomicUnorderedInsertMap<int, int> m(32);
  m.emplace(2, 3);
  t();
  return 0;
}
