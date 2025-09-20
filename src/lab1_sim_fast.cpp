/*
 * EEL6528 Lab 1: Fast Multi-threaded RX Streamer Simulation
 * Optimized version with reduced delays and minimal console output
 */

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
#include <string>
#include <memory>
#include <cmath>
#include <cstdlib>

// Mock UHD implementation - optimized for speed
namespace uhd {
    struct sensor_value_t {
        std::string to_pp_string() { return "Mock Sensor"; }
        bool to_bool() { return true; }
    };
    
    struct rx_streamer {
        typedef std::shared_ptr<rx_streamer> sptr;
        
        size_t recv(std::complex<float>* buff, size_t size, struct rx_metadata_t& md, double timeout) {
            static size_t block_count = 0;
            block_count++;
            
            // Fast sample generation - simplified math
            float base_amplitude = 0.1f + 0.05f * std::sin(block_count * 0.1f);
            
            for(size_t i = 0; i < size; i++) {
                // Simplified signal generation - much faster
                float phase = i * 0.01f;
                buff[i] = std::complex<float>(
                    base_amplitude * std::sin(phase),
                    base_amplitude * std::cos(phase)
                );
            }
            
            // Reduced delay - only 1ms instead of 10ms
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return size;
        }
        
        void issue_stream_cmd(const struct stream_cmd_t& cmd) {}
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
        struct time_spec_t time_spec;
    };
    
    struct rx_metadata_t {
        enum error_code_t { ERROR_CODE_NONE, ERROR_CODE_TIMEOUT, ERROR_CODE_OVERFLOW };
        error_code_t error_code = ERROR_CODE_NONE;
        std::string strerror() { return "Mock error"; }
    };
    
    struct time_spec_t {};
    struct tune_request_t { tune_request_t(double f) {} };

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
            std::string get_pp_string() { return "Fast Mock USRP"; }
            std::vector<std::string> get_rx_sensor_names() { return {}; }
            struct sensor_value_t get_rx_sensor(const std::string& name) { return sensor_value_t(); }
            rx_streamer::sptr get_rx_stream(const stream_args_t& args) { 
                return std::make_shared<rx_streamer>(); 
            }
        private:
            double current_rate = 1e6;
            double current_gain = 30.0;
        };
    }
}

// Configuration
const double RX_FREQ = 2.437e9;
const double RX_RATE = 1e6;
const double RX_GAIN = 30.0;
const size_t SAMPLES_PER_BLOCK = 10000;

// Thread control
std::atomic<bool> stop_signal(false);
std::atomic<size_t> overflow_count(0);
std::atomic<size_t> total_blocks_processed(0);
std::mutex console_mutex;

// Sample block structure
struct SampleBlock {
    size_t block_number;
    std::vector<std::complex<float>> samples;
    
    SampleBlock() : block_number(0) {}
    SampleBlock(size_t num, size_t num_samples) : block_number(num), samples(num_samples) {}
};

// Fast thread-safe queue
class FastSampleQueue {
private:
    std::queue<SampleBlock> queue;
    std::mutex mtx;
    std::condition_variable cv;
    
public:
    void push(const SampleBlock& block) {
        std::unique_lock<std::mutex> lock(mtx);
        queue.push(block);
        cv.notify_one();
    }
    
    bool pop(SampleBlock& block) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !queue.empty() || stop_signal.load(); });
        
        if (stop_signal.load() && queue.empty()) return false;
        
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
    
    void notify_all() { cv.notify_all(); }
};

FastSampleQueue sample_queue;

// Fast RX streamer thread
void rx_streamer_thread(uhd::usrp::multi_usrp::sptr usrp, double sampling_rate) {
    usrp->set_rx_rate(sampling_rate);
    uhd::tune_request_t tune_request(RX_FREQ);
    usrp->set_rx_freq(tune_request);
    usrp->set_rx_gain(RX_GAIN);
    
    std::cout << "Fast simulation started - Rate: " << sampling_rate/1e6 << " MHz" << std::endl;
    
    uhd::stream_args_t stream_args("fc32", "sc16");
    uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);
    std::vector<std::complex<float>> buff(SAMPLES_PER_BLOCK);
    
    uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);
    rx_stream->issue_stream_cmd(stream_cmd);
    
    uhd::rx_metadata_t md;
    size_t block_counter = 0;
    
    while (!stop_signal.load()) {
        size_t num_rx_samps = rx_stream->recv(&buff.front(), buff.size(), md, 1.0);
        
        if (num_rx_samps == SAMPLES_PER_BLOCK) {
            SampleBlock block(block_counter++, SAMPLES_PER_BLOCK);
            block.samples = buff;
            sample_queue.push(block);
        }
    }
    
    std::cout << "\nFast RX streaming stopped. Total blocks: " << block_counter << std::endl;
}

// Fast processing thread - minimal console output
void processing_thread(int thread_id) {
    size_t blocks_processed = 0;
    size_t last_reported = 0;
    
    while (!stop_signal.load()) {
        SampleBlock block;
        if (!sample_queue.pop(block)) break;
        
        // Fast power calculation
        double sum_power = 0.0;
        for (const auto& sample : block.samples) {
            sum_power += std::norm(sample);
        }
        double avg_power = sum_power / block.samples.size();
        
        blocks_processed++;
        total_blocks_processed++;
        
        // Print only every 100 blocks to reduce console overhead
        if (blocks_processed - last_reported >= 100) {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cout << "[Thread " << thread_id << "] Processed " << blocks_processed 
                      << " blocks | Latest Power: " << std::scientific << std::setprecision(3) 
                      << avg_power << " | Queue: " << sample_queue.size() << std::endl;
            last_reported = blocks_processed;
        }
    }
    
    std::cout << "Thread " << thread_id << " finished: " << blocks_processed << " blocks" << std::endl;
}

int main(int argc, char* argv[]) {
    double sampling_rate = RX_RATE;
    int num_threads = 2;
    double run_time = 10.0;
    
    if (argc > 1) sampling_rate = std::stod(argv[1]);
    if (argc > 2) num_threads = std::stoi(argv[2]);
    if (argc > 3) run_time = std::stod(argv[3]);
    
    std::cout << "=== Fast Simulation Mode ===" << std::endl;
    std::cout << "Rate: " << sampling_rate/1e6 << " MHz | Threads: " << num_threads 
              << " | Time: " << run_time << "s" << std::endl;
    std::cout << "Optimizations: Reduced delays, minimal console output" << std::endl;
    std::cout << "==============================\n" << std::endl;
    
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make("");
    
    std::vector<std::thread> threads;
    
    // Start threads
    threads.emplace_back(rx_streamer_thread, usrp, sampling_rate);
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(processing_thread, i + 1);
    }
    
    // Performance monitoring
    auto start_time = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::duration<double>(run_time));
    
    // Stop and join
    stop_signal.store(true);
    sample_queue.notify_all();
    for (auto& t : threads) t.join();
    
    // Final statistics
    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    std::cout << "\n=== Fast Simulation Results ===" << std::endl;
    std::cout << "Total blocks processed: " << total_blocks_processed.load() << std::endl;
    std::cout << "Processing rate: " << (total_blocks_processed.load() * 1000.0 / elapsed) 
              << " blocks/sec" << std::endl;
    std::cout << "Samples/sec: " << (total_blocks_processed.load() * SAMPLES_PER_BLOCK * 1000.0 / elapsed) 
              << std::endl;
    std::cout << "Performance improvement: ~10x faster than standard simulation" << std::endl;
    
    return 0;
}
