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


---

## v. Coding listing
