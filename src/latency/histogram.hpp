#pragma once 

#include <cstdint>                                                                                                                                                                                                                                                                             
#include <atomic>                                                                                                                                                                                                                                                                              
#include <array>            
#include "latency/clock.hpp"   // for cycles_to_ns

namespace latency {
    class Histogram {
        static constexpr int N_BUCKETS = 64;

        public:
        Histogram() noexcept = default;
        
        inline void record_ns(uint64_t ns) noexcept{
            if(ns < 1) ns = 1;
            int b_index = 63 - __builtin_clzll(ns);
            buckets_[b_index].fetch_add(1, std::memory_order_relaxed);
        }

        inline void record_cycles(uint64_t cycles) noexcept{
            record_ns(cycles_to_ns(cycles));
        }

        uint64_t count() const noexcept {
            uint64_t count = 0;
            for(int i=0;i<N_BUCKETS;i++) {
                count += buckets_[i].load(std::memory_order_relaxed);
            }
            return count;
        }

        void snapshot_percentile(uint64_t& p50, uint64_t& p90, uint64_t& p99, uint64_t& total) const noexcept {
            total = count();
            uint64_t p_50_count = static_cast<uint64_t>(static_cast<double>(total) * 0.5); 
            uint64_t p_90_count = static_cast<uint64_t>(static_cast<double>(total) * 0.9); 
            uint64_t p_99_count = static_cast<uint64_t>(static_cast<double>(total) * 0.99);
            

            bool is_p50_set = false, is_p90_set = false, is_p99_set = false;

            uint64_t curr_count = 0;

            for(int i=0;i<N_BUCKETS;i++) {
                curr_count += buckets_[i].load(std::memory_order_relaxed);
                if(!is_p50_set && curr_count >= p_50_count) {
                    is_p50_set = true;
                    p50 = 1ULL << i;
                }
                if(!is_p90_set && curr_count >= p_90_count ) {
                    is_p90_set = true;
                    p90 = 1ULL << i;
                }
                if(!is_p99_set && curr_count >= p_99_count) {
                    is_p99_set = true;
                    p99 = 1ULL << i;
                    break;
                }
            }
            return;
        }


        uint64_t bucket_count(int i) const noexcept{
            return buckets_[i].load(std::memory_order_relaxed);
        }

        void reset() noexcept {
            for(int i=0;i<N_BUCKETS;i++) buckets_[i].store(0, std::memory_order_relaxed);
        }

        private:
            alignas(64) std::array<std::atomic<uint64_t>, N_BUCKETS> buckets_{};

    };
}