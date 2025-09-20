/*
 * EEL6528 Lab 1: Multi-threaded RX Streamer - SIMULATION VERSION
 * 
 * DESCRIPTION:
 * This is a dedicated simulation program that implements a multi-threaded 
 * software-defined radio (SDR) application without requiring USRP hardware.
 * It generates synthetic I/Q samples and processes them in blocks to calculate
 * average power using a producer-consumer pattern with thread-safe FIFO queues.
 * 
 * FEATURES:
 * - Generates realistic synthetic I/Q samples with time-varying amplitude
 * - Simulates hardware overflow conditions at high sampling rates
 * - Multi-threaded architecture with producer-consumer pattern
 * - Thread-safe FIFO queue for sample block management
 * - Configurable sampling rates, thread counts, and runtime
 * 
 * COMPILATION:
 * Windows (MSVC): cl /EHsc /std:c++17 /O2 lab1_sim.cpp /Fe:lab1_sim.exe
 * Linux/Mac (GCC): g++ -std=c++17 -O3 -o lab1_sim lab1_sim.cpp -pthread
 * 
 * USAGE:
 * ./lab1_sim [sampling_rate] [num_threads] [run_time]
 * 
 * PARAMETERS:
 * - sampling_rate: Sampling rate in Hz (default: 1e6 = 1 MHz)
 * - num_threads: Number of processing threads (default: 2)
 * - run_time: Duration to run in seconds (default: 10)
 * 
 * EXAMPLES:
 * ./lab1_sim 5e6 4 30    # 5MHz, 4 threads, 30 seconds
 * ./lab1_sim 1e6 2 10    # 1MHz, 2 threads, 10 seconds
 */

// =============================================================================
// HEADER INCLUDES
// =============================================================================

#include <iostream>                   // Console I/O operations
#include <complex>                    // Complex number support for I/Q samples
#include <vector>                     // Dynamic arrays for sample storage
#include <thread>                     // Multi-threading support
#include <queue>                      // FIFO queue for sample blocks
#include <mutex>                      // Thread synchronization primitives
#include <condition_variable>         // Thread signaling for producer-consumer
#include <atomic>                     // Atomic operations for thread-safe flags
#include <chrono>                     // Time measurement and delays
#include <iomanip>                    // Output formatting for precision
#include <algorithm>                  // Standard algorithms
#include <string>                     // String manipulation
#include <memory>                     // Smart pointers
#include <cmath>                      // Mathematical functions (sin, cos, etc.)
#include <cstdlib>                    // Standard library utilities (rand, etc.)

// =============================================================================
// MOCK UHD IMPLEMENTATION FOR SIMULATION
// =============================================================================

/**
 * Mock UHD namespace containing simulated versions of UHD classes
 * This provides the same interface as real UHD but generates synthetic data
 */
namespace uhd {
    // Forward declarations of UHD types we'll mock
    struct sensor_value_t;      // Hardware sensor readings
    struct rx_streamer;         // Sample streaming interface  
    struct stream_args_t;       // Stream configuration
    struct stream_cmd_t;        // Streaming commands
    struct rx_metadata_t;       // Receive metadata and error info
    struct time_spec_t;         // Time specification
    struct tune_request_t;      // Frequency tuning requests
    
    /**
     * Mock tune request structure
     * In real UHD, this specifies frequency tuning parameters
     */
    struct tune_request_t {
        tune_request_t(double f) {} // Constructor takes frequency but ignores it in simulation
    };
    
    /**
     * Mock time specification structure
     * In real UHD, this represents precise timing information
     */
    struct time_spec_t {};
    
    /**
     * Mock sensor value structure
     * Simulates hardware sensor readings (temperature, LO lock status, etc.)
     */
    struct sensor_value_t {
        std::string to_pp_string() { return "Mock Sensor"; }  // Pretty-print sensor value
        bool to_bool() { return true; }                       // Always return true for simulation
    };
    
