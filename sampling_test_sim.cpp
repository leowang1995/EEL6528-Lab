// EEL6528 Lab 1: Sampling Rate Test - Windows Simulation Version
// This version simulates the performance testing without hardware
// Compile: cl /EHsc /std:c++17 /DSIMULATE_MODE sampling_test_sim.cpp /Fe:sampling_test_sim.exe

// Temporary fix for IDE IntelliSense
#ifndef SIMULATE_MODE
#define SIMULATE_MODE
#endif

#ifdef SIMULATE_MODE
// Mock UHD types for simulation (same as your main program)
namespace uhd {
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
            // Simulate variable load based on sampling rate
            static double sim_sampling_rate = 1e6;
            
            // Generate mock data
            for(size_t i = 0; i < size; i++) {
                buff[i] = std::complex<float>(0.1f * std::sin(i * 0.01f), 0.1f * std::cos(i * 0.01f));
            }
            
            // Simulate processing delay based on sampling rate
            auto delay_ms = static_cast<int>(10000.0 / sim_sampling_rate * 1e6); // Scale with rate
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, delay_ms)));
            
            return size;
        }
        void issue_stream_cmd(const stream_cmd_t& cmd) {}
        void set_sim_rate(double rate) { /* Would set sim_sampling_rate */ }
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
            static sptr make(const std::string& args) { return std::make_shared<multi_usrp>(); }
            void set_rx_rate(double rate) { current_rate = rate; }
            double get_rx_rate() { return current_rate; }
            void set_rx_freq(const tune_request_t& tune_req) {}
            double get_rx_freq() { return 2.437e9; }
            void set_rx_gain(double gain) {}
            double get_rx_gain() { return 20.0; }
            std::string get_pp_string() { return "Mock USRP (Simulation)"; }
            std::vector<std::string> get_rx_sensor_names() { return {}; }
            sensor_value_t get_rx_sensor(const std::string& name) { return sensor_value_t(); }
            rx_streamer::sptr get_rx_stream(const stream_args_t& args) { 
                return std::make_shared<rx_streamer>(); 
            }
        private:
            double current_rate = 1e6;
        };
    }
}
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

// Configuration
const double RX_FREQ = 2.437e9;
const size_t SAMPLES_PER_BLOCK = 10000;

// Performance monitoring variables
std::atomic<bool> stop_signal(false);
std::atomic<size_t> overflow_count(0);
std::atomic<size_t> total_blocks(0);
std::atomic<size_t> dropped_blocks(0);
std::mutex console_mutex;

// Simple performance stats for Windows
struct PerformanceStats {
    std::chrono::steady_clock::time_point start_time;
    double processing_rate = 0.0;
    size_t queue_max_size = 0;
};

PerformanceStats perf_stats;

// SampleBlock structure
struct SampleBlock {
    size_t block_number;
    std::vector<std::complex<float>> samples;
    std::chrono::steady_clock::time_point timestamp;
    
    SampleBlock() : block_number(0) {}
    SampleBlock(size_t num, size_t num_samples) : block_number(num), samples(num_samples) {
        timestamp = std::chrono::steady_clock::now();
    }
};

// Monitored queue
class MonitoredSampleQueue {
private:
    std::queue<SampleBlock> queue;
    std::mutex mtx;
    std::condition_variable cv;
    std::atomic<size_t> max_size{0};
    
public:
    void push(const SampleBlock& block) {
        std::unique_lock<std::mutex> lock(mtx);
        queue.push(block);
        
        size_t current_size = queue.size();
        size_t expected = max_size.load();
        while (current_size > expected && 
               !max_size.compare_exchange_weak(expected, current_size)) {
            expected = max_size.load();
        }
        
        cv.notify_one();
    }
    
    bool pop(SampleBlock& block) {
        std::unique_lock<std::mutex> lock(mtx);
        
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
        std::lock_guard<std::mutex> lock(mtx);
        return queue.size();
    }
    
    size_t get_max_size() {
        return max_size.load();
    }
    
    void notify_all() {
        cv.notify_all();
    }
};

MonitoredSampleQueue sample_queue;

// Simulated RX streamer thread
void rx_streamer_thread(uhd::usrp::multi_usrp::sptr usrp, double sampling_rate) {
    
    usrp->set_rx_rate(sampling_rate);
    uhd::tune_request_t tune_request(RX_FREQ);
    usrp->set_rx_freq(tune_request);
    usrp->set_rx_gain(20.0);
    
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Simulated RX rate: " << usrp->get_rx_rate()/1e6 << " MHz" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    uhd::stream_args_t stream_args("fc32", "sc16");
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    
    std::vector<std::complex<float>> buff(SAMPLES_PER_BLOCK);
    
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    rx_stream->issue_stream_cmd(stream_cmd);
    perf_stats.start_time = std::chrono::steady_clock::now();
    
    uhd::rx_metadata_t md;
    size_t block_counter = 0;
    
    // Simulate overflow based on sampling rate
    double overflow_threshold = 15e6; // Simulate overflows above 15 MHz
    
    while (!stop_signal.load()) {
        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, 1.0);
        
        // Simulate overflow at high sampling rates
        if (sampling_rate > overflow_threshold && (rand() % 100) < 5) {
            md.error_code = uhd::rx_metadata_t::ERROR_CODE_OVERFLOW;
        }
        
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            overflow_count++;
            std::cerr << "O" << std::flush;
            continue;
        }
        
        if (num_rx_samps == SAMPLES_PER_BLOCK) {
            SampleBlock block(block_counter++, SAMPLES_PER_BLOCK);
            block.samples = buff;
            
            // Simulate queue overflow at very high rates
            if (sample_queue.size() > 50 && sampling_rate > 20e6) {
                dropped_blocks++;
                continue;
            }
            
            sample_queue.push(block);
            total_blocks++;
        }
        
        md.error_code = uhd::rx_metadata_t::ERROR_CODE_NONE; // Reset for next iteration
    }
}

