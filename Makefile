default:
	g++ -std=c++17 -g3 main.cpp -pthread
	g++ AtomicUnorderedMapTest.cpp -std=c++14 -lgtest -lgtest_main -pthread -g3 -O0 -o test
