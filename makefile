CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O3 -DNDEBUG -march=native

CORE := OrderArena.cpp OrderBook.cpp BitmapOrderBook.cpp MatchingEngine.cpp

.PHONY: all test bench clean

all: test_runner bench_runner

test_runner: tests/test_main.cpp $(CORE)
$(CXX) $(CXXFLAGS) $^ -o $@

bench_runner: benchmark/bench_main.cpp $(CORE)
$(CXX) $(CXXFLAGS) $^ -o $@

test: test_runner
./test_runner

bench: bench_runner
./bench_runner

clean:
rm -f test_runner bench_runner *.o *.d tests/*.o benchmark/*.o