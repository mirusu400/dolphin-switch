// M5-prep ImGui frontend skeleton.
//
// Boots SDL2 + libnx EGL + GLES 3.2, hands ImGui its SDL2/OpenGL3 backends,
// renders the ImGui demo window, exits on + button. Designed to be a
// drop-in foundation for M5's actual game browser / settings UI.
//
// Path layout:
//   - Hello-world stays available before the SDL2 window is up so nxlink
//     gets early-boot diagnostics.
//   - UICommon::Init/Shutdown stays wired so dolphin libs remain pulled
//     in (frontend NRO must keep the dolphin code linked).
//   - Host_* stubs remain — frontend->core callback contract.
//   - Newlib symbol-gap stubs live in switch_libc_shim.c.

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

#include <unistd.h>

#include <switch.h>

#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "UICommon/UICommon.h"
#include "Common/Version.h"
#include "Common/FileUtil.h"
#include "Common/Config/Config.h"
#include "Common/Logging/Log.h"
#include "Common/Logging/LogManager.h"
#include "Common/WindowSystemInfo.h"
#include "Core/Config/MainSettings.h"
#include "Core/ConfigManager.h"
#include "Core/BootManager.h"
#include "Core/Boot/Boot.h"
#include "Core/Core.h"
#include "Core/Host.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"

#include "debug_log.h"

// ----------------------------------------------------------------------------
// Frontend->Core callback stubs (Host_* contract). M5 main UI replaces these
// with real bodies; for the skeleton they all return defaults.
// ----------------------------------------------------------------------------
std::vector<std::string> Host_GetPreferredLocales() { return {"en"}; }
bool Host_UIBlocksControllerState() { return false; }
bool Host_RendererHasFocus() { return true; }
bool Host_RendererHasFullFocus() { return true; }
bool Host_RendererIsFullscreen() { return true; }
bool Host_TASInputHasFocus() { return false; }
void Host_Message(HostMessageID) {}
void Host_PPCSymbolsChanged() {}
void Host_PPCBreakpointsChanged() {}
void Host_RequestRenderWindowSize(int, int) {}
void Host_UpdateDisasmDialog() {}
void Host_JitCacheInvalidation() {}
void Host_JitProfileDataWiped() {}
void Host_UpdateTitle(const std::string&) {}
void Host_YieldToUI() {}
void Host_TitleChanged() {}
void Host_UpdateDiscordClientID(const std::string&) {}
bool Host_UpdateDiscordPresenceRaw(const std::string&, const std::string&, const std::string&,
                                   const std::string&, const std::string&, const std::string&,
                                   const int64_t, const int64_t, const int, const int)
{
  return false;
}
std::unique_ptr<GBAHostInterface> Host_CreateGBAHost(std::weak_ptr<HW::GBA::Core>)
{
  return nullptr;
}

