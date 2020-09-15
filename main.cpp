#include "AtomicUnorderedMap.h"
struct PairHash {
  size_t operator()(const std::pair<uint64_t, uint64_t>& pr) const {
    return pr.first ^ pr.second;
  }
};

void t() {
  typedef std::pair<uint64_t, uint64_t> Key;
  folly::AtomicUnorderedInsertMap<Key, folly::MutableAtom<uint32_t>, PairHash> Map(100);
  Map.emplace(std::make_pair(1, 2), 3);
}

int main(int argc, char* argv[]) {
  folly::AtomicUnorderedInsertMap<int, int> m(32);
  m.emplace(2, 3);
  return 0;
}
