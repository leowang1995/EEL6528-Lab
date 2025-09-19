// EEL6528 Lab 1: Sampling Rate Analysis for N210
// Tests different sampling rates to find overflow threshold
// Compile: g++ -std=c++17 -O3 -o sampling_test sampling_rate_test.cpp -luhd -pthread

#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/stream.hpp>
#include <uhd/types/tune_request.hpp>
#include <uhd/types/sensors.hpp>
#include <uhd/utils/thread.hpp>

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
#include <sys/resource.h>  // For CPU usage monitoring

// N210 Hardware Configuration
const double RX_FREQ = 2.437e9;
const size_t SAMPLES_PER_BLOCK = 10000;

// Thread control and monitoring variables
std::atomic<bool> stop_signal(false);
std::atomic<size_t> overflow_count(0);
std::atomic<size_t> total_blocks(0);
std::atomic<size_t> dropped_blocks(0);
std::mutex console_mutex;

// Performance monitoring
struct PerformanceStats {
    double cpu_usage = 0.0;
    size_t memory_usage = 0;
    size_t queue_max_size = 0;
    double processing_rate = 0.0;
    std::chrono::steady_clock::time_point start_time;
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

// Enhanced thread-safe FIFO Queue with monitoring
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
        
        // Update max queue size
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

// CPU Usage monitoring
double get_cpu_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    
    long user_time = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec;
    long sys_time = usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;
    
    static long prev_user = 0, prev_sys = 0;
    static auto prev_time = std::chrono::steady_clock::now();
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - prev_time).count();
    
    double cpu_percent = 0.0;
    if (elapsed > 0) {
        cpu_percent = ((user_time - prev_user) + (sys_time - prev_sys)) * 100.0 / elapsed;
    }
    
    prev_user = user_time;
    prev_sys = sys_time;
    prev_time = now;
    
    return cpu_percent;
}

// Memory usage monitoring
size_t get_memory_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss; // Peak memory usage in KB
}

// RX Streamer with performance monitoring
void rx_streamer_thread(uhd::usrp::multi_usrp::sptr usrp, double sampling_rate) {
    
    // Set parameters
    usrp->set_rx_rate(sampling_rate);
    uhd::tune_request_t tune_request(RX_FREQ);
    usrp->set_rx_freq(tune_request);
    usrp->set_rx_gain(20.0);
    
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "Actual RX rate: " << usrp->get_rx_rate()/1e6 << " MHz" << std::endl;
        std::cout << "Actual RX freq: " << usrp->get_rx_freq()/1e9 << " GHz" << std::endl;
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // Create RX streamer
    uhd::stream_args_t stream_args("fc32", "sc16");
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    
    std::vector<std::complex<float>> buff(SAMPLES_PER_BLOCK);
    
    // Start streaming
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    stream_cmd.num_samps = 0;
    stream_cmd.stream_now = true;
    stream_cmd.time_spec = uhd::time_spec_t();
    
    rx_stream->issue_stream_cmd(stream_cmd);
    perf_stats.start_time = std::chrono::steady_clock::now();
    
    uhd::rx_metadata_t md;
    size_t block_counter = 0;
    
    while (!stop_signal.load()) {
        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, 1.0);
        
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_TIMEOUT) {
            continue;
        }
        
        if (md.error_code == uhd::rx_metadata_t::ERROR_CODE_OVERFLOW) {
            overflow_count++;
            std::cerr << "O" << std::flush;
            continue;
        }
        
        if (md.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE) {
            break;
        }
        
        if (num_rx_samps == SAMPLES_PER_BLOCK) {
            SampleBlock block(block_counter++, SAMPLES_PER_BLOCK);
            block.samples = buff;
            
            // Check if queue is getting too full (potential overflow indicator)
            if (sample_queue.size() > 100) {
                dropped_blocks++;
                continue; // Drop this block
            }
            
            sample_queue.push(block);
            total_blocks++;
        }
    }
    
    // Stop streaming
    stream_cmd.stream_mode = uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS;
    rx_stream->issue_stream_cmd(stream_cmd);
}