// ----------------------------------------------------------------------------
// nxlink debug output — Switch homebrew tradition. Plumbs stdout/stderr to
// `nxlink -s` running on the host PC during development.
// ----------------------------------------------------------------------------
namespace
{
int RedirectToNxlink()
{
  socketInitializeDefault();
  int fd = nxlinkStdio();
  if (fd < 0)
    socketExit();
  return fd;
}

void CloseNxlink(int fd)
{
  if (fd >= 0)
  {
    close(fd);
    socketExit();
  }
}

constexpr int kSwitchScreenW = 1280;
constexpr int kSwitchScreenH = 720;
constexpr const char* kRomDir = "sdmc:/roms/";

struct RomEntry
{
  std::string path;
  std::string name;
  std::uintmax_t size_bytes = 0;
};

const char* CoreStateName(Core::State state)
{
  switch (state)
  {
  case Core::State::Uninitialized:
    return "Uninitialized";
  case Core::State::Paused:
    return "Paused";
  case Core::State::Running:
    return "Running";
  case Core::State::Starting:
    return "Starting";
  case Core::State::Stopping:
    return "Stopping";
  }
  return "?";
}

dbg::Level ToDbgLevel(Common::Log::LogLevel level)
{
  switch (level)
  {
  case Common::Log::LogLevel::LERROR:
    return dbg::Level::Error;
  case Common::Log::LogLevel::LWARNING:
    return dbg::Level::Warn;
  case Common::Log::LogLevel::LDEBUG:
    return dbg::Level::Debug;
  case Common::Log::LogLevel::LNOTICE:
  case Common::Log::LogLevel::LINFO:
    return dbg::Level::Info;
  }
  return dbg::Level::Info;
}

class DolphinLogBridge final : public Common::Log::LogListener
{
public:
  void Log(Common::Log::LogLevel level, const char* msg) override
  {
    std::string line = msg ? msg : "";
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
      line.pop_back();

    dbg::LogF(ToDbgLevel(level), "dolphin-log", 0, "%s", line.c_str());
  }
};

void EnableDolphinDiagnosticLogs()
{
  auto* log_manager = Common::Log::LogManager::GetInstance();
  if (!log_manager)
  {
    DBG_WARN("Dolphin LogManager is not initialized; core log bridge disabled.");
    return;
  }

  log_manager->RegisterListener(Common::Log::LogListener::LOG_WINDOW_LISTENER,
                                std::make_unique<DolphinLogBridge>());
  log_manager->EnableListener(Common::Log::LogListener::LOG_WINDOW_LISTENER, true);
  log_manager->SetConfigLogLevel(Common::Log::LogLevel::LINFO);

  constexpr Common::Log::LogType kDiagnosticTypes[] = {
      Common::Log::LogType::AUDIO,
      Common::Log::LogType::BOOT,
      Common::Log::LogType::COMMON,
      Common::Log::LogType::CONSOLE,
      Common::Log::LogType::CONTROLLERINTERFACE,
      Common::Log::LogType::CORE,
      Common::Log::LogType::DISCIO,
      Common::Log::LogType::DSPHLE,
      Common::Log::LogType::DVDINTERFACE,
      Common::Log::LogType::IOS,
      Common::Log::LogType::IOS_DI,
      Common::Log::LogType::IOS_ES,
      Common::Log::LogType::IOS_FS,
      Common::Log::LogType::MEMMAP,
      Common::Log::LogType::OSREPORT,
      Common::Log::LogType::OSREPORT_HLE,
      Common::Log::LogType::POWERPC,
      Common::Log::LogType::VIDEO,
      Common::Log::LogType::VIDEOINTERFACE,
  };
  for (const Common::Log::LogType type : kDiagnosticTypes)
    log_manager->SetEnable(type, true);

  DBG_INFO("Dolphin diagnostic log bridge enabled (%zu log types, verbosity=LINFO).",
           sizeof(kDiagnosticTypes) / sizeof(kDiagnosticTypes[0]));
}

void LogMemorySnapshot(std::string_view label)
{
  u64 total_mem = 0;
  u64 used_mem = 0;
  const Result total_rc = svcGetInfo(&total_mem, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  const Result used_rc = svcGetInfo(&used_mem, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

  if (R_SUCCEEDED(total_rc) && R_SUCCEEDED(used_rc))
  {
    const u64 free_mem = total_mem > used_mem ? total_mem - used_mem : 0;
    DBG_INFO("Memory[%.*s]: total=%llu MiB used=%llu MiB free=%llu MiB",
             static_cast<int>(label.size()), label.data(),
             static_cast<unsigned long long>(total_mem >> 20),
             static_cast<unsigned long long>(used_mem >> 20),
             static_cast<unsigned long long>(free_mem >> 20));
    return;
  }

  DBG_WARN("Memory[%.*s]: svcGetInfo failed total_rc=0x%08X used_rc=0x%08X",
           static_cast<int>(label.size()), label.data(), total_rc, used_rc);
}

void LogPathProbe(const char* label, const std::string& path)
{
  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec);
  const std::string exists_error = ec ? ec.message() : "ok";

  bool is_dir = false;
  bool is_file = false;
  std::uintmax_t size = 0;
  if (exists)
  {
    ec.clear();
    is_dir = std::filesystem::is_directory(path, ec);
    ec.clear();
    is_file = std::filesystem::is_regular_file(path, ec);
    if (is_file)
    {
      ec.clear();
      size = std::filesystem::file_size(path, ec);
    }
  }

  DBG_INFO("Path[%s]: '%s' exists=%d dir=%d file=%d size=%llu exists_ec=%s",
           label, path.c_str(), exists ? 1 : 0, is_dir ? 1 : 0, is_file ? 1 : 0,
           static_cast<unsigned long long>(size), exists_error.c_str());
}

void LogDolphinPathSummary()
{
  DBG_INFO("Dolphin paths: user=%s sys=%s config=%s cache=%s logs=%s mainlog=%s",
           File::GetUserPath(D_USER_IDX).c_str(), File::GetSysDirectory().c_str(),
           File::GetUserPath(D_CONFIG_IDX).c_str(), File::GetUserPath(D_CACHE_IDX).c_str(),
           File::GetUserPath(D_LOGS_IDX).c_str(), File::GetUserPath(F_MAINLOG_IDX).c_str());
  DBG_INFO("Dolphin paths: wiiroot=%s session_wiiroot=%s gcuser=%s gamesettings=%s",
           File::GetUserPath(D_WIIROOT_IDX).c_str(),
           File::GetUserPath(D_SESSION_WIIROOT_IDX).c_str(),
           File::GetUserPath(D_GCUSER_IDX).c_str(),
           File::GetUserPath(D_GAMESETTINGS_IDX).c_str());

  LogPathProbe("user-root", File::GetUserPath(D_USER_IDX));
  LogPathProbe("sys-root", File::GetSysDirectory());
  LogPathProbe("sys-gamesettings", File::GetSysDirectory() + "GameSettings");
  LogPathProbe("rom-dir", kRomDir);
}

void LogSdlInputProbe()
{
  const int joystick_count = SDL_NumJoysticks();
  int controller_count = 0;
  DBG_INFO("SDL input probe: joysticks=%d", joystick_count);
  for (int i = 0; i < joystick_count; ++i)
  {
    const SDL_bool is_controller = SDL_IsGameController(i);
    if (is_controller)
      ++controller_count;
    DBG_INFO("SDL input[%d]: gamecontroller=%d name=%s", i, is_controller ? 1 : 0,
             SDL_GameControllerNameForIndex(i) ? SDL_GameControllerNameForIndex(i) : "(null)");
  }
  DBG_INFO("SDL input probe: gamecontrollers=%d", controller_count);
}

void LogGlError(const char* label)
{
  bool saw_error = false;
  for (int i = 0; i < 8; ++i)
  {
    const GLenum err = glGetError();
    if (err == GL_NO_ERROR)
      break;
    saw_error = true;
    DBG_WARN("GL error after %s: 0x%04X", label, err);
  }

  if (!saw_error)
    DBG_DEBUG("GL error check after %s: clean", label);
}

const char* BootParameterTypeName(const BootParameters& boot)
{
  if (std::holds_alternative<BootParameters::Disc>(boot.parameters))
    return "Disc";
  if (std::holds_alternative<BootParameters::Executable>(boot.parameters))
    return "Executable";
  if (std::holds_alternative<DiscIO::VolumeWAD>(boot.parameters))
    return "WAD";
  if (std::holds_alternative<BootParameters::NANDTitle>(boot.parameters))
    return "NANDTitle";
  if (std::holds_alternative<BootParameters::IPL>(boot.parameters))
    return "IPL";
  if (std::holds_alternative<BootParameters::DFF>(boot.parameters))
    return "DFF";
  return "?";
}

void LogBootParameters(const BootParameters& boot)
{
  DBG_INFO("BootParameters: type=%s riivolution_patches=%zu savestate=%s netplay=%d",
           BootParameterTypeName(boot), boot.riivolution_patches.size(),
           boot.boot_session_data.GetSavestatePath()
               ? boot.boot_session_data.GetSavestatePath()->c_str()
               : "(none)",
           boot.boot_session_data.GetNetplaySettings() ? 1 : 0);

  std::visit(
      [](const auto& params) {
        using T = std::decay_t<decltype(params)>;
        if constexpr (std::is_same_v<T, BootParameters::Disc>)
        {
          DBG_INFO("BootParameters::Disc path=%s auto_disc_change_paths=%zu volume=%p",
                   params.path.c_str(), params.auto_disc_change_paths.size(), params.volume.get());
        }
        else if constexpr (std::is_same_v<T, BootParameters::Executable>)
        {
          DBG_INFO("BootParameters::Executable path=%s reader=%p", params.path.c_str(),
                   params.reader.get());
        }
        else if constexpr (std::is_same_v<T, DiscIO::VolumeWAD>)
        {
          DBG_INFO("BootParameters::WAD");
        }
        else if constexpr (std::is_same_v<T, BootParameters::NANDTitle>)
        {
          DBG_INFO("BootParameters::NANDTitle id=0x%016llX",
                   static_cast<unsigned long long>(params.id));
        }
        else if constexpr (std::is_same_v<T, BootParameters::IPL>)
        {
          DBG_INFO("BootParameters::IPL path=%s has_disc=%d region=%d", params.path.c_str(),
                   params.disc ? 1 : 0, static_cast<int>(params.region));
        }
        else if constexpr (std::is_same_v<T, BootParameters::DFF>)
        {
          DBG_INFO("BootParameters::DFF path=%s", params.dff_path.c_str());
        }
      },
      boot.parameters);
}

void LogDolphinConfigSummary(const char* label)
{
  const SConfig& startup = SConfig::GetInstance();
  DBG_INFO("Config[%s]: gfx=%s cpu_core=%d cpu_thread=%d fastmem=%d arena=%d skip_ipl=%d",
           label, Config::Get(Config::MAIN_GFX_BACKEND).c_str(),
           static_cast<int>(Config::Get(Config::MAIN_CPU_CORE)),
           Config::Get(Config::MAIN_CPU_THREAD) ? 1 : 0,
           Config::Get(Config::MAIN_FASTMEM) ? 1 : 0,
           Config::Get(Config::MAIN_FASTMEM_ARENA) ? 1 : 0,
           Config::Get(Config::MAIN_SKIP_IPL) ? 1 : 0);
  DBG_INFO("Config[%s]: dsp_hle=%d dsp_thread=%d audio=%s muted=%d region=%d game_id=%s title=%s",
           label, Config::Get(Config::MAIN_DSP_HLE) ? 1 : 0,
           Config::Get(Config::MAIN_DSP_THREAD) ? 1 : 0,
           Config::Get(Config::MAIN_AUDIO_BACKEND).c_str(),
           Config::Get(Config::MAIN_AUDIO_MUTED) ? 1 : 0, static_cast<int>(startup.m_region),
           startup.GetGameID().c_str(), startup.GetTitleDescription().c_str());
}

bool IsRomExt(const std::filesystem::path& p)
{
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext == ".iso" || ext == ".gcm" || ext == ".ciso" || ext == ".gcz" ||
         ext == ".rvz" || ext == ".wbfs" || ext == ".wad" || ext == ".dol" ||
         ext == ".elf";
}

std::string FormatSize(std::uintmax_t bytes);

std::vector<RomEntry> ScanRoms()
{
  std::vector<RomEntry> roms;
  std::error_code ec;
  if (!std::filesystem::exists(kRomDir, ec))
  {
    DBG_INFO("ROM dir %s does not exist; creating it.", kRomDir);
    std::filesystem::create_directories(kRomDir, ec);
    if (ec)
      DBG_WARN("create_directories(%s) failed: %s", kRomDir, ec.message().c_str());
    return roms;
  }

  for (const auto& entry : std::filesystem::directory_iterator(kRomDir, ec))
  {
    if (ec)
    {
      DBG_WARN("directory_iterator on %s aborted: %s", kRomDir, ec.message().c_str());
      break;
    }
    if (!entry.is_regular_file(ec))
      continue;
    if (!IsRomExt(entry.path()))
      continue;
    RomEntry r;
    r.path = entry.path().string();
    r.name = entry.path().filename().string();
    r.size_bytes = entry.file_size(ec);
    roms.push_back(std::move(r));
  }
  std::sort(roms.begin(), roms.end(),
            [](const RomEntry& a, const RomEntry& b) { return a.name < b.name; });
  DBG_INFO("ScanRoms: %zu entries in %s", roms.size(), kRomDir);
  for (std::size_t i = 0; i < roms.size() && i < 64; ++i)
  {
    DBG_INFO("ROM[%zu]: %s size=%s path=%s", i, roms[i].name.c_str(),
             FormatSize(roms[i].size_bytes).c_str(), roms[i].path.c_str());
  }
  if (roms.size() > 64)
    DBG_WARN("ScanRoms: only first 64 ROMs logged; total=%zu", roms.size());
  return roms;
}

std::string FormatSize(std::uintmax_t bytes)
{
  static constexpr const char* kUnits[] = {"B", "KiB", "MiB", "GiB"};
  double v = static_cast<double>(bytes);
  int u = 0;
  while (v >= 1024.0 && u < 3)
  {
    v /= 1024.0;
    ++u;
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f %s", v, kUnits[u]);
  return buf;
}

std::string ResolveBootPath(const std::vector<RomEntry>& roms, int selected,
                            const std::string& fallback)
{
  if (selected >= 0 && selected < static_cast<int>(roms.size()))
    return roms[selected].path;

  std::error_code ec;
  if (!fallback.empty() && std::filesystem::exists(fallback, ec))
    return fallback;

  if (!roms.empty())
    return roms.front().path;

  return {};
}

// Returns true if SDL is up and the GL context is current.
bool InitGraphics(SDL_Window** out_window, SDL_GLContext* out_ctx)
{
  DBG_INFO("InitGraphics: SDL_Init(VIDEO|EVENTS|GAMECONTROLLER)");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0)
  {
    DBG_ERROR("SDL_Init failed: %s", SDL_GetError());
    return false;
  }

  // GLES 3.2 — best Mesa-on-Switch profile (Mesa 20.1 reports up to 3.2).
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  DBG_DEBUG("Requested GLES 3.2 + double-buffer");

  SDL_Window* window =
      SDL_CreateWindow("Dolphin (Switch)", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                       kSwitchScreenW, kSwitchScreenH,
                       SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
  if (!window)
  {
    DBG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return false;
  }
  DBG_INFO("SDL_CreateWindow: %dx%d fullscreen", kSwitchScreenW, kSwitchScreenH);

  SDL_GLContext ctx = SDL_GL_CreateContext(window);
  if (!ctx)
  {
    DBG_ERROR("SDL_GL_CreateContext failed: %s", SDL_GetError());
    SDL_DestroyWindow(window);
    SDL_Quit();
    return false;
  }
  SDL_GL_MakeCurrent(window, ctx);
  SDL_GL_SetSwapInterval(1);
  DBG_INFO("GL context created and made current; swap interval=1");

  *out_window = window;
  *out_ctx = ctx;
  return true;
}

void ShutdownGraphics(SDL_Window* window, SDL_GLContext ctx)
{
  if (ctx)
    SDL_GL_DeleteContext(ctx);
  if (window)
    SDL_DestroyWindow(window);
  SDL_Quit();
}
}  // namespace

int main(int /*argc*/, char** /*argv*/)
{
  // Early-boot console + nxlink so we have output before the GL window is up.
  consoleInit(nullptr);
  const int nxlink_fd = RedirectToNxlink();

  // dbg::Init opens the SD log file — must come after sdmc:/ is mounted,
  // which libnx does for us at process start.
  dbg::Init();

  DBG_INFO("dolphin-switch boot — version %s", Common::GetScmRevStr().c_str());
  DBG_INFO("nxlink fd: %d (negative = no host listening)", nxlink_fd);
  LogMemorySnapshot("early boot");

  bool romfs_mounted = false;
  Result romfs_rc = romfsInit();
  if (R_SUCCEEDED(romfs_rc))
  {
    romfs_mounted = true;
    DBG_INFO("romfsInit: PASS; Dolphin Sys directory will use romfs:/Sys/");
  }
  else
  {
    DBG_WARN("romfsInit failed: rc=0x%08X; falling back to sdmc Sys directory.", romfs_rc);
  }

  // Dolphin's user-data root must be set BEFORE UICommon::Init runs —
  // otherwise the IOS HLE filesystem asserts on an empty m_root_path
  // (Core/IOS/FS/HostBackend/FS.cpp:46 BuildFilename). On Switch the
  // canonical layout per CLAUDE.md is:
  //   sdmc:/switch/dolphin/      — writable state (config, NAND, saves)
  //   romfs:/Sys/                — read-only system files (per-game compat DB)
  //   sdmc:/roms/                — ROM scan dir
  constexpr const char* kUserDir = "sdmc:/switch/dolphin/";
  File::CreateFullPath(kUserDir);
  File::SetUserPath(D_USER_IDX, kUserDir);
  File::SetUserPath(D_SESSION_WIIROOT_IDX, File::GetUserPath(D_WIIROOT_IDX));

  const unsigned int required_user_dirs[] = {
      D_CONFIG_IDX,
      D_CACHE_IDX,
      D_GCUSER_IDX,
      D_WIIROOT_IDX,
      D_SESSION_WIIROOT_IDX,
      D_DUMP_IDX,
      D_LOAD_IDX,
      D_LOGS_IDX,
      D_STATESAVES_IDX,
      D_SCREENSHOTS_IDX,
      D_THEMES_IDX,
      D_GAMESETTINGS_IDX,
  };
  for (const unsigned int dir : required_user_dirs)
    File::CreateFullPath(File::GetUserPath(dir));
  DBG_INFO("User paths set under %s", kUserDir);

  if (romfs_mounted)
  {
    File::SetSysDirectory("romfs:/Sys");
  }
  else
  {
    const std::string fallback_sys_dir = std::string(kUserDir) + "Sys";
    File::CreateFullPath(fallback_sys_dir);
    File::SetSysDirectory(fallback_sys_dir);
  }
  DBG_INFO("Dolphin Sys directory: %s", File::GetSysDirectory().c_str());
  LogDolphinPathSummary();

  DBG_INFO("Calling UICommon::Init()...");
  UICommon::Init();
  DBG_INFO("UICommon::Init() returned.");
  EnableDolphinDiagnosticLogs();
  LogDolphinConfigSummary("after UICommon::Init");

  // Tear down the early console before SDL grabs the framebuffer.
  consoleExit(nullptr);

  SDL_Window* window = nullptr;
  SDL_GLContext gl_ctx = nullptr;
  if (!InitGraphics(&window, &gl_ctx))
  {
    DBG_ERROR("Graphics init failed; aborting boot.");
    UICommon::Shutdown();
    if (romfs_mounted)
      romfsExit();
    dbg::Shutdown();
    CloseNxlink(nxlink_fd);
    return 1;
  }
  LogSdlInputProbe();

  // System probe AFTER GL is up — GL strings only valid once a context is
  // current. This is the single most important diagnostic for hardware
  // bring-up: shows JIT cap, memory budget, GL_VENDOR/RENDERER/VERSION,
  // applet state — everything we need to triage a Switch boot.
  dbg::DumpSystemInfo();
  LogGlError("runtime probe");

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
  // ImGui's GLES shader template only triggers on glsl_version == 300, even
  // though Mesa here advertises GLSL ES 3.20. Pass 300 es so ImGui picks the
  // *_glsl_300_es shader (which prepends `precision mediump float;`).
  ImGui_ImplOpenGL3_Init("#version 300 es");
  DBG_INFO("ImGui SDL2+GLES backends initialized.");
  LogGlError("ImGui backend init");

  // ---------------------------------------------------------------------
  // Direct ROM boot (M2-stage hardware bring-up).
  //
  // The ImGui browser can now pass a selected ROM into BootManager::BootCore.
  // This is expected to surface the next blockers in the emulator core rather
  // than frontend wiring.
  //
  // WindowSystemInfo type=Headless picks Dolphin's Null video backend,
  // which is what we want until M3 wires GLContextSwitch into the OGL
  // backend. Audio/input come up via the existing Switch arms (audren
  // is M4, hid mapping is M4 — both are no-op on Switch right now).
  // ---------------------------------------------------------------------
  WindowSystemInfo wsi{};
  wsi.type = WindowSystemType::Headless;
  wsi.render_window = nullptr;
  wsi.render_surface = nullptr;

  UICommon::InitControllers(wsi);
  DBG_INFO("UICommon::InitControllers returned.");

  // Force the Null video backend — there is no working OGL backend wired to
  // our SDL2/EGL surface yet (M3). Null lets the CPU/JIT path run without a
  // window backend; we render our own ImGui-only output.
  Config::SetBase(Config::MAIN_GFX_BACKEND, std::string{"Null"});
  DBG_INFO("Forced video backend = Null.");

  // Disable fastmem on Switch — Dolphin's InitFastmemArena reserves a 14
  // GiB VA window which the Switch process budget (3.2 GiB) cannot cover.
  // The slower memory-check path still emulates correctly, just without
  // the JIT pointer-math fast path. M2 hardware perf work revisits this.
  Config::SetBase(Config::MAIN_FASTMEM, false);
  Config::SetBase(Config::MAIN_FASTMEM_ARENA, false);
  DBG_INFO("Forced fastmem = OFF.");

  // Force Interpreter CPU core. JITARM64 default would emit ARM64 code into
  // the rw_addr alias and branch the dispatcher to the SAME pointer, but on
  // Horizon OS rw and rx are distinct VA aliases — branching to rw is a
  // kernel panic (fatal 2345-0008). The rw→rx dispatcher translation is M2
  // load-bearing work per docs/jit-memory.md §swap-strategy step 3 and is
  // not implemented yet. Interpreter sidesteps the JIT entirely.
  Config::SetBase(Config::MAIN_CPU_CORE, PowerPC::CPUCore::Interpreter);
  DBG_INFO("Forced CPU core = Interpreter (JIT rw→rx alias not done; M2).");

  // DSP HLE on (default true, but make it explicit). DSP_LLE drags in the
  // x86-only DSP JIT — falls back to DSPEmitterNull on ARM64 but at HLE
  // throughput we don't care.
  Config::SetBase(Config::MAIN_DSP_HLE, true);
  Config::SetBase(Config::MAIN_DSP_THREAD, false);
  DBG_INFO("Forced DSP HLE = ON, DSP_THREAD = OFF.");

  // Mute audio + force NullSound — audren backend is M4 work; until then
  // produce no sound rather than risk Cubeb fallback paths on Switch.
  Config::SetBase(Config::MAIN_AUDIO_BACKEND, std::string{BACKEND_NULLSOUND});
  Config::SetBase(Config::MAIN_AUDIO_MUTED, true);
  DBG_INFO("Forced audio backend = NullSound, muted.");
  LogDolphinConfigSummary("forced Switch base config");

  const std::string default_rom_path = "sdmc:/roms/GameCube-240pSuite-1.20.iso";
  LogPathProbe("default-rom-fallback", default_rom_path);
  bool core_booted = false;

  // Hook Core state transitions so we can see the boot progress.
  // EventHook is [[nodiscard]] — keep alive for the whole frontend lifetime.
  static auto state_hook = Core::AddOnStateChangedCallback([](Core::State state) {
    DBG_INFO("Core state -> %s", CoreStateName(state));
    LogMemorySnapshot("Core state change");
  });

  std::vector<RomEntry> roms = ScanRoms();
  int selected = roms.empty() ? -1 : 0;
  std::string status_line = roms.empty() ? std::string{"No ROMs found in sdmc:/roms/"}
                                         : std::string{"Select a ROM to boot."};

  auto try_boot_rom = [&](const std::string& boot_path) {
    if (core_booted)
    {
      DBG_WARN("Boot request ignored because core is already booted; state=%s",
               CoreStateName(Core::GetState(Core::System::GetInstance())));
      return;
    }
    if (boot_path.empty())
    {
      DBG_WARN("No ROM path selected; skipping boot.");
      status_line = "No ROM selected and no fallback ROM exists.";
      return;
    }
    LogPathProbe("boot-rom", boot_path);
    std::error_code ec;
    if (!std::filesystem::exists(boot_path, ec))
    {
      DBG_WARN("ROM not found at %s; skipping boot.", boot_path.c_str());
      status_line = "ROM not found: " + boot_path;
      return;
    }
    LogMemorySnapshot("before GenerateFromFile");
    DBG_INFO("BootParameters::GenerateFromFile(%s)", boot_path.c_str());
    status_line = "Booting: " + boot_path;
    const auto generate_start = std::chrono::steady_clock::now();
    auto boot_params = BootParameters::GenerateFromFile(boot_path);
    const auto generate_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - generate_start)
                                 .count();
    if (!boot_params)
    {
      DBG_ERROR("GenerateFromFile returned null after %lld ms — bad ROM or unsupported format.",
                static_cast<long long>(generate_ms));
      status_line = "BootParameters::GenerateFromFile failed: " + boot_path;
      return;
    }
    DBG_INFO("GenerateFromFile returned %s in %lld ms.", BootParameterTypeName(*boot_params),
             static_cast<long long>(generate_ms));
    LogBootParameters(*boot_params);
    LogDolphinConfigSummary("before BootCore");
    LogMemorySnapshot("before BootCore");

    DBG_INFO("BootManager::BootCore starting...");
    const auto boot_start = std::chrono::steady_clock::now();
    core_booted = BootManager::BootCore(Core::System::GetInstance(),
                                        std::move(boot_params), wsi);
    const auto boot_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - boot_start)
                             .count();
    LogMemorySnapshot("after BootCore");
    LogDolphinConfigSummary("after BootCore");
    if (core_booted)
    {
      DBG_INFO("BootCore returned true after %lld ms (Core thread launched).",
               static_cast<long long>(boot_ms));
      status_line = "BootCore launched: " + boot_path;
    }
    else
    {
      DBG_ERROR("BootCore returned false after %lld ms — boot rejected.",
                static_cast<long long>(boot_ms));
      status_line = "BootCore returned false: " + boot_path;
    }
  };

