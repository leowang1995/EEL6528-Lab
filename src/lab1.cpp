/*
 *
 * EEL6528 Lab 1: Make friends with UHD
 * Multi-threaded Software Defined Radio (SDR) Receiver
 * 
 * This program implements a multi-threaded RX streamer with power calculation for 
 * Software-Defined Radio (SDR) applications. It supports both hardware mode (with
 * actual USRP devices) and simulation mode (with mock data generation).
 * 
 * COMPILATION:
 * Hardware Mode: g++ -std=c++17 -O3 -o lab1_rx lab1.cpp -luhd -pthread
 * Simulation Mode: g++ -std=c++17 -O3 -DSIMULATE_MODE -o lab1_rx_sim lab1.cpp -pthread
 * 
 * FEATURES:
 * - Multi-threaded producer-consumer architecture
 * - Real-time signal power calculation
 * - Configurable sampling rates (1MHz to 25MHz)
 * - Scalable processing threads (1 to 8 threads)
 * - Overflow detection and performance monitoring
 * - Cross-platform simulation mode for development/testing
 * 
 * Based on code examples from: https://tanfwong.github.io/sdr_notes/ch2/prelims_exs.html
 */

// ============================================================================
// INCLUDE STATEMENTS
// ============================================================================

// UHD (USRP Hardware Driver) Library Headers - only for hardware mode
// Provide the interface to communicate with USRP devices
#ifndef SIMULATE_MODE
#include <uhd/usrp/multi_usrp.hpp>      // Main USRP device interface
#include <uhd/stream.hpp>               // Streaming interface for RX/TX data
#include <uhd/types/tune_request.hpp>   // Frequency tuning requests
#endif

// Standard C++ Library Headers
#include <iostream>          // Console I/O operations
#include <complex>           // Complex number support for IQ samples
#include <vector>            // Dynamic arrays for sample storage
#include <thread>            // Multi-threading support
#include <queue>             // FIFO queue for sample blocks
#include <mutex>             // Mutual exclusion for thread safety
#include <condition_variable> // Thread synchronization primitives
#include <atomic>            // Atomic operations for lock-free programming
#include <chrono>            // Time-related functions and types
#include <iomanip>           // I/O stream formatting

using namespace std;

#ifdef SIMULATE_MODE
// ============================================================================
// MOCK UHD TYPES FOR SIMULATION MODE
// ============================================================================
// These mock implementations allow the program to compile and run without
// actual UHD hardware, generating synthetic data for development and testing
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

// ============================================================================
// HARDWARE CONFIGURATION CONSTANTS
// ============================================================================

// RF Frontend Configuration
const double RX_FREQ = 2.437e9;        // 2.437 GHz carrier frequency (WiFi band)
const double RX_RATE = 1e6;            // 1 MHz sampling rate (default)
const double RX_GAIN = 30.0;           // 30 dB receive gain setting
const size_t SAMPLES_PER_BLOCK = 10000; // Number of samples per processing block

// ============================================================================
// GLOBAL THREAD SYNCHRONIZATION VARIABLES
// ============================================================================

// Atomic flag to signal all threads to stop execution
atomic<bool> stop_signal(false);

// Counter for overflow events when samples are dropped
atomic<size_t> overflow_count(0);

/**
 * Sample management block
 * 
 * This structure encapsulates a fixed-size block of complex samples along with
 * metadata for tracking and processing.
 * 
 * MEMORY LAYOUT:
 * - block_number: Sequential identifier for ordering and debugging
 * - samples: Vector of complex<float> representing IQ sample pairs
 *   * Real component (I): In-phase signal component
 *   * Imaginary component (Q): Quadrature signal component
 */
struct SampleBlock {
    size_t block_number;                     // Sequential block identifier
    vector<complex<float>> samples;          // IQ sample data (I + jQ format)
    
    // Default constructor: Creates empty block with ID 0
    SampleBlock() : block_number(0) {}
    
