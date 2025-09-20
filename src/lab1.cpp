/*
 *
 * EEL6528 Lab 1: Make friends with UHD
 * Multi-threaded Software Defined Radio (SDR) Receiver
 * 
 * COMPILATION:
 * Hardware Mode: g++ -std=c++17 -O3 -o lab1_rx lab1.cpp -luhd -pthread
 * Simulation Mode: g++ -std=c++17 -O3 -DSIMULATE_MODE -o lab1_rx_sim lab1.cpp -pthread
 * 
 * Based on code examples from: https://tanfwong.github.io/sdr_notes/ch2/prelims_exs.html
 */

// UHD includes - only for hardware mode
#ifndef SIMULATE_MODE
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/stream.hpp>
#include <uhd/types/tune_request.hpp>
#endif

#include <iostream>
#include <complex>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <iomanip>

using namespace std;

#ifdef SIMULATE_MODE
// Mock UHD types for simulation
namespace uhd {
    namespace usrp {
        struct multi_usrp {
            typedef shared_ptr<multi_usrp> sptr;
            static sptr make(const string& args) { return nullptr; }
            void set_rx_rate(double rate) {}
            double get_rx_rate() { return 1e6; }
            void set_rx_freq(double freq) {}
            double get_rx_freq() { return 2.437e9; }
            void set_rx_gain(double gain) {}
            double get_rx_gain() { return 30.0; }
            string get_pp_string() { return "Mock USRP"; }
            vector<string> get_rx_sensor_names() { return {}; }
        };
    }
    struct tune_request_t {
        tune_request_t(double f) {}
    };
    struct time_spec_t {};
    struct sensor_value_t {
        string to_pp_string() { return "Mock Sensor"; }
        bool to_bool() { return true; }
    };
    struct rx_streamer {
        typedef shared_ptr<rx_streamer> sptr;
        size_t recv(complex<float>* buff, size_t size, void* md, double timeout) {
            // Generate mock data
            for(size_t i = 0; i < size; i++) {
                buff[i] = complex<float>(0.1f * sin(i * 0.01f), 0.1f * cos(i * 0.01f));
            }
            this_thread::sleep_for(chrono::milliseconds(10)); // Simulate data rate
            return size;
        }
        void issue_stream_cmd(void* cmd) {}
    };
    struct stream_args_t {
        stream_args_t(const string& cpu, const string& wire) {}
    };
    struct stream_cmd_t {
        enum stream_mode_t { STREAM_MODE_START_CONTINUOUS, STREAM_MODE_STOP_CONTINUOUS };
        stream_cmd_t(stream_mode_t mode) : stream_mode(mode) {}
        stream_mode_t stream_mode;
        size_t num_samps;
        bool stream_now;
        time_spec_t time_spec;
    };
    struct rx_metadata_t {
        enum error_code_t { ERROR_CODE_NONE, ERROR_CODE_TIMEOUT, ERROR_CODE_OVERFLOW };
        error_code_t error_code = ERROR_CODE_NONE;
        string strerror() { return "Mock error"; }
    };
}
#endif

// Hardware Configuration
const double RX_FREQ = 2.437e9;        // 2.437 GHz RX carrier frequency
const double RX_RATE = 1e6;            // 1 MHz RX sampling rate
const double RX_GAIN = 30.0;           // 30 dB RX gain
const size_t SAMPLES_PER_BLOCK = 10000; // 10000 samples per block

// Thread control variables
atomic<bool> stop_signal(false);
atomic<size_t> overflow_count(0);

// SampleBlock structure
struct SampleBlock {
    size_t block_number;
    vector<complex<float>> samples;
    
    SampleBlock() : block_number(0) {}
    SampleBlock(size_t num, size_t size) : block_number(num), samples(size) {}
};

// Thread-safe FIFO Queue
class SampleQueue {
private:
    queue<SampleBlock> queue;
    mutex mtx;
    condition_variable cv;
    
public:
    void push(const SampleBlock& block) {
        unique_lock<mutex> lock(mtx);
        queue.push(block);
        cv.notify_one();
    }
    
    bool pop(SampleBlock& block) {
        unique_lock<mutex> lock(mtx);
        
        cv.wait(lock, [this] { 
            return !queue.empty() || stop_signal.load(); 
        });
        
        if (stop_signal.load() && queue.empty()) {
            return false;
        }
        
        if (!queue.empty()) {
            block = queue.front();
            queue.pop();
            return true;
        }
        return false;
    }
    
    size_t size() {
        lock_guard<mutex> lock(mtx);
        return queue.size();
    }
    