// Processing thread
void processing_thread(int thread_id) {
    size_t blocks_processed = 0;
    
    while (!stop_signal.load()) {
        SampleBlock block;
        
        if (!sample_queue.pop(block)) {
            break;
        }
        
        // Calculate average power
        double sum_power = 0.0;
        for (const auto& sample : block.samples) {
            sum_power += std::norm(sample);
        }
        double avg_power = sum_power / block.samples.size();
        
        blocks_processed++;
        
        // Print every 50th block
        if (blocks_processed % 50 == 0) {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << "[Thread " << thread_id << "] "
                      << "Block #" << std::setw(6) << block.block_number 
                      << " | Avg Power: " << std::scientific << std::setprecision(3) << avg_power
                      << " | Queue: " << std::setw(3) << sample_queue.size()
                      << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    
    if (argc != 4) {
        std::cout << "Usage: " << argv[0] << " <sampling_rate_MHz> <num_threads> <test_duration_sec>" << std::endl;
        std::cout << "Example: " << argv[0] << " 5 2 10" << std::endl;
        std::cout << "Simulation mode - tests performance characteristics without hardware" << std::endl;
        return -1;
    }
    
    double sampling_rate = std::stod(argv[1]) * 1e6;  // Convert MHz to Hz
    int num_threads = std::stoi(argv[2]);
    double run_time = std::stod(argv[3]);
    
    std::cout << "\n=== SIMULATION: Sampling Rate Performance Test ===" << std::endl;
    std::cout << "Target sampling rate: " << sampling_rate/1e6 << " MHz" << std::endl;
    std::cout << "Processing threads: " << num_threads << std::endl;
    std::cout << "Test duration: " << run_time << " seconds" << std::endl;
    std::cout << "Mode: Windows Simulation (no hardware)" << std::endl;
    std::cout << "================================================\n" << std::endl;
    
    try {
        // Create mock USRP
        uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make("");
        
        // Create threads
        std::vector<std::thread> threads;
        
        // Start RX streamer thread
        threads.emplace_back(rx_streamer_thread, usrp, sampling_rate);
        
        // Start processing threads
        for (int i = 0; i < num_threads; i++) {
            threads.emplace_back(processing_thread, i + 1);
        }
        
        // Run test
        std::this_thread::sleep_for(std::chrono::duration<double>(run_time));
        
        // Stop all threads
        stop_signal.store(true);
        sample_queue.notify_all();
        
        for (auto& t : threads) {
            t.join();
        }
        
        // Calculate final stats
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(end_time - perf_stats.start_time).count();
        if (elapsed > 0) {
            perf_stats.processing_rate = total_blocks.load() / (double)elapsed;
        }
        perf_stats.queue_max_size = sample_queue.get_max_size();
        
        // Print simulation results
        std::cout << "\n=== SIMULATION RESULTS ===" << std::endl;
        std::cout << "Sampling Rate: " << sampling_rate/1e6 << " MHz" << std::endl;
        std::cout << "Test Duration: " << run_time << " seconds" << std::endl;
        std::cout << "Total Blocks: " << total_blocks.load() << std::endl;
        std::cout << "Dropped Blocks: " << dropped_blocks.load() << std::endl;
        std::cout << "Simulated Overflows: " << overflow_count.load() << std::endl;
        std::cout << "Max Queue Size: " << perf_stats.queue_max_size << std::endl;
        std::cout << "Processing Rate: " << std::fixed << std::setprecision(2) 
                  << perf_stats.processing_rate << " blocks/sec" << std::endl;
        
        // Simulation-based assessment
        if (sampling_rate <= 5e6) {
            std::cout << "\n✅ SIMULATION: Excellent performance expected" << std::endl;
        } else if (sampling_rate <= 10e6) {
            std::cout << "\n⚠️  SIMULATION: Good performance, monitor for overflows" << std::endl;
        } else if (sampling_rate <= 15e6) {
            std::cout << "\n⚠️  SIMULATION: Challenging rate, overflows likely" << std::endl;
        } else {
            std::cout << "\n❌ SIMULATION: High overflow risk, reduce sampling rate" << std::endl;
        }
        
        std::cout << "\nNOTE: These are simulated results. Run on Linux with N210 for actual performance." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
    
    return 0;
}
