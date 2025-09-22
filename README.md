[![Review Assignment Due Date](https://classroom.github.com/assets/deadline-readme-button-22041afd0340ce965d47ae6ef1cefeee28c7c493a6346c4f15d667ab976d596c.svg)](https://classroom.github.com/a/6j2Lm4kv)
# EEL6528 Lab 1: Make friends with UHD

## Goals:
- You will practice using the UHD C++ APIs to construct a USRP RX object, set its parameters, and construct a RX streamer to stream signal samples from the USRP N210 radio to the host PC.
- You will also practice spawning multiple threads to run the RX streamer above and to do some simple processing on the signal samples, and learn how to set up a simple thread-safe FIFO queue to pass the samples from the RX streamer thread to the processing threads asynchronously.
- Completing this lab, you will get a taste of what are needed to continuously receive and process samples from the N210 radio.

## What you need to do:
1. Using the [code examples](https://tanfwong.github.io/sdr_notes/ch2/prelims_exs.html#code-examples) in class to write a C++ program to continuously stream RX signal samples from the N210 radio, push blocks of these samples to a FIFO queue, and calculate and print the averaged sample power values of the blocks.
   Programming requirements:
   - The RX carrier frequency should be set to 2.437 GHz. The RX sampling rate should be 1 MHz. You may select the values of other relevant USRP parameters as you see fit.
   - The RX sample streamer function should continuously collect blocks of 10000 complex-valued samples from the radio, number the blocks consecutively, and push them (together with the corresponding block numbers) into a FIFO queue. You may run the RX streamer on the main thread or on another thread.
   - The processing function should pop a sample block from the FIFO queue at a time, find the average power of the block, and print the block number together with the average power value out to `std::cout`. You should instantiate 2 simultaneous copies of the processing function on as many different threads, also not on the thread that runs the RX streamer.

> [!NOTE]
> Suppose there are $N$ samples $x[0], x[1], \ldots, x[N-1]$ in a block. The average power of the block is given by
> $\displaystyle \frac{1}{N} \sum_{n=0}^{N-1} |x[n]|^2$. 
   - All threads should run asynchronously and the output should not be garbled together.

2. Increase the RX sampling rate beyond 1 MHz (recall the restriction that we mentioned in class on the choice of sampling rate). Observe and report what happens as the sampling rate increases, in particular notice the CPU load, memory usage, and overflow occurrence. What is the highest sampling rate of your implementation before buffer overflow occurs?

> [!NOTE]
> You may obtain information about the CPU load and memory usage using the Linux command `top -H`.

3. Observe and report the effects of increasing the number of simultaneous processing threads for different choices of sampling rate.