    void notify_all() {
        cv.notify_all();
    }
};

// Global queue instance
SampleQueue sample_queue;

// RX Streamer Function
void rx_streamer_thread(uhd::usrp::multi_usrp::sptr usrp, double sampling_rate) {
    
    // Set RX rate
    cout << "Setting RX rate to " << sampling_rate/1e6 << " MHz..." << endl;
    usrp->set_rx_rate(sampling_rate);
    cout << "Actual RX rate: " << usrp->get_rx_rate()/1e6 << " MHz" << endl;
    
    // Set RX frequency
    cout << "Setting RX frequency to " << RX_FREQ/1e9 << " GHz..." << endl;
    uhd::tune_request_t tune_request(RX_FREQ);
    usrp->set_rx_freq(tune_request);
    cout << "Actual RX frequency: " << usrp->get_rx_freq()/1e9 << " GHz" << endl;
    
    // Set RX gain
    cout << "Setting RX gain to " << RX_GAIN << " dB..." << endl;
    usrp->set_rx_gain(RX_GAIN);
    cout << "Actual RX gain: " << usrp->get_rx_gain() << " dB" << endl;
    
    // Wait for setup time
    this_thread::sleep_for(chrono::seconds(1));
    
    // Check LO locked sensor
    std::vector<std::string> sensor_names = usrp->get_rx_sensor_names();
    if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked") != sensor_names.end()) {
        uhd::sensor_value_t lo_locked = usrp->get_rx_sensor("lo_locked");
        std::cout << "LO Locked: " << lo_locked.to_pp_string() << std::endl;
        if (!lo_locked.to_bool()) {
            std::cerr << "Failed to lock LO" << std::endl;
            return;
        }
    }
    
    // Create RX streamer
    uhd::stream_args_t stream_args("fc32", "sc16");  // CPU format, Wire format
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    
    // Allocate buffer for samples
    std::vector<std::complex<float>> buff(SAMPLES_PER_BLOCK);
    
    // Setup streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.num_samps = 0;  // Continuous mode
    stream_cmd.stream_now = true;
    stream_cmd.time_spec = uhd::time_spec_t();
    
    // Start streaming
    rx_stream->issue_stream_cmd(stream_cmd);
    std::cout << "\n=== RX Streaming Started ===" << std::endl;
    
    // RX metadata
    uhd::rx_metadata_t md;
    
    // Block counter
    size_t block_counter = 0;
    
    // Main streaming loop
    while (!stop_signal.load()) {
        // Receive samples
        size_t num_rx_samps = rx_stream->recv(
            &buff.front(),
            buff.size(),
            md,
            3.0  // timeout
        );
        
        // Handle errors
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cerr << "Timeout while receiving" << std::endl;
            break;
        }
        
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            // Overflow - samples were dropped
            overflow_count++;
            std::cerr << "O";  // Print 'O' for overflow
            std::cerr.flush();
            continue;
        }
        
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            std::cerr << "Receive error: " << md.strerror() << std::endl;
            break;
        }
        
        // Push complete block to queue
        if (num_rx_samps == SAMPLES_PER_BLOCK) {
            SampleBlock block(block_counter++, SAMPLES_PER_BLOCK);
            block.samples = buff;
            sample_queue.push(block);
        }
    }
    
    // Stop streaming
    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_stream->issue_stream_cmd(stream_cmd);
    
    std::cout << "\n=== RX Streaming Stopped ===" << std::endl;
    std::cout << "Total blocks transmitted: " << block_counter << std::endl;
    std::cout << "Total overflows: " << overflow_count.load() << std::endl;
}

// Processing Thread Function
void processing_thread(int thread_id) {
    std::cout << "Processing thread " << thread_id << " started" << std::endl;
    
    size_t blocks_processed = 0;
    
    while (!stop_signal.load()) {
        SampleBlock block;
        
        // Get block from queue
        if (!sample_queue.pop(block)) {
            break;  // Stop signal received
        }
        
        // Calculate average power: (1/N) * sum(|x[n]|^2)
        double sum_power = 0.0;
        for (const auto& sample : block.samples) {
            // |x|^2 = real^2 + imag^2
            sum_power += std::norm(sample);
        }
        double avg_power = sum_power / block.samples.size();
        
        // Print results (thread-safe with cout)
        std::cout << std::fixed << std::setprecision(8);
        std::cout << "[Thread " << thread_id << "] "
                  << "Block #" << std::setw(6) << block.block_number 
                  << " | Avg Power: " << std::setw(14) << avg_power
                  << " | Queue Size: " << sample_queue.size()
                  << std::endl;
        
        blocks_processed++;
    }
    
    std::cout << "Processing thread " << thread_id 
              << " stopped. Processed " << blocks_processed << " blocks" << std::endl;
}

