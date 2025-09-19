
// Temporary fix for IDE IntelliSense
#ifndef SIMULATE_MODE
#define SIMULATE_MODE
#endif

// EEL6528 Lab 1: Multi-threaded RX Streamer with Power Calculation

//
// Compile for real hardware: g++ -std=c++17 -O3 -o lab1_rx lab1.cpp -luhd -pthread
// Compile for simulation: g++ -std=c++17 -O3 -DSIMULATE_MODE -o lab1_rx_sim lab1.cpp -pthread
// Run: ./lab1_rx (hardware) or ./lab1_rx_sim (simulation)

// UHD includes - simulation mode
#ifndef SIMULATE_MODE
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/stream.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/types/sensors.hpp>
#include <uhd/utils/thread.hpp>
#include <uhd/utils/safe_main.hpp>
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
#include <algorithm>
#include <string>
#include <memory>
#include <cmath>
#include <cstdlib>



#ifdef SIMULATE_MODE
#define SIMULATE_MODE
// Mock UHD types for simulation
namespace uhd {
    // Forward declarations
    struct sensor_value_t;
    struct rx_streamer;
    struct stream_args_t;
    struct stream_cmd_t;
    struct rx_metadata_t;
    struct time_spec_t;
    struct tune_request_t;
    
    struct tune_request_t {
        tune_request_t(double f) {}
    };
    
    struct time_spec_t {};
    
    struct sensor_value_t {
        std::string to_pp_string() { return "Mock Sensor"; }
        bool to_bool() { return true; }
    };
    
    struct rx_streamer {
        typedef std::shared_ptr<rx_streamer> sptr;
        size_t recv(std::complex<float>* buff, size_t size, rx_metadata_t& md, double timeout) {
            // Generate mock data with varying power levels
            static size_t block_count = 0;
            block_count++;
            
            for(size_t i = 0; i < size; i++) {
                // Create different signal patterns with varying amplitudes
                float amplitude = 0.1f + 0.05f * std::sin(block_count * 0.1f); // Amplitude varies from 0.05 to 0.15
                float noise = 0.02f * (std::rand() / (float)RAND_MAX - 0.5f); // Small random noise
                
                float real_part = amplitude * std::sin(i * 0.01f) + noise;
                float imag_part = amplitude * std::cos(i * 0.01f) + noise;
                
                buff[i] = std::complex<float>(real_part, imag_part);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Simulate data rate
            return size;
        }
        void issue_stream_cmd(const stream_cmd_t& cmd) {}
    };
    
    struct stream_args_t {
        stream_args_t(const std::string& cpu, const std::string& wire) {}
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
        std::string strerror() { return "Mock error"; }
    };

    namespace usrp {
        struct multi_usrp {
            typedef std::shared_ptr<multi_usrp> sptr;
            static sptr make(const std::string& args) { return nullptr; }
            void set_rx_rate(double rate) {}        //set the rx rate
            double get_rx_rate() { return 1e6; }    //get the rx rate
            void set_rx_freq(const tune_request_t& tune_req) {}    //set the rx frequency
            double get_rx_freq() { return 2.437e9; }    //get the rx frequency
            void set_rx_gain(double gain) {}    //set the rx gain
            double get_rx_gain() { return 30.0; }
            std::string get_pp_string() { return "Mock USRP"; }    //get the pp string
            std::vector<std::string> get_rx_sensor_names() { return {}; }    //get the rx sensor names
            sensor_value_t get_rx_sensor(const std::string& name) { return sensor_value_t(); }    //get the rx sensor
            rx_streamer::sptr get_rx_stream(const stream_args_t& args) { return rx_streamer::sptr(); }
        };
    }
}
#endif


// =============================================================================
// Global Constants and Variables (from class examples)
// =============================================================================
// USRP configuration parameters as specified in lab requirements
const double RX_FREQ = 2.437e9;        // 2.437 GHz RX carrier frequency
const double RX_RATE = 1e6;            // RX sampleing rate
const double RX_GAIN = 30.0;           // Default RX gain
const size_t SAMPLES_PER_BLOCK = 10000; // 10000 samples per block as required

// Thread control variables
std::atomic<bool> stop_signal(false);
std::atomic<size_t> overflow_count(0);

// Mutex for thread-safe console output
std::mutex console_mutex;

// =====================================================================================
// SampleBlock structure to hold samples with block number, hold 10000 samples per block
// =====================================================================================
struct SampleBlock {
    size_t block_number;
    std::vector<std::complex<float>> samples;
    
