# EEL6528_Lab
Digital Communication with Software-define Radio Lab

## Lab 1: Multi-threaded RX Streamer with Power Calculation

### Overview
This lab implements a multi-threaded RX streamer with power calculation for Software-Defined Radio (SDR) applications using USRP N210 hardware.

### Project Structure
```
├── src/                    # Source code files
│   ├── lab1.cpp           # Main implementation with simulation/hardware modes
│   ├── lab1_n210.cpp      # Generic N210 hardware version
│   ├── lab1_bob.cpp       # Bob's specific N210 setup
│   ├── Makefile           # Build automation
│   └── other .cpp files   # Additional implementations
├── report/                # Lab report and documentation
├── README.md              # This file
└── setup files           # Build scripts and configuration
```

### Requirements
- Multi-threaded RX streamer with power calculation
- Test different sampling rates (1MHz to 25MHz)
- Test thread scaling (1 to 8 processing threads)
- Support both simulation and hardware modes
- USRP N210 hardware support

### Building and Running
See the files in `src/` directory for compilation and execution instructions.

### Reference
Based on examples from: https://tanfwong.github.io/sdr_notes/ch2/prelims_exs.html