// SPDX-License-Identifier: GPL-2.0-or-later
// bbport_settings.cpp: the menu saves its keys into bbport.ini and keeps the launcher's (controls).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include "gpu/shim/bbport_settings.h"

#ifdef _WIN32
// MSYS2 builds: the CRT has no setenv.
static int setenv(const char* name, const char* value, int) { return _putenv_s(name, value); }
static int unsetenv(const char* name) { return _putenv_s(name, ""); }
#endif

static std::string Read(const char* path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

int main() {
    // Windows (MSYS2): the user's temp folder; elsewhere /tmp.
    const char* temp = std::getenv("TEMP");
    std::string pattern = std::string(temp && temp[0] ? temp : "/tmp") + "/bbport-settings-test-XXXXXX";
    char* path = pattern.data();
    const int fd = mkstemp(path);
    assert(fd >= 0);
    const char ini[] = "# launcher\nupscaler=fsr3\nkey.cross=X, Space\npad.circle=a\nshow_fps=0\n"
                       "fullscreen_hint=1\n";
    assert(write(fd, ini, sizeof(ini) - 1) == ssize_t(sizeof(ini) - 1));
    close(fd);
    setenv("BB_CONFIG", path, 1);

    BbSettings::Load();
    auto& s = BbSettings::Get();
    assert(s.upscaler == BbSettings::UpscalerFsr3 && !s.show_fps && s.menu_x < 0.0f);
    s.show_fps = true;
    s.upscaler = BbSettings::UpscalerFsr411;
    s.menu_x = 0.625f;
    s.menu_y = 0.125f;
    BbSettings::Save();

    const std::string saved = Read(path);
    // The launcher's controls, its other keys and comments stay; the menu's keys are replaced
    // in place, new ones appended.
    assert(saved.find("# launcher\nupscaler=fsr411\nkey.cross=X, Space\npad.circle=a\nshow_fps=1\n"
                      "fullscreen_hint=1\n") == 0);
    assert(saved.find("menu_pos=0.6250,0.1250\n") != std::string::npos);
    assert(saved.find("upscaler=fsr3") == std::string::npos);

    s.menu_x = -1.0f;
    s.menu_y = -1.0f;
    BbSettings::Load();
    assert(s.menu_x == 0.625f && s.menu_y == 0.125f && s.upscaler == BbSettings::UpscalerFsr411);
    unlink(path);
    std::puts("PASS: settings save keeps other keys, menu position");

    // Kaniel 2026-10-10: an FPS limit and the DLSS model in the game's own menu. The menu's
    // frame cap is kept in bbport.ini and wins over the launcher's (BB_FPS_LIMIT) once set there.
    std::string second = std::string(temp && temp[0] ? temp : "/tmp") + "/bbport-settings-test-XXXXXX";
    const int fd2 = mkstemp(second.data());
    assert(fd2 >= 0);
    const char ini2[] = "fps_limit=144\ndlss_model=K\n";
    assert(write(fd2, ini2, sizeof(ini2) - 1) == ssize_t(sizeof(ini2) - 1));
    close(fd2);
    setenv("BB_CONFIG", second.c_str(), 1);
    setenv("BB_FPS_LIMIT", "60", 1);
    BbSettings::Load();
    assert(s.fps_limit == 144 && "the menu's cap, not the launcher's");
    assert(s.dlss_model == 11 && "NVSDK_NGX_DLSS_Hint_Render_Preset_K");
    s.fps_limit = 240;
    s.dlss_model = 13;
    BbSettings::Save();
    const std::string saved2 = Read(second.c_str());
    assert(saved2.find("fps_limit=240\n") != std::string::npos);
    assert(saved2.find("dlss_model=M\n") != std::string::npos);
    unlink(second.c_str());
    // No fps_limit in the file: the launcher's.
    char third[512];
    std::snprintf(third, sizeof(third), "%s/bbport-settings-test-XXXXXX", temp && temp[0] ? temp : "/tmp");
    close(mkstemp(third));
    setenv("BB_CONFIG", third, 1);
    BbSettings::Load();
    assert(s.fps_limit == 60);
    unlink(third);
    unsetenv("BB_FPS_LIMIT");

    // The cap applies to the unlocked frame rate only: the game's 30 and 60 modes keep their
    // own fixed timestep (a cap below it would slow the game down).
    assert(BbSettings::EffectiveFrameLimit(144, true) == 144);
    assert(BbSettings::EffectiveFrameLimit(144, false) == 0);
    assert(BbSettings::EffectiveFrameLimit(0, true) == 0);
    // The menu's rows: a cap or model from elsewhere shows as the nearest choice below it.
    assert(BbSettings::FpsLimitIndex(0) == 0);
    assert(BbSettings::FpsLimitIndex(144) == 5);
    assert(BbSettings::FpsLimits[BbSettings::FpsLimitIndex(75)] == 60);
    assert(BbSettings::FpsLimits[BbSettings::FpsLimitIndex(1000)] == 240);
    assert(BbSettings::DlssModelIndex(0) == 0);
    assert(BbSettings::DlssModels[BbSettings::DlssModelIndex(11)] == 11);
    assert(BbSettings::DlssModelIndex(99) == 0 && "unknown: NVIDIA's default");
    // Online (co-op) play keeps the fixed 60 FPS mode (Kaniel 2026-10-13: a host above 60 made
    // the guests see everyone freeze and fly): the FPS limit rows are locked there.
    assert(BbSettings::FrameRateLocked("1"));
    assert(!BbSettings::FrameRateLocked("0"));
    assert(!BbSettings::FrameRateLocked(""));
    assert(!BbSettings::FrameRateLocked(nullptr));
    std::puts("PASS: FPS limit and DLSS model settings");

    // Kaniel 2026-10-10: window mode (windowed, borderless, exclusive) and HDR output in the game's
    // menus. The launcher's BB_FULLSCREEN counts until the file has a window mode.
    char fourth[512];
    std::snprintf(fourth, sizeof(fourth), "%s/bbport-settings-test-XXXXXX", temp && temp[0] ? temp : "/tmp");
    close(mkstemp(fourth));
    setenv("BB_CONFIG", fourth, 1);
    setenv("BB_FULLSCREEN", "1", 1);
    BbSettings::Load();
    assert(s.window_mode == BbSettings::Borderless && "the launcher's fullscreen is borderless");
    assert(!s.hdr && s.hdr_peak_nits == 1000 && s.hdr_paper_white == 200);
    assert(s.hdr_scene && "highlights from the 16-bit scene by default");
    s.window_mode = BbSettings::ExclusiveFullscreen;
    s.hdr = true;
    s.hdr_peak_nits = 1500;
    s.hdr_paper_white = 250;
    s.hdr_highlights = 0.7f;
    s.hdr_scene = false;
    BbSettings::Save();
    const std::string saved4 = Read(fourth);
    assert(saved4.find("window_mode=fullscreen\n") != std::string::npos);
    assert(saved4.find("hdr=1\n") != std::string::npos);
    assert(saved4.find("hdr_peak_nits=1500\n") != std::string::npos);
    assert(saved4.find("hdr_paper_white=250\n") != std::string::npos);
    assert(saved4.find("hdr_highlights=0.70\n") != std::string::npos);
    assert(saved4.find("hdr_scene=0\n") != std::string::npos);
    s.hdr = false;
    BbSettings::Load();
    assert(s.window_mode == BbSettings::ExclusiveFullscreen && "the file's mode wins over BB_FULLSCREEN");
    assert(s.hdr && s.hdr_peak_nits == 1500 && s.hdr_paper_white == 250 && s.hdr_highlights == 0.7f);
    assert(!s.hdr_scene);
    setenv("BB_FULLSCREEN", "0", 1);
    unlink(fourth);
    BbSettings::Load();
    assert(s.window_mode == BbSettings::Windowed);
    unsetenv("BB_FULLSCREEN");
    // The menu's rows show the nearest offered brightness.
    assert(BbSettings::HdrPeakNits[BbSettings::HdrPeakIndex(1000)] == 1000);
    assert(BbSettings::HdrPeakNits[BbSettings::HdrPeakIndex(1100)] == 1000);
    assert(BbSettings::HdrPeakNits[BbSettings::HdrPeakIndex(9000)] == 4000);
    assert(BbSettings::HdrPaperWhiteNits[BbSettings::HdrPaperWhiteIndex(203)] == 200);
    std::puts("PASS: window mode and HDR settings");

    // Kaniel 2026-10-11: the controls in the game's menu. The lines it does not keep in Values
    // (key.*, pad.*, mouse_*, button_icons: runtime_pad.c and the icon step read them) are read
    // from the file, and Save replaces them in place, appends new ones and removes cleared ones
    // (the default applies again).
    char fifth[512];
    std::snprintf(fifth, sizeof(fifth), "%s/bbport-settings-test-XXXXXX", temp && temp[0] ? temp : "/tmp");
    const int fd5 = mkstemp(fifth);
    assert(fd5 >= 0);
    const char ini5[] = "key.cross=X, Space\npad.circle=a\nmouse_sensitivity=1.50\nupscaler=fsr3\n";
    assert(write(fd5, ini5, sizeof(ini5) - 1) == ssize_t(sizeof(ini5) - 1));
    close(fd5);
    setenv("BB_CONFIG", fifth, 1);
    BbSettings::Load();
    assert(BbSettings::Line("key.cross") == std::optional<std::string>("X, Space"));
    assert(BbSettings::Line("mouse_sensitivity") == std::optional<std::string>("1.50"));
    assert(!BbSettings::Line("key.circle"));
    BbSettings::SetLine("key.cross", "Ctrl+F");
    BbSettings::SetLine("pad.circle", std::nullopt);
    BbSettings::SetLine("key.l1", "Mouse X1");
    assert(BbSettings::Line("key.cross") == std::optional<std::string>("Ctrl+F") && "read back before the save");
    assert(!BbSettings::Line("pad.circle"));
    BbSettings::Save();
    const std::string saved5 = Read(fifth);
    assert(saved5.find("key.cross=Ctrl+F\nmouse_sensitivity=1.50\nupscaler=") == 0);
    assert(saved5.find("pad.circle") == std::string::npos);
    assert(saved5.find("key.l1=Mouse X1\n") != std::string::npos);
    BbSettings::Load();
    assert(BbSettings::Line("key.l1") == std::optional<std::string>("Mouse X1") && !BbSettings::Line("pad.circle"));
    unlink(fifth);
    std::puts("PASS: control lines read, changed and removed");
}
