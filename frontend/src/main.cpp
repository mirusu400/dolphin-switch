// M0 hello-world + M1 link-validation entry point.
//
// Boots into a console, prints over nxlink (so `nxlink -s` on the host receives
// the output), exercises a couple of dolphin entry points so the linker keeps
// the dolphin static libs in the NRO, waits for the + button, exits cleanly.
//
// This file will be replaced with the ImGui-based frontend in M5.

#include <cstdio>
#include <unistd.h>

#include <switch.h>

#include "UICommon/UICommon.h"
#include "Common/Version.h"
#include "Core/Host.h"

// Newlib symbol-gap stubs live in switch_libc_shim.c — kept in a C
// translation unit to avoid the libgen.h / string.h conflicting-decl
// trap newlib triggers when basename gets asm-aliased to __gnu_basename.

// Host_* are the frontend->core callback contract. Stub them all for the M1
// link-validation build; M5 ImGui frontend will replace these with real bodies.
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

namespace {

// Redirect stdout/stderr to nxlink so `nxlink -s` shows our output.
// Returns the socket fd (or -1 if no host is listening); callers must close it.
int RedirectToNxlink() {
    socketInitializeDefault();
    int fd = nxlinkStdio();
    if (fd < 0) {
        // Fall back to on-screen console only.
        socketExit();
    }
    return fd;
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
    consoleInit(nullptr);

    const int nxlink_fd = RedirectToNxlink();

    std::printf("Hello Dolphin - M0 NRO is alive.\n");
    std::printf("  dolphin-emu version: %s\n", Common::GetScmRevStr().c_str());
    std::printf("  Calling UICommon::Init()...\n");
    std::fflush(stdout);
    UICommon::Init();
    std::printf("  UICommon::Init() returned.\n");
    std::printf("  Press + to exit (UICommon::Shutdown will run on exit).\n");
    std::fflush(stdout);

    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);

    while (appletMainLoop()) {
        padUpdate(&pad);
        const u64 down = padGetButtonsDown(&pad);
        if (down & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(nullptr);
    }

    UICommon::Shutdown();

    if (nxlink_fd >= 0) {
        close(nxlink_fd);
        socketExit();
    }
    consoleExit(nullptr);
    return 0;
}
