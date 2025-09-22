# EEL6528 Lab 1: Make friends with UHD Report

## Introduction
In Lab 1: Make Friends with UHD, the UHD C++ APIs are used to construct a USRP RX object that continuously streams received signal samples from the USRP N210 radio to the host PC. These samples are placed into a FIFO queue in blocks. The power of each block is calculated and the averaged value is printed. The RX carrier frequency is set to 2.437 GHz, and the sampling rate is 1 MHz.

According to the requirements, the RX sample streamer function continuously collects blocks of 10,000 complex-valued samples from the radio, numbers the blocks consecutively, and pushes them into the FIFO queue. The processing function pops one block at a time from the FIFO queue, calculates the average power of the block, and prints the block number together with its average power value. 

---

## Solution Approach
Explain the methods or algorithms you used.  
- Did you use any specific algorithm, formula, or technique?  
- If the solution is straightforward (e.g., direct coding), you can keep this short or omit.  

---

## Implementation
Describe your software design.  
- How many threads did you use?  
- Which thread handles which task?  
- Key functions and modules.  

Example:  
- **Thread 1**: Handles input.  
- **Thread 2**: Performs computation.  
- **Thread 3**: Manages output.  

You can include diagrams or flowcharts as images:
```markdown
![Architecture Diagram](images/architecture.png)
