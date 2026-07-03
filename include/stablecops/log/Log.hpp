#pragma once

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>

// Pluggable logging for the stableCOPS runtime.
//
// The library never writes to std::cout / std::cerr directly; it writes to the
// level-tagged streams below (log::err/warn/out/debug). By default those are
// forwarded, one whole line at a time, to std::cerr (Error/Warn) and std::cout
// (Info/Debug). An embedding application can install its own sink to redirect,
// prefix (timestamp, node id), rate-limit, or silence all library output, and
// can raise/lower the minimum level.
//
// Usage mirrors an ostream, so existing `<<` chains and the writeHex/ostream
// helpers keep working:
//
//   stablecops::log::out() << "enabled, statusword=" << sw << '\n';
//   stablecops::log::err() << "fault: code=" << code << '\n';
//
// A message is delivered to the sink when a '\n' is streamed, so always end a
// log line with '\n' (a partial line is held until the next newline). Each
// level has one stream per thread, so building a line across several statements
// on the same thread is fine as long as no other newline is emitted in between.
namespace stablecops::log {

enum class Level : std::uint8_t { Error = 0, Warn = 1, Info = 2, Debug = 3 };

// Receives one fully-formatted line (without the trailing newline). May be
// invoked from any thread the library logs on (primarily the bus loop thread),
// so an implementation that touches shared state must do its own locking.
using Sink = std::function<void(Level, const std::string&)>;

// Install a custom sink; pass nullptr to restore the built-in stderr/stdout
// sink. Thread-safe.
void setSink(Sink sink);

// Minimum level that is emitted; messages below it are dropped. Default Info
// (Debug suppressed). Thread-safe.
void setLevel(Level level);
Level level();
bool enabled(Level level);

const char* toString(Level level);

// Emit one already-formatted line at the given level (used by the streams; also
// usable directly).
void write(Level level, const std::string& line);

// Level-tagged, line-buffered output stream (one instance per thread per level).
std::ostream& stream(Level level);

inline std::ostream& err() { return stream(Level::Error); }
inline std::ostream& warn() { return stream(Level::Warn); }
inline std::ostream& out() { return stream(Level::Info); }
inline std::ostream& debug() { return stream(Level::Debug); }

}  // namespace stablecops::log
