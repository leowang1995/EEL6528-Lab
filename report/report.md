# EEL6528 Lab 1: Make friends with UHD Report

## I. Introduction
In Lab 1: Make Friends with UHD, the UHD C++ APIs are used to construct a USRP RX object that continuously streams received signal samples from the USRP N210 radio to the host PC. These samples are placed into a FIFO queue in blocks. The power of each block is calculated and the averaged value is printed. The RX carrier frequency is set to 2.437 GHz, and the sampling rate is 1 MHz.

According to the requirements, the RX sample streamer function continuously collects blocks of 10,000 complex-valued samples from the radio, numbers the blocks consecutively, and pushes them into the FIFO queue. The processing function pops one block at a time from the FIFO queue, calculates the average power of the block, and prints the block number together with its average power value. 

---

## ii. Solution Approach
To achieve the requirements, I implemented several classes to handle different functionalities. The SampleQueue class provides a queue structure to add sample blocks and pop them for analysis. In the rx_streamer_thread() function, the block counter is incremented with each new block and passed to SampleBlock. The push() function adds sample blocks to the back of the queue, ensuring FIFO behavior, while the pop() function removes sample blocks from the front of the queue. The pop() function also blocks when the queue is empty until new data arrives.

The signal power analysis section calculates the average power of each received RF signal block. This is done using std::norm(sample) for each complex sample. The powers of all samples in the block are summed and then divided by the block size to obtain the average power.

---

## iii. Implementation
In the program implementation, there are three default threads. The first is the RX streamer thread, which is responsible for configuring the USRP hardware, continuously receiving complex samples from the radio, pushing completed sample blocks into the FIFO queue, and handling overflow detection.

In addition, there are two processing threads that retrieve sample blocks from the FIFO queue, calculate the average signal power for each block, and display the results. By default, two processing threads are created, but the number of processing threads can be adjusted through command-line arguments for performance testing or scalability experiments.

Each processing thread also manages the output stage, ensuring that the block number and corresponding average power are displayed in real time.

The key functions and classes that support these operations are described in detail in the Solution Approach section.

---

## iv. Results
In this section, the testing results are shown for question 2, observe and report what happens as the sampling rate increases. The screenshot for different sampling rates are store in [`question2/`](../report/results/question2) directory. From the testing, it shows that along with the RX sampling rate beyond 1 MHz, the average CPU usage is 3.6%, 6.0%, 14.8%, and 26.6% for 1 MHz, 2MHz, 10 MHz, and 25 MHz, respectively. Therefore, when the sampling rate is increasing, the average CPU usage is increased significantly. However, the peak memeory usage for each RX sampling rate is not changing much from the testing, the peak memory usage is around 20600 KB for all cases. There was no overflow happened for the aboved RX sampling rate. In order to observe the overflow, the sampling rate is increased to 50 MHz, then the sampling rate is observed as shown in [`overflow/`](../report/results/question2/50MHz_1.png).

---

## v. Coding listing
All source code for this lab is located in the [`src/`](../src) directory.  
Below are direct links to each file:  

- [lab1.cpp](../src/lab1.cpp) – Base program for lab1 
- [lab1_sim.cpp](../src/lab1_sim.cpp) – Simulation program without USRP N210
- [lab1_bob.cpp](../src/lab1_bob.cpp) – Program works with hardware (USRP N210)
