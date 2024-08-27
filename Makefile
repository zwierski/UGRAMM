#######################################################
###########   Makefile for GRAMM          ###########
#######################################################

CXX = g++
CXXFLAGS = -I$(LIB_DIR) -lboost_graph -lspdlog -lfmt


# Directories
SRC_DIR = src
LIB_DIR = lib
BUILD_DIR = build

EXE = GRAMM

# Source files
SRCS = $(SRC_DIR)/GRAMM.cpp $(SRC_DIR)/utilities.cpp

# Object files
OBJS = $(BUILD_DIR)/GRAMM.o $(BUILD_DIR)/utilities.o

# Target for the executable
$(EXE): $(OBJS)
	$(CXX) $(OBJS) $(CXXFLAGS) -o $(EXE)

# Rule to build object files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(LIB_DIR)/%.h
	mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean target to remove generated files
clean:
	rm -f $(EXE) $(BUILD_DIR)/*.o *.dot *.txt *.png