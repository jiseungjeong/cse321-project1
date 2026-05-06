CXX      ?= g++
CXXSTD    = -std=c++17
WARN      = -Wall -Wextra -Wpedantic
RELEASE   = -O2 -DNDEBUG
DEBUG     = -O0 -g -fsanitize=address,undefined

CXXFLAGS ?= $(CXXSTD) $(WARN) $(RELEASE)

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)
DEP := $(OBJ:.o=.d)
BIN := run

.PHONY: all debug clean figures

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

debug:
	$(MAKE) clean
	$(MAKE) all CXXFLAGS="$(CXXSTD) $(WARN) $(DEBUG)"

clean:
	rm -f $(OBJ) $(DEP) $(BIN)

# Regenerate analysis figures from results/*.csv. Requires Python 3
# with matplotlib. Output is written into report/project1/figure/ when
# that directory is present locally; the report itself is not part of
# this repository (submitted separately via Blackboard).
figures:
	python3 scripts/plot.py

-include $(DEP)
