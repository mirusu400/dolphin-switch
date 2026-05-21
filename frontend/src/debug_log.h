// Debug log facility for the Switch frontend.
//
// Three sinks every message goes to:
//   1. stdout/stderr (visible on PC via `nxlink -s`)
//   2. SD file at `sdmc:/switch/dolphin/logs/dolphin-switch-YYYYMMDD-HHMMSS.log`
//      (flushed every line so a crash leaves a trail)
//   3. In-process ring buffer for the ImGui log window (capped, snapshot reads
//      so Dolphin's emulation thread can write diagnostics safely)
//
// Use the DBG_* macros at call sites — they capture file/line so the log shows
// where each line came from. Hardware bring-up code should emit liberally;
// performance budget is irrelevant during M0/M1/M5-prep validation.

#pragma once

#include <cstdarg>
#include <cstddef>
#include <string>
#include <vector>

namespace dbg
{

enum class Level
{
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warn = 3,
  Error = 4,
};

// Initialize sinks. Safe to call before SDL/console are ready — file open
// happens lazily and stdio writes are tolerant of a closed nxlink fd.
void Init();

// Flush + close the file sink. Ring buffer + stdio remain until process exit.
void Shutdown();

// printf-style log entry. Use the DBG_* macros — they capture file:line.
void LogV(Level level, const char* file, int line, const char* fmt, std::va_list args);
void LogF(Level level, const char* file, int line, const char* fmt, ...);

// Probe everything interesting about the Switch runtime — GL strings, libnx
// version, JIT capability (per docs/jit-memory.md), memory budget, applet
// state. Call once after SDL/GL are up.
void DumpSystemInfo();

// Snapshot the in-process ring buffer for the ImGui log window.
std::vector<std::string> RingBufferSnapshot();

}  // namespace dbg

#define DBG_TRACE(...) ::dbg::LogF(::dbg::Level::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define DBG_DEBUG(...) ::dbg::LogF(::dbg::Level::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define DBG_INFO(...)  ::dbg::LogF(::dbg::Level::Info,  __FILE__, __LINE__, __VA_ARGS__)
#define DBG_WARN(...)  ::dbg::LogF(::dbg::Level::Warn,  __FILE__, __LINE__, __VA_ARGS__)
#define DBG_ERROR(...) ::dbg::LogF(::dbg::Level::Error, __FILE__, __LINE__, __VA_ARGS__)