// Processing thread with performance monitoring
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
        
        // Print every 100th block to avoid output spam
        if (blocks_processed % 100 == 0) {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << "[Thread " << thread_id << "] "
                      << "Block #" << std::setw(6) << block.block_number 
                      << " | Avg Power: " << std::scientific << std::setprecision(3) << avg_power
                      << " | Queue: " << std::setw(3) << sample_queue.size()
                      << " | CPU: " << std::fixed << std::setprecision(1) << get_cpu_usage() << "%"
                      << std::endl;
        }
    }
}

// Performance monitoring thread
void monitor_thread() {
    while (!stop_signal.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        perf_stats.cpu_usage = get_cpu_usage();
        perf_stats.memory_usage = get_memory_usage();
        perf_stats.queue_max_size = sample_queue.get_max_size();
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - perf_stats.start_time).count();
        if (elapsed > 0) {
            perf_stats.processing_rate = total_blocks.load() / (double)elapsed;
        }
    }
}

int main(int argc, char* argv[]) {
    
    if (argc != 4) {
        std::cout << "Usage: " << argv[0] << " <sampling_rate_MHz> <num_threads> <test_duration_sec>" << std::endl;
        std::cout << "Example: " << argv[0] << " 5 2 30" << std::endl;
        return -1;
    }
    
    double sampling_rate = std::stod(argv[1]) * 1e6;  // Convert MHz to Hz
    int num_threads = std::stoi(argv[2]);
    double run_time = std::stod(argv[3]);
    
    try {
        std::cout << "\n=== Sampling Rate Performance Test ===" << std::endl;
        std::cout << "Target sampling rate: " << sampling_rate/1e6 << " MHz" << std::endl;
        std::cout << "Processing threads: " << num_threads << std::endl;
        std::cout << "Test duration: " << run_time << " seconds" << std::endl;
        std::cout << "==========================================\n" << std::endl;
        
        // Create USRP device
        std::string device_args = "addr=192.168.10.2,serial=F51F60";
        uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(device_args);
        
        // Create threads
        std::vector<std::thread> threads;
        
        // Start monitoring thread
        threads.emplace_back(monitor_thread);
        
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
        
        // Print final performance report
        std::cout << "\n=== PERFORMANCE ANALYSIS REPORT ===" << std::endl;
        std::cout << "Sampling Rate: " << sampling_rate/1e6 << " MHz" << std::endl;
        std::cout << "Test Duration: " << run_time << " seconds" << std::endl;
        std::cout << "Total Blocks Received: " << total_blocks.load() << std::endl;
        std::cout << "Blocks Dropped (Queue Full): " << dropped_blocks.load() << std::endl;
        std::cout << "Hardware Overflows: " << overflow_count.load() << std::endl;
        std::cout << "Max Queue Size: " << perf_stats.queue_max_size << std::endl;
        std::cout << "Processing Rate: " << std::fixed << std::setprecision(2) 
                  << perf_stats.processing_rate << " blocks/sec" << std::endl;
        std::cout << "Peak Memory Usage: " << perf_stats.memory_usage << " KB" << std::endl;
        std::cout << "Average CPU Usage: " << std::fixed << std::setprecision(1) 
                  << perf_stats.cpu_usage << "%" << std::endl;
        
        // Performance assessment
        if (overflow_count.load() > 0) {
            std::cout << "\n❌ OVERFLOW DETECTED - Sampling rate too high!" << std::endl;
        } else if (dropped_blocks.load() > 0) {
            std::cout << "\n⚠️  QUEUE OVERFLOW - Processing can't keep up!" << std::endl;
        } else {
            std::cout << "\n✅ SUCCESS - No overflows at " << sampling_rate/1e6 << " MHz" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
    
    return 0;
}
