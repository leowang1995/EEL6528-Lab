# EEL6528 Lab 1 - Multi-threaded RX Streamer Makefile
# Supports both simulation and hardware versions

CXX = g++
CXXFLAGS = -std=c++17 -O3 -pthread -Wall -Wextra
UHD_LIBS = -luhd

# Default target - build simulation version
all: simulation

# Simulation version (no UHD required)
simulation: lab1_sim
	@echo "Simulation version built successfully!"
	@echo "Run with: ./lab1_sim [sampling_rate] [num_threads] [run_time]"

lab1_sim: lab1.cpp
	$(CXX) $(CXXFLAGS) -DSIMULATE_MODE -o lab1_sim lab1.cpp

# Hardware version (requires UHD library)
hardware: lab1_hardware
	@echo "Hardware version built successfully!"
	@echo "Run with: ./lab1_hardware [sampling_rate] [num_threads] [run_time]"

lab1_hardware: lab1_hardware.cpp
	$(CXX) $(CXXFLAGS) -o lab1_hardware lab1_hardware.cpp $(UHD_LIBS)

# N210 specific version
n210: lab1_n210
	@echo "N210 hardware version built successfully!"
	@echo "Run with: ./lab1_n210 [sampling_rate] [num_threads] [run_time]"

lab1_n210: lab1_n210.cpp
	$(CXX) $(CXXFLAGS) -o lab1_n210 lab1_n210.cpp $(UHD_LIBS)

# Test compilation (simulation only - safe for any system)
test: lab1_sim
	@echo "Running quick test..."
	./lab1_sim 1e6 2 3

# Clean build artifacts
clean:
	rm -f lab1_sim lab1_hardware lab1_n210 *.o

# Install UHD dependencies (Ubuntu/Debian)
install-deps:
	@echo "Installing UHD dependencies..."
	sudo apt update
	sudo apt install -y libuhd-dev uhd-host build-essential

# Check if UHD is installed
check-uhd:
	@echo "Checking UHD installation..."
	@which uhd_find_devices || echo "UHD not found - run 'make install-deps'"
	@pkg-config --exists uhd && echo "UHD library found" || echo "UHD library not found"

# Help target
help:
	@echo "EEL6528 Lab 1 Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build simulation version (default)"
	@echo "  simulation   - Build simulation version (no hardware required)"
	@echo "  hardware     - Build hardware version (requires UHD)"
	@echo "  n210         - Build N210 specific version (requires UHD)"
	@echo "  test         - Build and run quick simulation test"
	@echo "  clean        - Remove build artifacts"
	@echo "  install-deps - Install UHD dependencies (Ubuntu/Debian)"
	@echo "  check-uhd    - Check if UHD is properly installed"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make simulation && ./lab1_sim"
	@echo "  make n210 && ./lab1_n210 5e6 4 30"
	@echo "  make test"

.PHONY: all simulation hardware n210 test clean install-deps check-uhd help