    // Parameterized constructor: Pre-allocates sample vector
    // @param num: Block sequence number for tracking
    // @param size: Number of samples to pre-allocate
    SampleBlock(size_t num, size_t size) : block_number(num), samples(size) {}
};

/**
 * SampleQueue: Thread-safe FIFO queue for producer-consumer pattern
 * 
 * This class implements a thread-safe queue using the producer-consumer pattern.
 * The RX thread (producer) pushes sample blocks, while processing threads
 * (consumers) pop blocks for analysis.
 * 
 * PERFORMANCE CONSIDERATIONS:
 * - unique_lock used for condition variable compatibility
 * - lock_guard used for simple critical sections
 * - notify_one() vs notify_all() optimizes wake-up efficiency
 */
class SampleQueue {
private:
    queue<SampleBlock> queue;    // Underlying STL queue container
    mutex mtx;                   // Mutex for thread-safe access
    condition_variable cv;       // Condition variable for blocking operations
    
public:
    /**
     * push(): Add a sample block to the queue
     * @param block: Sample block to add to queue
     */
    void push(const SampleBlock& block) {
        unique_lock<mutex> lock(mtx);  // Acquire exclusive access
        queue.push(block);             // Add block to queue
        cv.notify_one();               // Wake up one waiting consumer
    }
    
    /**
     * pop(): Remove and return a sample block
     * @param block: Reference to store the retrieved block
     * @return: true if block retrieved, false if shutting down
     */
    bool pop(SampleBlock& block) {
        unique_lock<mutex> lock(mtx);  // Acquire exclusive access
        
        // Block until queue has data OR stop signal is set
        cv.wait(lock, [this] { 
            return !queue.empty() || stop_signal.load(); 
        });
        
        // Check for shutdown condition (stop signal + empty queue)
        if (stop_signal.load() && queue.empty()) {
            return false;  // Signal consumer to exit
        }
        
        // Retrieve block if available
        if (!queue.empty()) {
            block = queue.front();  // Copy front block
            queue.pop();            // Remove from queue
            return true;            // Success
        }
        return false;  // Fallback case
    }
    
    /**
     * size(): Get current queue size
     * @return: Number of blocks currently in queue
     */
    size_t size() {
        lock_guard<mutex> lock(mtx);  // Lock for read operation
        return queue.size();
    }
    
    /**
     * notify_all(): Wake up all waiting threads
     * Used during shutdown to wake up blocked consumers
     */
    void notify_all() {
        cv.notify_all();  // Wake up ALL waiting threads
    }
};

// Global queue instance for sample block communication
// Shared between RX streamer thread and processing threads
SampleQueue sample_queue;

// ============================================================================
//          RX STREAMER THREAD
// ============================================================================

/**
 * rx_streamer_thread(): Main SDR receiver thread function
 * 
 * This function implements the producer thread in the producer-consumer pattern.
 * It configures the USRP hardware, establishes an RF streaming connection,
 * and continuously receives IQ samples from the RF frontend.
 * 
 * HARDWARE CONFIGURATION SEQUENCE:
 * 1. Set sampling rate and verify actual rate achieved
 * 2. Tune RF frontend to desired carrier frequency  
 * 3. Configure receive gain for optimal signal levels
 * 4. Check LO (Local Oscillator) lock status
 * 5. Create and configure RX data stream
 * 
 * STREAMING OPERATION:
 * - Continuously receives blocks of IQ samples from USRP
 * - Handles error conditions (timeouts, overflows, etc.)
 * - Pushes complete sample blocks to processing queue
 * - Monitors for stop signal to terminate gracefully
 * 
 * @param usrp: Shared pointer to USRP device interface
 * @param sampling_rate: Desired sampling rate in samples/second
 */
