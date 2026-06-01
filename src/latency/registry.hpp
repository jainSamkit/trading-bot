
#pragma once                                                                                                                                                                                                                                                                                                                            
#include <thread>                                                                                                                                                                                                                                                                              
#include <mutex>                                                                                                                                                                                                                                                                               
#include <map>                                   
#include <memory>                                                                                                                                                                                                                                                                              
#include <atomic>                                           
#include <chrono>                                                    
#include <string>           
#include <pthread.h>
#include <sched.h>                                                                                                                                                                                                                                                                             
                                        
#include "latency/histogram.hpp"
#include "latency/tag_set.hpp"
#include "latency/influx_writer.hpp"
#include "config/config.hpp"

namespace latency {

    class Registry {
        public:

            Registry() = delete;
            ~Registry() = delete;

            static void init(const TagSet::Venue venue) {
                venue_ = venue;
            }

            static Histogram* get_or_create(const TagSet& tags) {

                std::lock_guard lk{map_mutex_};
                auto [it, _] = tagset_map_.try_emplace(tags, std::make_unique<Histogram>());
                return it->second.get();
            }

            static void start_periodic_push(InfluxWriter* writer, int interval_s, int pin_to_core = -1) {
                if(running_.exchange(true)) return;
                //run this inside a thread
                push_thread_ = std::thread([writer, interval_s, pin_to_core]() {

                        // ── Defeat inherited hot-thread scheduling ──────────────
                        // pthread_create defaults to PTHREAD_INHERIT_SCHED, so a
                        // push thread spawned by a SCHED_FIFO busy-spin parent
                        // also runs SCHED_FIFO at the same priority — and is then
                        // starved indefinitely because the parent never blocks.
                        // Reset to plain CFS (SCHED_OTHER) and pin to housekeeping
                        // cores (0–1) so we share with the OS, not the hot path.
                        // macOS no-ops both calls (no SCHED_FIFO inheritance to
                        // worry about there anyway).
                    #if defined(__linux__)
                        sched_param sp_other{};      // prio 0 — required for SCHED_OTHER
                        pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp_other);

                        cpu_set_t hk_set;
                        CPU_ZERO(&hk_set);
                        CPU_SET(cfg::cpu::HOUSEKEEPING_CORE_LO, &hk_set);
                        CPU_SET(cfg::cpu::HOUSEKEEPING_CORE_HI, &hk_set);
                        pthread_setaffinity_np(pthread_self(), sizeof(hk_set), &hk_set);
                    #endif

                    if (pin_to_core >= 0) {
                        // Explicit override still respected if caller passes
                        // a specific core — e.g. for benchmarks.
                        cpu_set_t cpuset;
                        CPU_ZERO(&cpuset);
                        CPU_SET(pin_to_core, &cpuset);
                        pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
                    }

                    while(running_.load(std::memory_order_relaxed)) {
                        std::this_thread::sleep_for(std::chrono::seconds(interval_s));

                        if(!running_.load(std::memory_order_relaxed)) break;
                        {
                            std::lock_guard lk{map_mutex_};
                            for(const auto& [tags, hist_uptr] : tagset_map_) {
                                uint64_t p50 = 0, p90 = 0, p99 = 0, total = 0;
                                hist_uptr->snapshot_percentile(p50, p90, p99, total);
                                if (total == 0) continue;   // skip empty histograms — nothing meaningful to push
                                std::string line = format_line(tags, venue_, p50,p90,p99, total);
                                writer->push_line(line);
                            }
                        }

                        writer->flush();
                    }
                });
            }

            static void stop_periodic_push() {

                if(!running_.exchange(false)) return;
                if(push_thread_.joinable()) push_thread_.join();
            }

        private:
            inline static TagSet::Venue venue_;
            inline static std::map<TagSet, std::unique_ptr<Histogram>> tagset_map_;
            inline static std::mutex map_mutex_;
            inline static std::thread push_thread_;
            inline static std::atomic<bool> running_{false};
    };
}