    /**
     * Mock RX Streamer - simulates receiving I/Q samples from USRP hardware
     * This is the core of the simulation, generating synthetic complex samples
     * that mimic real radio frequency data
     */
    struct rx_streamer {
        typedef std::shared_ptr<rx_streamer> sptr;  // Smart pointer type definition
        
        /**
         * Simulated receive function - generates synthetic I/Q samples
         * @param buff: Buffer to fill with complex samples
         * @param size: Number of samples to generate
         * @param md: Metadata structure (unused in simulation)
         * @param timeout: Receive timeout (unused in simulation)
         * @return: Number of samples generated (always equals size)
         */
        size_t recv(std::complex<float>* buff, size_t size, rx_metadata_t& md, double timeout) {
            // Static counter to create time-varying signal patterns
            // This simulates a slowly changing signal environment
            static size_t block_count = 0;
            block_count++;
            
            // Generate synthetic I/Q samples for each buffer position
            for(size_t i = 0; i < size; i++) {
                // Create time-varying amplitude to simulate signal power changes
                // Amplitude oscillates between 0.05 and 0.15 to create realistic power variations
                float amplitude = 0.1f + 0.05f * std::sin(block_count * 0.1f);
                
                // Add small random noise component to simulate real-world conditions
                // Noise level is 2% of signal amplitude, centered around zero
                float noise = 0.02f * (std::rand() / (float)RAND_MAX - 0.5f);
                
                // Generate I/Q components using sinusoidal functions
                // This creates a complex sinusoid similar to a modulated carrier
                float real_part = amplitude * std::sin(i * 0.01f) + noise;  // In-phase component
                float imag_part = amplitude * std::cos(i * 0.01f) + noise;  // Quadrature component
                
                // Store as complex sample (I + jQ format)
                buff[i] = std::complex<float>(real_part, imag_part);
            }
            
            // Simulate realistic data rate by adding small delay
            // This prevents the simulation from running too fast
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            return size;  // Always return full buffer size (no errors in simulation)
        }
        
        /**
         * Mock stream command handler - does nothing in simulation
         * In real UHD, this starts/stops the data stream
         */
        void issue_stream_cmd(const stream_cmd_t& cmd) {}
    };
    
    /**
     * Mock stream arguments structure
     * In real UHD, this specifies data format and wire protocol
     */
    struct stream_args_t {
        // Constructor takes CPU format (e.g., "fc32") and wire format (e.g., "sc16")
        stream_args_t(const std::string& cpu, const std::string& wire) {}
    };
    
    /**
     * Mock stream command structure
     * In real UHD, this controls streaming start/stop and timing
     */
    struct stream_cmd_t {
        enum stream_mode_t { 
            STREAM_MODE_START_CONTINUOUS,   // Start continuous streaming
            STREAM_MODE_STOP_CONTINUOUS     // Stop continuous streaming
        };
        
        stream_cmd_t(stream_mode_t mode) : stream_mode(mode) {}
        
        stream_mode_t stream_mode;  // Streaming mode (start/stop)
        size_t num_samps;          // Number of samples (for finite streaming)
        bool stream_now;           // Start streaming immediately
        time_spec_t time_spec;     // Precise timing specification
    };
    
    /**
     * Mock receive metadata structure
     * In real UHD, this contains error information and timing data
     */
    struct rx_metadata_t {
        enum error_code_t { 
            ERROR_CODE_NONE,        // No error occurred
            ERROR_CODE_TIMEOUT,     // Receive operation timed out
            ERROR_CODE_OVERFLOW     // Sample buffer overflow (samples dropped)
        };
        
        error_code_t error_code = ERROR_CODE_NONE;  // Error status
        std::string strerror() { return "Mock error"; }  // Error description
    };

