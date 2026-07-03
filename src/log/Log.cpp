#include "stablecops/log/Log.hpp"

#include <atomic>
#include <iostream>
#include <mutex>
#include <streambuf>

namespace stablecops::log {

namespace {

std::mutex& sinkMutex() {
    static std::mutex mutex;
    return mutex;
}

Sink& sinkStorage() {
    static Sink sink;  // empty => use the built-in sink
    return sink;
}

std::atomic<Level>& minLevel() {
    static std::atomic<Level> level{Level::Info};
    return level;
}

// Built-in sink: whole lines to std::cerr (Error/Warn) and std::cout
// (Info/Debug). Error/Warn are tagged; Info is passed through untagged so the
// human-facing console output looks unchanged. Guarded by a mutex so lines from
// different threads never interleave.
void defaultSink(Level level, const std::string& line) {
    static std::mutex io_mutex;
    std::lock_guard<std::mutex> lock(io_mutex);
    switch (level) {
        case Level::Error:
            std::cerr << "[error] " << line << '\n';
            break;
        case Level::Warn:
            std::cerr << "[warn] " << line << '\n';
            break;
        case Level::Debug:
            std::cout << "[debug] " << line << '\n';
            break;
        case Level::Info:
            std::cout << line << '\n';
            break;
    }
}

// Accumulates characters until a newline, then forwards the completed line to
// the active sink at a fixed level. One instance lives per thread per level (see
// stream()), so accumulation needs no locking; only the sink call is guarded.
class LineStreambuf final : public std::streambuf {
public:
    explicit LineStreambuf(Level level) : level_(level) {}

protected:
    int_type overflow(int_type ch) override {
        if (traits_type::eq_int_type(ch, traits_type::eof())) {
            return traits_type::not_eof(ch);
        }
        const char c = traits_type::to_char_type(ch);
        if (c == '\n') {
            flushLine();
        } else {
            line_.push_back(c);
        }
        return ch;
    }

    std::streamsize xsputn(const char* s, std::streamsize count) override {
        for (std::streamsize i = 0; i < count; ++i) {
            overflow(traits_type::to_int_type(s[i]));
        }
        return count;
    }

private:
    void flushLine() {
        write(level_, line_);
        line_.clear();
    }

    Level level_;
    std::string line_;
};

}  // namespace

void setSink(Sink sink) {
    std::lock_guard<std::mutex> lock(sinkMutex());
    sinkStorage() = std::move(sink);
}

void setLevel(Level level) {
    minLevel().store(level, std::memory_order_relaxed);
}

Level level() {
    return minLevel().load(std::memory_order_relaxed);
}

bool enabled(Level level) {
    return static_cast<int>(level) <= static_cast<int>(minLevel().load(std::memory_order_relaxed));
}

const char* toString(Level level) {
    switch (level) {
        case Level::Error:
            return "error";
        case Level::Warn:
            return "warn";
        case Level::Info:
            return "info";
        case Level::Debug:
            return "debug";
    }
    return "info";
}

void write(Level level, const std::string& line) {
    if (!enabled(level)) {
        return;
    }
    Sink sink;
    {
        std::lock_guard<std::mutex> lock(sinkMutex());
        sink = sinkStorage();
    }
    if (sink) {
        sink(level, line);
    } else {
        defaultSink(level, line);
    }
}

std::ostream& stream(Level level) {
    thread_local LineStreambuf error_buf(Level::Error);
    thread_local LineStreambuf warn_buf(Level::Warn);
    thread_local LineStreambuf info_buf(Level::Info);
    thread_local LineStreambuf debug_buf(Level::Debug);
    thread_local std::ostream error_stream(&error_buf);
    thread_local std::ostream warn_stream(&warn_buf);
    thread_local std::ostream info_stream(&info_buf);
    thread_local std::ostream debug_stream(&debug_buf);
    switch (level) {
        case Level::Error:
            return error_stream;
        case Level::Warn:
            return warn_stream;
        case Level::Debug:
            return debug_stream;
        case Level::Info:
            break;
    }
    return info_stream;
}

}  // namespace stablecops::log