  bool running = true;
  std::uint64_t frame_counter = 0;
  auto last_heartbeat = std::chrono::steady_clock::now();
  while (running && appletMainLoop())
  {
    ++frame_counter;
    const auto now = std::chrono::steady_clock::now();
    if (now - last_heartbeat >= std::chrono::seconds(5))
    {
      last_heartbeat = now;
      DBG_DEBUG("Heartbeat: frame=%llu core_booted=%d core_state=%s roms=%zu selected=%d",
                static_cast<unsigned long long>(frame_counter), core_booted ? 1 : 0,
                CoreStateName(Core::GetState(Core::System::GetInstance())), roms.size(),
                selected);
      LogMemorySnapshot("heartbeat");
    }

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT)
        running = false;
      if (event.type == SDL_CONTROLLERBUTTONDOWN)
      {
        // Switch button mapping (SDL2):
        //   A → SDL_CONTROLLER_BUTTON_A           (boot ROM)
        //   + → SDL_CONTROLLER_BUTTON_START       (exit)
        if (event.cbutton.button == SDL_CONTROLLER_BUTTON_START)
        {
          DBG_INFO("Plus pressed — exiting main loop.");
          running = false;
        }
        else if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
        {
          std::string boot_path = ResolveBootPath(roms, selected, default_rom_path);
          DBG_INFO("A pressed — triggering ROM boot: %s",
                   boot_path.empty() ? "(none)" : boot_path.c_str());
          try_boot_rom(boot_path);
        }
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(720, 600), ImGuiCond_Once);
    ImGui::Begin("Dolphin (Switch) — ROM browser");
    ImGui::Text("dolphin version: %s", Common::GetScmRevStr().c_str());
    ImGui::Text("Renderer: SDL2 + Mesa GLES (libnx EGL)");
    ImGui::Text("ROM dir:  %s   (%zu entries)", kRomDir, roms.size());
    if (core_booted)
      ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Core: BOOTED");
    else
      ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f),
                         "Core: not booted — press A or Boot selected, + to exit.");
    ImGui::Separator();

    if (ImGui::Button("Rescan"))
    {
      roms = ScanRoms();
      selected = roms.empty() ? -1 : 0;
      status_line = roms.empty() ? std::string{"No ROMs found in sdmc:/roms/"}
                                 : std::string{"Select a ROM to boot."};
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Press + on controller to exit.");

    ImGui::BeginChild("rom_list", ImVec2(0, 380), true);
    for (int i = 0; i < static_cast<int>(roms.size()); ++i)
    {
      const auto& r = roms[i];
      const std::string label = r.name + "    [" + FormatSize(r.size_bytes) + "]";
      if (ImGui::Selectable(label.c_str(), selected == i,
                            ImGuiSelectableFlags_AllowDoubleClick))
      {
        selected = i;
        if (ImGui::IsMouseDoubleClicked(0))
        {
          try_boot_rom(r.path);
        }
      }
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (selected >= 0 && selected < static_cast<int>(roms.size()))
    {
      ImGui::Text("Selected: %s", roms[selected].name.c_str());
      if (ImGui::Button("Boot selected"))
      {
        try_boot_rom(roms[selected].path);
      }
    }
    ImGui::TextWrapped("%s", status_line.c_str());
    ImGui::End();

    // ImGui log viewer — visible on Switch screen without PC connection.
    // Auto-scrolls to tail when user is already at the bottom; level-tagged
    // lines are color-coded.
    {
      ImGui::SetNextWindowPos(ImVec2(40, 460), ImGuiCond_Once);
      ImGui::SetNextWindowSize(ImVec2(1200, 240), ImGuiCond_Once);
      ImGui::Begin("Debug log");
      static bool auto_scroll = true;
      ImGui::Checkbox("Auto-scroll", &auto_scroll);
      ImGui::SameLine();
      ImGui::TextDisabled("Sinks: nxlink stdio + sdmc:/switch/dolphin/logs/*.log");
      ImGui::Separator();
      ImGui::BeginChild("log_scroll", ImVec2(0, 0), false,
                        ImGuiWindowFlags_HorizontalScrollbar);
      const auto ring = dbg::RingBufferSnapshot();
      for (const auto& l : ring)
      {
        ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
        if (l.find("][ERROR]") != std::string::npos)
          color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
        else if (l.find("][WARN ]") != std::string::npos)
          color = ImVec4(1.0f, 0.85f, 0.4f, 1.0f);
        else if (l.find("][DEBUG]") != std::string::npos ||
                 l.find("][TRACE]") != std::string::npos)
          color = ImVec4(0.55f, 0.7f, 1.0f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextUnformatted(l.c_str());
        ImGui::PopStyleColor();
      }
      if (auto_scroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
      ImGui::EndChild();
      ImGui::End();
    }

    ImGui::Render();
    glViewport(0, 0,
               static_cast<GLsizei>(io.DisplaySize.x),
               static_cast<GLsizei>(io.DisplaySize.y));
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
  }

  DBG_INFO("Main loop exited; shutting down.");
  if (core_booted)
  {
    DBG_INFO("Core::Stop()...");
    Core::Stop(Core::System::GetInstance());
    DBG_INFO("Core::Shutdown()...");
    Core::Shutdown(Core::System::GetInstance());
  }
  UICommon::ShutdownControllers();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  ShutdownGraphics(window, gl_ctx);

  UICommon::Shutdown();
  if (romfs_mounted)
    romfsExit();
  DBG_INFO("UICommon::Shutdown() returned. Goodbye.");
  dbg::Shutdown();
  CloseNxlink(nxlink_fd);
  return 0;
}
