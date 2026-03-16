CXX       = g++
CXXFLAGS  = -std=c++17 -Wall -Wextra -Wpedantic -pthread
RELEASE   = -O3 -DNDEBUG -march=native
DEBUG_F   = -O0 -g -fsanitize=address,undefined -DDEBUG
DEPFLAGS  = -MMD -MP

CXXFLAGS += $(RELEASE) $(DEPFLAGS)

# Source files
ENGINE_SRC = OrderArena.cpp OrderBook.cpp MatchingEngine.cpp
ENGINE_OBJ = $(ENGINE_SRC:.cpp=.o)

TARGET    = hft_engine
TEST_BIN  = test_runner
BENCH_BIN = bench_runner

.PHONY: all test bench clean debug

all: $(TARGET)

$(TARGET): main.o $(ENGINE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TEST_BIN): tests/test_main.o $(ENGINE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BENCH_BIN): benchmark/bench_main.o $(ENGINE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

test: $(TEST_BIN)
	@./$(TEST_BIN)

bench: $(BENCH_BIN)
	@./$(BENCH_BIN)

debug:
	$(MAKE) CXXFLAGS="-std=c++17 -Wall -Wextra -Wpedantic -pthread $(DEBUG_F) $(DEPFLAGS)" all

clean:
	rm -f *.o *.d $(TARGET) $(TEST_BIN) $(BENCH_BIN)
	rm -f tests/*.o tests/*.d
	rm -f benchmark/*.o benchmark/*.d

# Pattern rules
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

tests/%.o: tests/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

benchmark/%.o: benchmark/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Auto-generated dependencies
-include $(wildcard *.d tests/*.d benchmark/*.d)