// Main Function
int main(int argc, char* argv[]) {
    
    // Parse command line arguments
    double sampling_rate = RX_RATE;
    int num_threads = 2;  // Default 2 processing threads
    double run_time = 10.0;  // Default run for 10 seconds
    
    if (argc > 1) {
        sampling_rate = std::stod(argv[1]);
        std::cout << "Using sampling rate: " << sampling_rate/1e6 << " MHz" << std::endl;
    }
    if (argc > 2) {
        num_threads = std::stoi(argv[2]);
        std::cout << "Using " << num_threads << " processing threads" << std::endl;
    }
    if (argc > 3) {
        run_time = std::stod(argv[3]);
        std::cout << "Running for " << run_time << " seconds" << std::endl;
    }
    
    // Print usage if needed
    if (argc == 1) {
        std::cout << "Usage: " << argv[0] << " [sampling_rate] [num_threads] [run_time_seconds]" << std::endl;
        std::cout << "Example: " << argv[0] << " 5e6 4 30" << std::endl;
        std::cout << "Using defaults: rate=" << sampling_rate/1e6 << "MHz, threads=" << num_threads 
                  << ", time=" << run_time << "s" << std::endl;
    }
    
    // Create USRP device
    std::cout << "\n=== Creating USRP device ===" << std::endl;
    std::string device_args = "";  // Empty for default
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_args);
    
    // Print device info
    std::cout << "Using device: " << usrp->get_pp_string() << std::endl;
    
    // Create vector to hold threads
    std::vector<std::thread> threads;
    
    // Start RX streamer thread
    threads.emplace_back(rx_streamer_thread, usrp, sampling_rate);
    
    // Start processing threads
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(processing_thread, i + 1);
    }
    
    // Run for specified time
    std::cout << "\n=== Running for " << run_time << " seconds ===" << std::endl;
    std::cout << "Carrier Frequency: " << RX_FREQ/1e9 << " GHz" << std::endl;
    std::cout << "Sampling Rate: " << sampling_rate/1e6 << " MHz" << std::endl;
    std::cout << "Samples per Block: " << SAMPLES_PER_BLOCK << std::endl;
    std::cout << "Processing Threads: " << num_threads << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // Wait for specified duration
    std::this_thread::sleep_for(std::chrono::duration<double>(run_time));
    
    // Signal threads to stop
    std::cout << "\n=== Stopping threads ===" << std::endl;
    stop_signal.store(true);
    sample_queue.notify_all();  // Wake up any waiting threads
    
    // Join all threads
    for (auto& t : threads) {
        t.join();
    }
    
    // Print final statistics
    std::cout << "\n=== Final Statistics ===" << std::endl;
    std::cout << "Total Overflows: " << overflow_count.load() << std::endl;
    
    // Performance analysis for lab questions
    if (overflow_count.load() > 0) {
        std::cout << "WARNING: Overflows detected at " << sampling_rate/1e6 
                  << " MHz sampling rate!" << std::endl;
        std::cout << "This indicates the host computer cannot keep up with the data rate." << std::endl;
    } else {
        std::cout << "SUCCESS: No overflows at " << sampling_rate/1e6 
                  << " MHz sampling rate." << std::endl;
    }
    
    std::cout << "\nProgram finished!" << std::endl;
    
    return 0;
}

// Lab Questions Helper
// Question 2: Testing different sampling rates
// Run these commands and observe CPU usage with 'top -H':
//   ./lab1_rx 1e6 2 30    # 1 MHz - should work fine
//   ./lab1_rx 5e6 2 30    # 5 MHz - may see higher CPU
//   ./lab1_rx 10e6 2 30   # 10 MHz - likely to see overflows
//   ./lab1_rx 20e6 2 30   # 20 MHz - expect many overflows
//   ./lab1_rx 25e6 2 30   # 25 MHz - maximum for Gigabit Ethernet
//
// Question 3: Testing different number of threads
// Run these commands to observe thread scaling effects:
//   ./lab1_rx 5e6 1 30    # 1 processing thread
//   ./lab1_rx 5e6 2 30    # 2 processing threads
//   ./lab1_rx 5e6 4 30    # 4 processing threads
//   ./lab1_rx 5e6 8 30    # 8 processing threads
//
// Expected observations:
// - More threads can help process blocks faster
// - Too many threads may cause context switching overhead
// - Optimal thread count depends on number of CPU cores
