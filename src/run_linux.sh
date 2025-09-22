#!/bin/bash

# EEL6528 Lab 1 - Linux Runner Script
# This script helps you compile and run the lab code on Linux systems

set -e  # Exit on any error

echo "=========================================="
echo "EEL6528 Lab 1 - Linux Runner"
echo "=========================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
    echo -e "${BLUE}$1${NC}"
}

# Check if we're on Linux
if [[ "$OSTYPE" != "linux-gnu"* ]]; then
    print_warning "This script is designed for Linux. You may need to modify commands for other systems."
fi

# Function to check if UHD is installed
check_uhd() {
    print_status "Checking UHD installation..."
    
    if command -v uhd_find_devices &> /dev/null; then
        print_status "UHD tools found"
        uhd_find_devices --args="type=usrp" 2>/dev/null | head -5 || true
    else
        print_warning "UHD tools not found"
    fi
    
    if pkg-config --exists uhd 2>/dev/null; then
        UHD_VERSION=$(pkg-config --modversion uhd)
        print_status "UHD library found (version: $UHD_VERSION)"
    else
        print_warning "UHD library not found"
    fi
}

# Function to install dependencies
install_deps() {
    print_header "Installing dependencies..."
    
    # Detect package manager
    if command -v apt &> /dev/null; then
        print_status "Using apt package manager (Ubuntu/Debian)"
        sudo apt update
        sudo apt install -y build-essential libuhd-dev uhd-host pkg-config
    elif command -v yum &> /dev/null; then
        print_status "Using yum package manager (CentOS/RHEL)"
        sudo yum groupinstall -y "Development Tools"
        sudo yum install -y uhd-devel uhd
    elif command -v dnf &> /dev/null; then
        print_status "Using dnf package manager (Fedora)"
        sudo dnf groupinstall -y "Development Tools"
        sudo dnf install -y uhd-devel uhd
    elif command -v pacman &> /dev/null; then
        print_status "Using pacman package manager (Arch Linux)"
        sudo pacman -S base-devel uhd
    else
        print_error "Unknown package manager. Please install build-essential, libuhd-dev, and uhd-host manually."
        exit 1
    fi
    
    print_status "Dependencies installed successfully!"
}

# Function to compile simulation version
compile_simulation() {
    print_header "Compiling simulation version..."
    g++ -std=c++17 -O3 -pthread -DSIMULATE_MODE -o lab1_sim lab1.cpp
    print_status "Simulation version compiled successfully!"
}

# Function to compile hardware version
compile_hardware() {
    print_header "Compiling hardware version..."
    
    if ! pkg-config --exists uhd; then
        print_error "UHD library not found. Please install dependencies first."
        echo "Run: $0 --install-deps"
        exit 1
    fi
    
    g++ -std=c++17 -O3 -pthread -o lab1_hardware lab1_hardware.cpp -luhd
    print_status "Hardware version compiled successfully!"
}

# Function to compile N210 version
compile_n210() {
    print_header "Compiling N210 version..."
    
    if ! pkg-config --exists uhd; then
        print_error "UHD library not found. Please install dependencies first."
        echo "Run: $0 --install-deps"
        exit 1
    fi
    
    g++ -std=c++17 -O3 -pthread -o lab1_n210 lab1_n210.cpp -luhd
    print_status "N210 version compiled successfully!"
}

# Function to run simulation test
run_test() {
    print_header "Running simulation test..."
    
    if [[ ! -f "lab1_sim" ]]; then
        print_status "Simulation executable not found. Compiling first..."
        compile_simulation
    fi
    
    print_status "Running 3-second test with 1MHz sampling rate and 2 threads..."
    ./lab1_sim 1e6 2 3
}

# Function to show usage examples
show_examples() {
    print_header "Usage Examples:"
    echo ""
    echo "1. Quick test (simulation):"
    echo "   $0 --test"
    echo ""
    echo "2. Compile and run simulation:"
    echo "   $0 --simulation"
    echo "   ./lab1_sim [sampling_rate] [num_threads] [run_time]"
    echo "   ./lab1_sim 5e6 4 30"
    echo ""
    echo "3. Compile hardware version:"
    echo "   $0 --hardware"
    echo "   ./lab1_hardware 1e6 2 10"
    echo ""
    echo "4. Compile N210 version:"
    echo "   $0 --n210"
    echo "   ./lab1_n210 5e6 4 30"
    echo ""
    echo "5. Lab Question 2 (sampling rate testing):"
    echo "   ./lab1_sim 1e6 2 30    # 1 MHz"
    echo "   ./lab1_sim 5e6 2 30    # 5 MHz"
    echo "   ./lab1_sim 10e6 2 30   # 10 MHz"
    echo "   ./lab1_sim 20e6 2 30   # 20 MHz"
    echo ""
    echo "6. Lab Question 3 (thread scaling):"
    echo "   ./lab1_sim 5e6 1 30    # 1 thread"
    echo "   ./lab1_sim 5e6 2 30    # 2 threads"
    echo "   ./lab1_sim 5e6 4 30    # 4 threads"
    echo "   ./lab1_sim 5e6 8 30    # 8 threads"
}

# Main script logic
case "$1" in
    --install-deps)
        install_deps
        check_uhd
        ;;
    --check-uhd)
        check_uhd
        ;;
    --simulation)
        compile_simulation
        echo ""
        print_status "Ready to run! Example: ./lab1_sim 5e6 4 30"
        ;;
    --hardware)
        compile_hardware
        echo ""
        print_status "Ready to run! Example: ./lab1_hardware 1e6 2 10"
        ;;
    --n210)
        compile_n210
        echo ""
        print_status "Ready to run! Example: ./lab1_n210 5e6 4 30"
        ;;
    --test)
        run_test
        ;;
    --examples)
        show_examples
        ;;
    --all)
        print_header "Building all versions..."
        compile_simulation
        if pkg-config --exists uhd; then
            compile_hardware
            compile_n210
        else
            print_warning "Skipping hardware versions (UHD not installed)"
        fi
        ;;
    --help|"")
        echo "EEL6528 Lab 1 - Linux Runner Script"
        echo ""
        echo "Usage: $0 [OPTION]"
        echo ""
        echo "Options:"
        echo "  --install-deps   Install UHD and build dependencies"
        echo "  --check-uhd      Check UHD installation status"
        echo "  --simulation     Compile simulation version only"
        echo "  --hardware       Compile hardware version"
        echo "  --n210          Compile N210 specific version"
        echo "  --test          Compile and run quick simulation test"
        echo "  --all           Compile all versions"
        echo "  --examples      Show usage examples"
        echo "  --help          Show this help message"
        echo ""
        print_status "For quick start, run: $0 --test"
        ;;
    *)
        print_error "Unknown option: $1"
        echo "Run '$0 --help' for usage information"
        exit 1
        ;;
esac

echo ""
print_status "Done!"