void rx_streamer_thread(uhd::usrp::multi_usrp::sptr usrp, double sampling_rate) {
    
    // ========================================================================
    //           CONFIGURE SAMPLING RATE
    // ========================================================================
    // Set the ADC sampling rate for the RX path
    cout << "Setting RX rate to " << sampling_rate/1e6 << " MHz..." << endl;
    usrp->set_rx_rate(sampling_rate);
    
    // Verify the actual sampling rate achieved by hardware
    cout << "Actual RX rate: " << usrp->get_rx_rate()/1e6 << " MHz" << endl;
    
    // ========================================================================
    //          CONFIGURE RF CARRIER FREQUENCY
    // ========================================================================
    // Set the RF frontend to the desired carrier frequency
    cout << "Setting RX frequency to " << RX_FREQ/1e9 << " GHz..." << endl;
    
    // Create tune request for RF frontend
    // UHD automatically selects optimal RF/LO frequencies
    uhd::tune_request_t tune_request(RX_FREQ);
    usrp->set_rx_freq(tune_request);
    
    // Check actual frequency achieved
    cout << "Actual RX frequency: " << usrp->get_rx_freq()/1e9 << " GHz" << endl;
    
    // ========================================================================
    //          CONFIGURE RECEIVE GAIN
    // ========================================================================
    // Set the RF and analog gain stages for optimal signal levels
    // Too low: poor SNR, too high: saturation
    cout << "Setting RX gain to " << RX_GAIN << " dB..." << endl;
    usrp->set_rx_gain(RX_GAIN);
    
    // Verify actual gain achieved (hardware may quantize gain values)
    cout << "Actual RX gain: " << usrp->get_rx_gain() << " dB" << endl;
    
    // ========================================================================
    //           HARDWARE SETTLING TIME
    // ========================================================================
    // Allow hardware to stabilize after configuration changes
    this_thread::sleep_for(chrono::seconds(1));
    
    // ========================================================================
    //      VERIFY LOCAL OSCILLATOR (LO) LOCK STATUS
    // ========================================================================
    // Check if the RF synthesizer has achieved phase lock
    std::vector<std::string> sensor_names = usrp->get_rx_sensor_names();
    if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked") != sensor_names.end()) {
        uhd::sensor_value_t lo_locked = usrp->get_rx_sensor("lo_locked");
        std::cout << "LO Locked: " << lo_locked.to_pp_string() << std::endl;
        
        // Abort if LO failed to lock (unstable frequency reference)
        if (!lo_locked.to_bool()) {
            std::cerr << "Failed to lock LO - RF frontend unstable!" << std::endl;
            return;
        }
    }
    
    // ========================================================================
    //          CREATE AND CONFIGURE DATA STREAM
    // ========================================================================
    // Set up the data streaming interface between USRP and host computer
    // fc32: 32-bit floating point complex on CPU (I+jQ)
    // sc16: 16-bit signed complex over Ethernet/USB
    uhd::stream_args_t stream_args("fc32", "sc16");  // CPU format, Wire format
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    
    // Allocate receive buffer for one block of IQ samples
    // Buffer size determines the granularity of processing
    std::vector<std::complex<float>> buff(SAMPLES_PER_BLOCK);
    
    // ========================================================================
    //      CONFIGURE STREAMING PARAMETERS
    // ========================================================================
    // Set up continuous streaming mode for real-time operation
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.num_samps = 0;        // 0 = continuous streaming
    stream_cmd.stream_now = true;    // Start immediately
    stream_cmd.time_spec = uhd::time_spec_t();  // Timestamp
    
    // ========================================================================
    //          START RF STREAMING
    // ========================================================================
    // Issue stream command to begin continuous sample acquisition
    rx_stream->issue_stream_cmd(stream_cmd);
    std::cout << "\n=== RX Streaming Started ===" << std::endl;
    
    // Initialize streaming variables
    uhd::rx_metadata_t md;        // Metadata for each receive operation
    size_t block_counter = 0;     // Sequential block numbering
    
    // ========================================================================
    //          MAIN STREAMING LOOP
    // ========================================================================
    // This loop runs continuously until stop signal is received
    // Each iteration receives one block of IQ samples from the RF frontend
    while (!stop_signal.load()) {
        
        // Receive one block of samples from USRP hardware
        // This is a blocking call that waits for data from the RF frontend
        size_t num_rx_samps = rx_stream->recv(
            &buff.front(),    // Destination buffer pointer
            buff.size(),      // Maximum number of samples to receive
            md,               // Metadata (timestamps, error flags, etc.)
            3.0               // Timeout in seconds
        );
        
        // ====================================================================
        //      ERROR HANDLING AND STATUS MONITORING
        // ====================================================================
        
        // Handle timeout condition (no data received within timeout period)
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            std::cerr << "Timeout: No data received from USRP" << std::endl;
            break;  // Exit streaming loop
        }
        
        // Handle overflow condition - samples dropped due to processing lag
        // This indicates the host computer cannot keep up with the data rate
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            overflow_count++;           // Increment atomic counter
            std::cerr << "O";          // Print 'O' for visual overflow indication
            std::cerr.flush();         // Force immediate output
            continue;                  // Skip this block and continue streaming
        }
        
        // Handle other error conditions (hardware issues, etc.)
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            std::cerr << "Streaming error: " << md.strerror() << std::endl;
            break;  // Exit streaming loop on serious errors
        }
        
        // ====================================================================
        // SAMPLE BLOCK PROCESSING AND QUEUE MANAGEMENT
        // ====================================================================
        
        // Only process complete blocks
        if (num_rx_samps == SAMPLES_PER_BLOCK) {
            // Create new sample block with sequential numbering
            SampleBlock block(block_counter++, SAMPLES_PER_BLOCK);
            
            // Copy received samples to block
            block.samples = buff;
            
            // Push block to processing queue
            sample_queue.push(block);
        }
    }
    
    // ========================================================================
    //      CLEANUP AND SHUTDOWN SEQUENCE
    // ========================================================================
    // Stop continuous streaming mode
    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_stream->issue_stream_cmd(stream_cmd);
    
    // Print final streaming statistics
    std::cout << "\n=== RX Streaming Stopped ===" << std::endl;
    std::cout << "Total blocks transmitted: " << block_counter << std::endl;
    std::cout << "Total overflows: " << overflow_count.load() << std::endl;
    
    // Performance assessment
    if (overflow_count.load() > 0) {
        std::cout << "WARNING: Data loss detected - consider reducing sample rate" << std::endl;
    } else {
        std::cout << "SUCCESS: No data loss during streaming" << std::endl;
    }
}