    SampleBlock() : block_number(0) {}
    SampleBlock(size_t num, size_t num_samples) : block_number(num), samples(num_samples) {}
};

// =============================================================================
// Thread-safe FIFO Queue (based on class example pattern)
// =============================================================================
class SampleQueue {
private:
    std::queue<SampleBlock> queue;
    std::mutex mtx;
    std::condition_variable cv;
    
public:
    // Push a block to the queue
    void push(const SampleBlock& block) {
        std::unique_lock<std::mutex> lock(mtx);
        queue.push(block);
        cv.notify_one();
    }
    
    // Pop a block from the queue (blocking)
    bool pop(SampleBlock& block) {
        std::unique_lock<std::mutex> lock(mtx);
        
        // Wait until queue has data or stop signal
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
    
    // Get current queue size
    size_t size() {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.size();
    }
    
    // Wake up all waiting threads
    void notify_all() {
        cv.notify_all();
    }
};

// Global queue instance
SampleQueue sample_queue;

// =============================================================================
// RX Streamer Function (based on class example rx_samples_to_file.cpp pattern)
// =============================================================================
void rx_streamer_thread(uhd::usrp::multi_usrp::sptr usrp, double sampling_rate) {
    
    // Set RX rate
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Setting RX rate to " << sampling_rate/1e6 << " MHz..." << std::endl;
    }
    usrp->set_rx_rate(sampling_rate);
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Actual RX rate: " << usrp->get_rx_rate()/1e6 << " MHz" << std::endl;
    }
    
    // Set RX frequency
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Setting RX frequency to " << RX_FREQ/1e9 << " GHz..." << std::endl;
    }
    uhd::tune_request_t tune_request(RX_FREQ);
    usrp->set_rx_freq(tune_request);
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Actual RX frequency: " << usrp->get_rx_freq()/1e9 << " GHz" << std::endl;
    }
    
    // Set RX gain
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Setting RX gain to " << RX_GAIN << " dB..." << std::endl;
    }
    usrp->set_rx_gain(RX_GAIN);
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Actual RX gain: " << usrp->get_rx_gain() << " dB" << std::endl;
    }
    
    // Wait for setup time
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
#ifndef SIMULATE_MODE
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
#endif
    
    // Create RX streamer
    uhd::stream_args_t stream_args("fc32", "sc16");  // CPU format, Wire format, a synchronous stream
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

// =============================================================================
// Processing Thread Function (calculates average power)
// =============================================================================
void processing_thread(int thread_id) {
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Processing thread " << thread_id << " started" << std::endl;
    }
    
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
        {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << std::fixed << std::setprecision(8);
            std::cout << "[Thread " << thread_id << "] "
                      << "Block #" << std::setw(6) << block.block_number 
                      << " | Avg Power: " << std::setw(14) << avg_power
                      << " | Queue Size: " << sample_queue.size()
                      << std::endl;
        }
        
        blocks_processed++;
    }
    
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Processing thread " << thread_id 
                  << " stopped. Processed " << blocks_processed << " blocks" << std::endl;
    }
}

// =============================================================================
// Main Function (based on class example structure)
// =============================================================================
int main(int argc, char* argv[]) {
    
    // Parse command line arguments for sampling rate
    double sampling_rate = RX_RATE;
    int num_threads = 2;  // Default 2 processing threads as required
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
    
#ifndef SIMULATE_MODE
    // Create USRP device
    std::cout << "\n=== Creating USRP device ===" << std::endl;
    std::string device_args = "";  // Empty for default
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_args);
#else
    // Create mock USRP for simulation
    std::cout << "\n=== Creating Mock USRP device (Simulation Mode) ===" << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make("");
#endif
    
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
    
    // Additional statistics for different sampling rates (for Question 2)
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

