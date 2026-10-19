# RouteLens OSM road-network package (loader + graph structure only).
#   make          build bin/routelens_osm_info
#   make test     build and run bin/test_osm
.RECIPEPREFIX = >
CC  ?= cc
CXX ?= g++
CFLAGS   := -std=c11 -O2 -g -Wall -Wextra
CXXFLAGS := -std=c++17 -O2 -g -Wall -Wextra
BUILD := build
BIN   := bin

OBJ := $(BUILD)/src/core/graph.o $(BUILD)/src/data/osm_loader.o

.PHONY: all test clean
all: $(BIN)/routelens_osm_info

$(BUILD)/%.o: %.c
> @mkdir -p $(dir $@)
> $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.cpp
> @mkdir -p $(dir $@)
> $(CXX) $(CXXFLAGS) -c $< -o $@

$(BIN)/routelens_osm_info: $(BUILD)/src/app/osm_info_main.o $(OBJ)
> @mkdir -p $(BIN)
> $(CXX) $^ -lm -o $@

$(BIN)/test_osm: $(BUILD)/tests/test_osm.o $(OBJ)
> @mkdir -p $(BIN)
> $(CXX) $^ -lm -o $@

test: $(BIN)/test_osm
> ./$(BIN)/test_osm

clean:
> rm -rf $(BUILD) $(BIN)
