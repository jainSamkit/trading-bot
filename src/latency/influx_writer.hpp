#pragma once

// InfluxDB v2 line-protocol writer.
//
// Design notes
// ────────────
// • Called only from the Registry's single push thread — no internal locking.
// • `push_line` appends to an in-memory buffer; `flush` POSTs the buffer.
// • Auto-flush when buffer exceeds `cfg.max_batch_bytes` so memory stays bounded
//   even if the push interval grows.
// • Errors are swallowed (telemetry must never block trading). A failure counter
//   is exposed via `failed_flushes()` so a Grafana panel can spot the gap.
// • Constructor + `flush` are NOT marked noexcept — httplib + std::string can
//   throw on OOM / DNS / cert issues. Acceptable because the push thread is
//   isolated from the hot path.

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <httplib.h>

#include "latency/tag_set.hpp"

namespace latency {

class InfluxWriter {
public:
    struct Config {
        std::string host      = "127.0.0.1";
        int         port      = 8086;
        std::string org;
        std::string bucket;
        std::string token;
        std::string precision = "ns";          // ns | us | ms | s
        std::size_t max_batch_bytes = 64 * 1024;
    };

    explicit InfluxWriter(Config cfg)
        : cfg_(std::move(cfg)),
          client_(cfg_.host, cfg_.port),
          path_(build_path(cfg_)),
          auth_("Token " + cfg_.token)
    {
        client_.set_keep_alive(true);
        client_.set_connection_timeout(2);   // seconds
        client_.set_read_timeout(2);
        client_.set_write_timeout(2);
    }

    InfluxWriter(const InfluxWriter&)            = delete;
    InfluxWriter& operator=(const InfluxWriter&) = delete;
    InfluxWriter(InfluxWriter&&)                 = delete;
    InfluxWriter& operator=(InfluxWriter&&)      = delete;

    void push_line(std::string_view line) {
        if (!buffer_.empty()) buffer_ += '\n';
        buffer_.append(line.data(), line.size());

        if (buffer_.size() >= cfg_.max_batch_bytes) {
            flush();
        }
    }

    void flush() {
        if (buffer_.empty()) return;

        httplib::Headers headers = {
            {"Authorization", auth_},
            {"Content-Type",  "text/plain; charset=utf-8"}
        };

        auto res = client_.Post(path_.c_str(), headers,
                                buffer_, "text/plain; charset=utf-8");

        if (!res || res->status / 100 != 2) {
            failed_flushes_.fetch_add(1, std::memory_order_relaxed);
        }

        buffer_.clear();
    }

    std::size_t buffered_bytes() const noexcept { return buffer_.size(); }
    uint64_t    failed_flushes() const noexcept {
        return failed_flushes_.load(std::memory_order_relaxed);
    }

private:
    static std::string build_path(const Config& c) {
        return "/api/v2/write?org="  + c.org
             + "&bucket="            + c.bucket
             + "&precision="         + c.precision;
    }

    Config          cfg_;
    httplib::Client client_;
    std::string     path_;
    std::string     auth_;
    std::string     buffer_;
    std::atomic<uint64_t> failed_flushes_{0};
};

// ── InfluxDB line-protocol formatter ──────────────────────────────────────────
// Format:
//   latency,venue=<v>,event=<e>[,msg=<m>][,target=<t>] p50=<>,p90=<>,p99=<>,count=<>
//
// Tag values that come back nullptr from TagSet::to_string (None variants) are
// omitted entirely — InfluxDB tag indexes don't tolerate empty values.
inline std::string format_line(const TagSet& tags,
                               TagSet::Venue venue,
                               uint64_t      p50,
                               uint64_t      p90,
                               uint64_t      p99,
                               uint64_t      total) {
    std::string line = "latency,venue=";
    line += TagSet::to_string(venue);
    line += ",event=";
    line += TagSet::to_string(tags.event_type);

    if (const char* m = TagSet::to_string(tags.msg_type)) {
        line += ",msg=";
        line += m;
    }
    if (const char* t = TagSet::to_string(tags.target)) {
        line += ",target=";
        line += t;
    }

    line += " p50=";   line += std::to_string(p50);
    line += ",p90=";   line += std::to_string(p90);
    line += ",p99=";   line += std::to_string(p99);
    line += ",count="; line += std::to_string(total);
    return line;
}

}  // namespace latency
