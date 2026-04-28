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
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <unistd.h>

#include <switch.h>

#include <SDL2/SDL.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "UICommon/UICommon.h"
#include "Common/Version.h"
#include "Core/Host.h"

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

bool IsRomExt(const std::filesystem::path& p)
{
  std::string ext = p.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return ext == ".iso" || ext == ".gcm" || ext == ".ciso" || ext == ".gcz" ||
         ext == ".rvz" || ext == ".wbfs" || ext == ".wad" || ext == ".dol" ||
         ext == ".elf";
}

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
  DBG_INFO("Calling UICommon::Init()...");
  UICommon::Init();
  DBG_INFO("UICommon::Init() returned.");

  // Tear down the early console before SDL grabs the framebuffer.
  consoleExit(nullptr);

  SDL_Window* window = nullptr;
  SDL_GLContext gl_ctx = nullptr;
  if (!InitGraphics(&window, &gl_ctx))
  {
    DBG_ERROR("Graphics init failed; aborting boot.");
    UICommon::Shutdown();
    dbg::Shutdown();
    CloseNxlink(nxlink_fd);
    return 1;
  }

  // System probe AFTER GL is up — GL strings only valid once a context is
  // current. This is the single most important diagnostic for hardware
  // bring-up: shows JIT cap, memory budget, GL_VENDOR/RENDERER/VERSION,
  // applet state — everything we need to triage a Switch boot.
  dbg::DumpSystemInfo();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
  ImGui_ImplOpenGL3_Init("#version 320 es");
  DBG_INFO("ImGui SDL2+GLES backends initialized.");

  bool running = true;
  while (running && appletMainLoop())
  {
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT)
        running = false;
      // Switch's "+" button maps to SDL_CONTROLLER_BUTTON_START.
      if (event.type == SDL_CONTROLLERBUTTONDOWN &&
          event.cbutton.button == SDL_CONTROLLER_BUTTON_START)
      {
        running = false;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // ROM browser — M5 wires the actual Core::Init/Run on selection.
    static std::vector<RomEntry> roms = ScanRoms();
    static int selected = -1;
    static std::string status_line = roms.empty()
                                         ? std::string{"No ROMs found in sdmc:/roms/"}
                                         : std::string{"Select a ROM to boot."};

    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(720, 600), ImGuiCond_Once);
    ImGui::Begin("Dolphin (Switch) — ROM browser");
    ImGui::Text("dolphin version: %s", Common::GetScmRevStr().c_str());
    ImGui::Text("Renderer: SDL2 + Mesa GLES (libnx EGL)");
    ImGui::Text("ROM dir:  %s   (%zu entries)", kRomDir, roms.size());
    ImGui::Separator();

    if (ImGui::Button("Rescan"))
    {
      roms = ScanRoms();
      selected = -1;
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
          status_line = "Would boot: " + r.path +
                        " (Core::Init wiring lands in M5 hardware integration)";
        }
      }
    }
    ImGui::EndChild();

    ImGui::Separator();
    if (selected >= 0 && selected < static_cast<int>(roms.size()))
    {
      ImGui::Text("Selected: %s", roms[selected].name.c_str());
      if (ImGui::Button("Boot selected (stub)"))
      {
        status_line = "Would boot: " + roms[selected].path +
                      " (Core::Init wiring lands in M5 hardware integration)";
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
      const auto& ring = dbg::RingBuffer();
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
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  ShutdownGraphics(window, gl_ctx);

  UICommon::Shutdown();
  DBG_INFO("UICommon::Shutdown() returned. Goodbye.");
  dbg::Shutdown();
  CloseNxlink(nxlink_fd);
  return 0;
}
