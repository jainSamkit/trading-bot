#pragma once                                                                                                                                                                                                                                                                                   
                                                                      
#include <cstdint>              // uint64_t                                                                                                                                                                                                                                                    
#include "latency/clock.hpp"    // now_cycles()             
#include "latency/histogram.hpp"  // Histogram  

namespace latency {

    class Span {
        Histogram* hist_;
        uint64_t start_;

        public:
        explicit Span(Histogram* h) noexcept : hist_(h), start_(now_cycles()) {}

        ~Span() noexcept {
            hist_->record_cycles(now_cycles() - start_);
        }

        Span(const Span&) = delete;
        Span& operator=(const Span&) = delete;
        Span(Span&&) = delete;
        Span& operator=(Span&&) = delete;
    };
}