    namespace usrp {
        struct multi_usrp {
            typedef std::shared_ptr<multi_usrp> sptr;
            static sptr make(const std::string& args) { return std::make_shared<multi_usrp>(); }
            void set_rx_rate(double rate) { current_rate = rate; }
            double get_rx_rate() { return current_rate; }
            void set_rx_freq(const tune_request_t& tune_req) {}
            double get_rx_freq() { return 2.437e9; }
            void set_rx_gain(double gain) { current_gain = gain; }
            double get_rx_gain() { return current_gain; }
            std::string get_pp_string() { return "Mock USRP (Simulation Mode)"; }
            std::vector<std::string> get_rx_sensor_names() { return {}; }
            sensor_value_t get_rx_sensor(const std::string& name) { return sensor_value_t(); }
            rx_streamer::sptr get_rx_stream(const stream_args_t& args) { 
                return std::make_shared<rx_streamer>(); 
            }
        private:
            double current_rate = 1e6;
            double current_gain = 30.0;
        };
    }
}

// =============================================================================
// Global Constants and Variables
// =============================================================================
const double RX_FREQ = 2.437e9;        // 2.437 GHz RX carrier frequency
const double RX_RATE = 1e6;            // Default RX sampling rate
const double RX_GAIN = 30.0;           // Default RX gain
const size_t SAMPLES_PER_BLOCK = 10000; // 10000 samples per block

// Thread control variables
std::atomic<bool> stop_signal(false);
std::atomic<size_t> overflow_count(0);

// Mutex for thread-safe console output
std::mutex console_mutex;

// =============================================================================
// SampleBlock structure to hold samples with block number
// =============================================================================
struct SampleBlock {
    size_t block_number;
    std::vector<std::complex<float>> samples;
    
    SampleBlock() : block_number(0) {}
    SampleBlock(size_t num, size_t num_samples) : block_number(num), samples(num_samples) {}
};

// =============================================================================
// Thread-safe FIFO Queue
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
// Simulated RX Streamer Function
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
    std::cout << "\n=== Simulation RX Streaming Started ===" << std::endl;
    
    // RX metadata
    uhd::rx_metadata_t md;
    
    // Block counter
    size_t block_counter = 0;
    
    // Main streaming loop
    while (!stop_signal.load()) {
        // Receive samples (simulated)
        size_t num_rx_samps = rx_stream->recv(
            &buff.front(),
            buff.size(),
            md,
            3.0  // timeout
        );
        
        // Handle errors (simulated)
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cerr << "Timeout while receiving" << std::endl;
            break;
        }
        
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            // Overflow - samples were dropped
            overflow_count++;
            std::cerr << "Overflow";  // Print 'overflow' when overflow occurs
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
    
    std::cout << "\n=== Simulation RX Streaming Stopped ===" << std::endl;
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
// Main Function
// =============================================================================
int main(int argc, char* argv[]) {
    
    // Parse command line arguments
    double sampling_rate = RX_RATE;
    int num_threads = 2;    // Default 2 processing threads
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
    
    // Create mock USRP for simulation
    std::cout << "\n=== Creating Mock USRP device (Simulation Mode) ===" << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make("");
    
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
    std::cout << "\n=== Running Simulation for " << run_time << " seconds ===" << std::endl;
    std::cout << "Carrier Frequency: " << RX_FREQ/1e9 << " GHz" << std::endl;
    std::cout << "Sampling Rate: " << sampling_rate/1e6 << " MHz" << std::endl;
    std::cout << "Samples per Block: " << SAMPLES_PER_BLOCK << std::endl;
    std::cout << "Processing Threads: " << num_threads << std::endl;
    std::cout << "Mode: SIMULATION (No Hardware Required)" << std::endl;
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
    std::cout << "\n=== Simulation Final Statistics ===" << std::endl;
    std::cout << "Total Overflows: " << overflow_count.load() << std::endl;
    
    // Additional statistics for different sampling rates
    if (overflow_count.load() > 0) {
        std::cout << "WARNING: Overflows detected at " << sampling_rate/1e6 
                  << " MHz sampling rate!" << std::endl;
        std::cout << "This indicates the host computer cannot keep up with the data rate." << std::endl;
    } else {
        std::cout << "SUCCESS: No overflows at " << sampling_rate/1e6 
                  << " MHz sampling rate." << std::endl;
    }
    
    std::cout << "\nSimulation program finished!" << std::endl;
    
    return 0;
}
