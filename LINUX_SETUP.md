# EEL6528 Lab 1 - Running with N210 on Linux

## Quick Start Commands

### 1. Transfer files to Linux box
```bash
# Copy all files to your Linux system
scp -r "C:\EEL6528 Lab\Lab 1\*" username@your-linux-box:~/EEL6528-Lab1/
```

### 2. Connect to Linux box and setup
```bash
# SSH to your Linux box
ssh username@your-linux-box

# Navigate to project directory
cd ~/EEL6528-Lab1

# Make scripts executable
chmod +x run_linux.sh
```

### 3. Install dependencies (first time only)
```bash
# Install UHD and build tools
./run_linux.sh --install-deps

# Or manually:
sudo apt update
sudo apt install -y build-essential libuhd-dev uhd-host pkg-config
```

### 4. Compile N210 version
```bash
# Using the runner script
./run_linux.sh --n210

# Or manually with g++
g++ -std=c++17 -O3 -pthread -o lab1_n210 lab1_n210.cpp -luhd

# Or using Makefile
make n210
```

### 5. Connect and configure N210
```bash
# Find your N210 device
uhd_find_devices

# Test connection (should show your N210)
uhd_find_devices --args="type=usrp"

# If N210 IP is not 192.168.10.2, you may need to configure it
# The code expects N210 at 192.168.10.2 (see line 302 in lab1_n210.cpp)
```

### 6. Run the N210 program
```bash
# Basic run (default: 1MHz, 2 threads, 10 seconds)
./lab1_n210

# Custom parameters: [sampling_rate] [num_threads] [run_time_seconds]
./lab1_n210 5e6 4 30

# Example runs for lab questions:
./lab1_n210 1e6 2 30    # 1 MHz sampling rate
./lab1_n210 5e6 2 30    # 5 MHz sampling rate  
./lab1_n210 10e6 2 30   # 10 MHz sampling rate
./lab1_n210 1e6 1 30    # 1 thread
./lab1_n210 1e6 4 30    # 4 threads
```

## Expected Output
```
=== Connecting to N210 Hardware ===
Using device: USRP N210 (192.168.10.2)
Setting RX rate to 5 MHz...
Actual RX rate: 5 MHz
Setting RX frequency to 2.437 GHz...
Actual RX frequency: 2.437 GHz
Setting RX gain to 20 dB...
Actual RX gain: 20 dB
LO Locked: true

=== RX Streaming Started (N210 Hardware) ===
Processing thread 1 started
Processing thread 2 started

[Thread 1] Block #     0 | Avg Power:   0.00012345 | Queue Size: 0
[Thread 2] Block #     1 | Avg Power:   0.00015678 | Queue Size: 1
...
```

## Troubleshooting

### N210 Not Found
```bash
# Check network connection
ping 192.168.10.2

# Find devices on network
uhd_find_devices --args="addr=192.168.10.2"

# If different IP, modify lab1_n210.cpp line 302:
# std::string device_args = "addr=YOUR_N210_IP";
```

### Compilation Errors
```bash
# Check if UHD is installed
pkg-config --exists uhd && echo "UHD found" || echo "UHD missing"

# Install missing dependencies
sudo apt install -y libuhd-dev uhd-host

# Check compiler
g++ --version
```

### Runtime Errors
```bash
# Check for overflows (appears as 'O' characters)
# Reduce sampling rate if you see many overflows

# Monitor CPU usage
top -H  # Shows per-thread CPU usage

# Check system resources
free -h  # Memory usage
df -h    # Disk space
```

## Network Configuration for N210

### Default N210 Network Setup
- N210 default IP: 192.168.10.2
- Host computer should be on 192.168.10.x network
- Subnet mask: 255.255.255.0

### Configure your Linux network interface
```bash
# Check current network interfaces
ip addr show

# Configure interface (replace eth0 with your interface)
sudo ip addr add 192.168.10.1/24 dev eth0
sudo ip link set eth0 up

# Or use NetworkManager (if available)
sudo nmcli con add type ethernet con-name usrp ifname eth0 ip4 192.168.10.1/24
sudo nmcli con up usrp
```

### Test N210 connection
```bash
# Ping the N210
ping 192.168.10.2

# Find USRP devices
uhd_find_devices

# Get detailed device info
uhd_usrp_probe --args="addr=192.168.10.2"
```

## Performance Tips

### For Lab Question 2 (Sampling Rate Testing)
```bash
# Start with low rates and increase
./lab1_n210 1e6 2 30     # 1 MHz - should work fine
./lab1_n210 5e6 2 30     # 5 MHz - monitor for overflows
./lab1_n210 10e6 2 30    # 10 MHz - may see overflows
./lab1_n210 20e6 2 30    # 20 MHz - expect overflows on slower systems

# Monitor system performance
htop  # or top -H for per-thread view
```

### For Lab Question 3 (Thread Scaling)
```bash
# Test different thread counts
./lab1_n210 5e6 1 30     # 1 processing thread
./lab1_n210 5e6 2 30     # 2 processing threads  
./lab1_n210 5e6 4 30     # 4 processing threads
./lab1_n210 5e6 8 30     # 8 processing threads

# Check CPU core count
nproc  # Shows number of CPU cores
```

## File Summary
- `lab1_n210.cpp` - N210 hardware version (requires UHD)
- `lab1_hardware.cpp` - Generic hardware version  
- `lab1.cpp` - Main file with simulation mode
- `Makefile` - Build automation
- `run_linux.sh` - Helper script for compilation and running