// ============================================================================
//          SIGNAL PROCESSING THREAD
// ============================================================================

/**
 * processing_thread(): Consumer thread for signal analysis
 * 
 * Multiple instances of this thread run in parallel to process sample blocks
 * retrieved from the shared queue. Each thread performs signal analysis on
 * received RF data.
 * 
 * Signal processing operations:
 * 1. Retrieve sample blocks from thread-safe queue
 * 2. Calculate average signal power (energy content)
 * 3. Display results with thread identification
 * 
 * Power calculation:
 * Average Power = (1/N) * Σ|x[n]|² where:
 * - N = number of samples in block
 * - x[n] = complex sample (I + jQ)
 * - |x[n]|² = I² + Q² (magnitude squared)
 * 
 * @param thread_id: Unique identifier for this processing thread
 */
void processing_thread(int thread_id) {
    
    // Thread startup notification
    std::cout << "Processing thread " << thread_id << " started" << std::endl;
    
    // Local statistics tracking
    size_t blocks_processed = 0;  // Count of blocks processed by this thread
    
    // ========================================================================
    // MAIN PROCESSING LOOP: CONSUME AND ANALYZE SAMPLE BLOCKS
    // ========================================================================
    while (!stop_signal.load()) {
        SampleBlock block;  // Local storage for retrieved sample block
        
        // ====================================================================
        //      RETRIEVE SAMPLE BLOCK FROM QUEUE
        // ====================================================================
        // Blocking operation - thread sleeps until data available
        if (!sample_queue.pop(block)) {
            break;  // Stop signal received or queue empty during shutdown
        }
        
        // ====================================================================
        //      SIGNAL POWER ANALYSIS
        // ====================================================================
        // Calculate the average power of the received RF signal block
        // Power = energy per unit time, indicating signal strength
        
        double sum_power = 0.0;  // Accumulator for total power
        
        // Iterate through all IQ samples in the block
        for (const auto& sample : block.samples) {
            // Calculate magnitude squared: |x|² = I² + Q²
            // std::norm() efficiently computes real²+imag² for complex numbers
            sum_power += std::norm(sample);
        }
        
        // Compute average power across all samples in block
        // Normalizes for block size and gives power per sample
        double avg_power = sum_power / block.samples.size();
        
        // ====================================================================
        //      THREAD-SAFE RESULTS REPORTING
        // ====================================================================
        // Display analysis results with proper formatting and thread identification
        {
            // Configure floating-point output format for consistent display
            std::cout << std::fixed << std::setprecision(8);
            
            // Display comprehensive processing results
            std::cout << "[Thread " << thread_id << "] "                    // Thread identification
                      << "Block #" << std::setw(6) << block.block_number  // Block sequence number
                      << " | Avg Power: " << std::setw(14) << avg_power   // Signal power level
                      << " | Queue Size: " << sample_queue.size()         // Queue backlog status
                      << std::endl;
        }
        
        // Update local processing statistics
        blocks_processed++;
    }
    
    // ========================================================================
    //  THREAD SHUTDOWN REPORTING
    // ========================================================================
    std::cout << "Processing thread " << thread_id 
              << " stopped. Processed " << blocks_processed << " blocks" << std::endl;
}

