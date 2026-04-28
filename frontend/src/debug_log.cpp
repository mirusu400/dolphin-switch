// Debug log facility — see debug_log.h.

#include "debug_log.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <time.h>  // POSIX timespec / clock_gettime — newlib does not put these in std::
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

#include <switch.h>
#include <GLES3/gl3.h>

namespace dbg
{

namespace
{
constexpr const char* kLogDir = "sdmc:/switch/dolphin/logs";
constexpr std::size_t kRingCapacity = 1024;

std::mutex g_mu;
std::FILE* g_file = nullptr;
std::deque<std::string> g_ring;

const char* LevelTag(Level l)
{
  switch (l)
  {
  case Level::Trace: return "TRACE";
  case Level::Debug: return "DEBUG";
  case Level::Info:  return "INFO ";
  case Level::Warn:  return "WARN ";
  case Level::Error: return "ERROR";
  }
  return "?????";
}

// "main.cpp" from "/path/to/main.cpp" — no allocation.
const char* BasenameOf(const char* path)
{
  if (!path)
    return "?";
  const char* b = path;
  for (const char* p = path; *p; ++p)
    if (*p == '/' || *p == '\\')
      b = p + 1;
  return b;
}

std::string TimestampNow()
{
  ::timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  std::tm tm{};
  std::time_t s = ts.tv_sec;
  localtime_r(&s, &tm);
  char buf[32];
  int ms = static_cast<int>(ts.tv_nsec / 1'000'000);
  std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
  return buf;
}

std::string FilenameNow()
{
  std::time_t s = std::time(nullptr);
  std::tm tm{};
  localtime_r(&s, &tm);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

void OpenFileSink()
{
  std::error_code ec;
  std::filesystem::create_directories(kLogDir, ec);
  std::string path = std::string(kLogDir) + "/dolphin-switch-" + FilenameNow() + ".log";
  g_file = std::fopen(path.c_str(), "w");
  if (!g_file)
  {
    std::fprintf(stderr, "[dbg] could not open %s for writing\n", path.c_str());
  }
  else
  {
    // Flush after every line — crashes leave the file consistent.
    std::setvbuf(g_file, nullptr, _IOLBF, 0);
    std::fprintf(g_file, "[%s][INFO ][debug_log.cpp:%d] log file opened: %s\n",
                 TimestampNow().c_str(), __LINE__, path.c_str());
  }
}
}  // namespace

void Init()
{
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_file)
    return;
  OpenFileSink();
}

void Shutdown()
{
  std::lock_guard<std::mutex> lock(g_mu);
  if (g_file)
  {
    std::fprintf(g_file, "[%s][INFO ][debug_log.cpp:%d] log file closing.\n",
                 TimestampNow().c_str(), __LINE__);
    std::fclose(g_file);
    g_file = nullptr;
  }
}

void LogV(Level level, const char* file, int line, const char* fmt, std::va_list args)
{
  // Format the message body once; reuse for every sink.
  char body[1024];
  std::vsnprintf(body, sizeof(body), fmt, args);

  std::string line_str;
  line_str.reserve(64 + std::strlen(body));
  line_str += "[";
  line_str += TimestampNow();
  line_str += "][";
  line_str += LevelTag(level);
  line_str += "][";
  line_str += BasenameOf(file);
  line_str += ":";
  line_str += std::to_string(line);
  line_str += "] ";
  line_str += body;

  std::lock_guard<std::mutex> lock(g_mu);

  // Sink 1: stdio (nxlink + on-screen console).
  std::FILE* out = (level >= Level::Warn) ? stderr : stdout;
  std::fputs(line_str.c_str(), out);
  std::fputc('\n', out);

  // Sink 2: SD file.
  if (g_file)
  {
    std::fputs(line_str.c_str(), g_file);
    std::fputc('\n', g_file);
  }

  // Sink 3: ring buffer for the ImGui log panel.
  if (g_ring.size() >= kRingCapacity)
    g_ring.pop_front();
  g_ring.push_back(std::move(line_str));
}

void LogF(Level level, const char* file, int line, const char* fmt, ...)
{
  std::va_list ap;
  va_start(ap, fmt);
  LogV(level, file, line, fmt, ap);
  va_end(ap);
}

const std::deque<std::string>& RingBuffer()
{
  // Caller must observe the threading contract documented in the header
  // (single-threaded UI access today). Return without locking — copying
  // the deque on every frame would make the ImGui panel quadratic.
  return g_ring;
}

void DumpSystemInfo()
{
  DBG_INFO("=== Switch runtime probe ===");

  // libnx env / applet info.
  AppletType applet_type = appletGetAppletType();
  DBG_INFO("appletGetAppletType: %d", static_cast<int>(applet_type));
  DBG_INFO("envIsNso: %d  envHasHeapOverride: %d  envHasArgv: %d",
           envIsNso() ? 1 : 0,
           envHasHeapOverride() ? 1 : 0,
           envHasArgv() ? 1 : 0);

  // Memory budget — see docs/jit-memory.md §memory-budget.
  u64 total_mem = 0;
  Result rc = svcGetInfo(&total_mem, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  if (R_SUCCEEDED(rc))
    DBG_INFO("svcGetInfo TotalMemorySize: %llu bytes (%llu MiB)",
             static_cast<unsigned long long>(total_mem),
             static_cast<unsigned long long>(total_mem >> 20));
  else
    DBG_WARN("svcGetInfo TotalMemorySize failed: rc=0x%08X", rc);

  u64 used_mem = 0;
  rc = svcGetInfo(&used_mem, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
  if (R_SUCCEEDED(rc))
    DBG_INFO("svcGetInfo UsedMemorySize: %llu bytes (%llu MiB)",
             static_cast<unsigned long long>(used_mem),
             static_cast<unsigned long long>(used_mem >> 20));

  // JIT capability probe — per CLAUDE.md M2 critical-constraint #1. If this
  // fails, we are not running under hbloader and the JIT path will fault.
  Jit probe{};
  Result jit_rc = jitCreate(&probe, 0x100000);  // 1 MiB scratch
  if (R_SUCCEEDED(jit_rc))
  {
    DBG_INFO("JIT capability probe: PASS (jitCreate(1MiB) ok)");
    DBG_INFO("  rw_addr=%p rx_addr=%p", jitGetRwAddr(&probe), jitGetRxAddr(&probe));
    jitClose(&probe);
  }
  else
  {
    DBG_ERROR("JIT capability probe: FAIL (rc=0x%08X). Launch from hbmenu.",
              jit_rc);
  }

  // GL probe — Mesa nouveau identifies itself here. Useful for confirming
  // the EGL surface actually came up with a real GLES context.
  const GLubyte* gl_vendor = glGetString(GL_VENDOR);
  const GLubyte* gl_renderer = glGetString(GL_RENDERER);
  const GLubyte* gl_version = glGetString(GL_VERSION);
  const GLubyte* gl_glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
  DBG_INFO("GL_VENDOR:    %s",
           gl_vendor ? reinterpret_cast<const char*>(gl_vendor) : "(null)");
  DBG_INFO("GL_RENDERER:  %s",
           gl_renderer ? reinterpret_cast<const char*>(gl_renderer) : "(null)");
  DBG_INFO("GL_VERSION:   %s",
           gl_version ? reinterpret_cast<const char*>(gl_version) : "(null)");
  DBG_INFO("GLSL_VERSION: %s",
           gl_glsl ? reinterpret_cast<const char*>(gl_glsl) : "(null)");

  DBG_INFO("=== end runtime probe ===");
}

}  // namespace dbg
