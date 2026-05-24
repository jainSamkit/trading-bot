#pragma once

// Tiny .env loader — no dependencies.
//
// Semantics
// ─────────
// • Parses KEY=VALUE lines (Bourne-shell flavour, not POSIX).
// • Blank lines and lines starting with `#` are ignored.
// • Surrounding quotes (single or double) on the value are stripped.
// • `setenv(..., overwrite=0)` so a value already in the real process
//   environment WINS over the file. This lets CI / Docker / `INFLUX_TOKEN=...
//   ./trading_bot` override anything in .env.
// • `require(name)` throws std::runtime_error if the var is unset/empty —
//   call this for credentials that have no safe default.
// • `get_or(name, fallback)` returns the env value or fallback.

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace env {

namespace detail {

inline std::string_view strip(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t' ||
                          s.back()  == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

inline std::string unquote(std::string_view s) {
    if (s.size() >= 2 &&
        ((s.front() == '"'  && s.back() == '"') ||
         (s.front() == '\'' && s.back() == '\''))) {
        s.remove_prefix(1);
        s.remove_suffix(1);
    }
    return std::string(s);
}

}  // namespace detail

// Returns true if the file was opened (even if empty). Returns false if the
// file does not exist — callers can decide whether that's fatal.
inline bool load_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;

    std::string line;
    while (std::getline(in, line)) {
        std::string_view sv = detail::strip(line);
        if (sv.empty() || sv.front() == '#') continue;

        const auto eq = sv.find('=');
        if (eq == std::string_view::npos) continue;

        std::string key{detail::strip(sv.substr(0, eq))};
        std::string val = detail::unquote(detail::strip(sv.substr(eq + 1)));

        if (key.empty()) continue;
        ::setenv(key.c_str(), val.c_str(), /*overwrite=*/0);
    }
    return true;
}

inline std::string require(const char* name) {
    const char* v = std::getenv(name);
    if (!v || *v == '\0') {
        throw std::runtime_error(std::string("required env var not set: ") + name);
    }
    return v;
}

inline std::string get_or(const char* name, std::string_view fallback) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : std::string(fallback);
}

inline int get_int_or(const char* name, int fallback) {
    const char* v = std::getenv(name);
    if (!v || *v == '\0') return fallback;
    try { return std::stoi(v); }
    catch (...) { return fallback; }
}

}  // namespace env