// Main Function
int main(int argc, char* argv[]) {
    
    // Parse command line arguments
    double sampling_rate = RX_RATE;
    int num_threads = 2;  // Default 2 processing threads
    double run_time = 10.0;  // Default run for 10 seconds
    
    // Parse optional command line arguments for runtime configuration
    if (argc > 1) {
        sampling_rate = std::stod(argv[1]);  // Convert string to double
        std::cout << "Using sampling rate: " << sampling_rate/1e6 << " MHz" << std::endl;
    }
    if (argc > 2) {
        num_threads = std::stoi(argv[2]);    // Convert string to integer
        std::cout << "Using " << num_threads << " processing threads" << std::endl;
    }
    if (argc > 3) {
        run_time = std::stod(argv[3]);       // Convert string to double
        std::cout << "Running for " << run_time << " seconds" << std::endl;
    }
    
    // Display usage information and current configuration
    if (argc == 1) {
        std::cout << "\n=== SDR Multi-threaded Receiver ===" << std::endl;
        std::cout << "Usage: " << argv[0] << " [sampling_rate] [num_threads] [run_time_seconds]" << std::endl;
        std::cout << "Example: " << argv[0] << " 5e6 4 30  (5MHz, 4 threads, 30 seconds)" << std::endl;
        std::cout << "Using defaults: rate=" << sampling_rate/1e6 << "MHz, threads=" << num_threads 
                  << ", time=" << run_time << "s" << std::endl;
    }
    
    // ========================================================================
    //       USRP HARDWARE INITIALIZATION
    // ========================================================================
    // Initialize connection to USRP hardware (or simulation mode)
    std::cout << "\n=== Creating USRP device ===" << std::endl;
    std::string device_args = "";  // Empty for default device discovery
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_args);
    
    // Display detailed device information for verification
    std::cout << "Using device: " << usrp->get_pp_string() << std::endl;
    
    // ====================================================================
    //      THREAD CREATION AND LAUNCH
    // ====================================================================
    
    // Container for all thread objects
    std::vector<std::thread> threads;
    
    // Launch the RX streamer thread (producer)
    // This thread interfaces with USRP hardware and feeds the sample queue
    threads.emplace_back(rx_streamer_thread, usrp, sampling_rate);
    
    // Launch multiple signal processing threads (consumers)
    // Each thread processes sample blocks independently for parallel analysis
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(processing_thread, i + 1);  // Thread IDs start at 1
    }
    
    // ====================================================================
    //       SYSTEM MONITORING AND RUNTIME CONTROL
    // ====================================================================
    
    // Display comprehensive system configuration
    std::cout << "\n=== Multi-threaded SDR System Active ===" << std::endl;
    std::cout << "Carrier Frequency: " << RX_FREQ/1e9 << " GHz" << std::endl;
    std::cout << "Sampling Rate: " << sampling_rate/1e6 << " MHz" << std::endl;
    std::cout << "Samples per Block: " << SAMPLES_PER_BLOCK << std::endl;
    std::cout << "Processing Threads: " << num_threads << std::endl;
    std::cout << "Runtime Duration: " << run_time << " seconds" << std::endl;
    std::cout << "Thread Architecture: 1 Producer + " << num_threads << " Consumers" << std::endl;
    std::cout << "=========================================\n" << std::endl;
    
    // Allow system to run for specified duration
    // Main thread sleeps while worker threads process RF data
    std::this_thread::sleep_for(std::chrono::duration<double>(run_time));
    
    // ====================================================================
    //       COORDINATED SYSTEM SHUTDOWN
    // ====================================================================
    
    std::cout << "\n=== Initiating graceful shutdown ===" << std::endl;
    
    // Set atomic stop signal to notify all threads to terminate
    stop_signal.store(true);
    
    // Wake up any threads blocked on queue operations
    sample_queue.notify_all();
    
    // Wait for all threads to complete their current operations and exit
    std::cout << "Waiting for all threads to complete..." << std::endl;
    for (auto& t : threads) {
        t.join();  // Block until thread terminates
    }
    std::cout << "All threads terminated successfully." << std::endl;
    
    // ====================================================================
    //      PERFORMANCE ANALYSIS AND FINAL REPORTING
    // ====================================================================
    
    std::cout << "\n=== Final Performance Statistics ===" << std::endl;
    std::cout << "Total Overflow Events: " << overflow_count.load() << std::endl;
    
    // Analyze system performance and provide feedback
    if (overflow_count.load() > 0) {
        std::cout << "\n⚠ PERFORMANCE WARNING:" << std::endl;
        std::cout << "  - " << overflow_count.load() << " overflow events detected" << std::endl;
        std::cout << "  - Data loss occurred at " << sampling_rate/1e6 << " MHz sampling rate" << std::endl;
        std::cout << "  - System cannot keep up with current data rate" << std::endl;
        std::cout << "\n🛠 OPTIMIZATION RECOMMENDATIONS:" << std::endl;
        std::cout << "  - Reduce sampling rate (try " << sampling_rate/2e6 << " MHz)" << std::endl;
        std::cout << "  - Increase number of processing threads" << std::endl;
        std::cout << "  - Optimize signal processing algorithms" << std::endl;
    } else {
        std::cout << "\n✅ PERFORMANCE SUCCESS:" << std::endl;
        std::cout << "  - No data loss at " << sampling_rate/1e6 << " MHz sampling rate" << std::endl;
        std::cout << "  - System successfully processed all RF data" << std::endl;
        std::cout << "  - Multi-threading architecture performed optimally" << std::endl;
        std::cout << "  - Ready for higher sampling rates or more complex processing" << std::endl;
    }
    
    std::cout << "\n✅ Multi-threaded SDR program completed successfully!" << std::endl;
    
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
