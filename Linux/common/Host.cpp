#include "Host.h"
#include "Core.h"
#include "Prefs.h"
#include <glib.h>
#include <gst/gst.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <glob.h>
#ifdef VIKEY_HAVE_X11
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#endif
#ifndef VIKEY_DATADIR
#define VIKEY_DATADIR "/usr/share/vikey"
#endif
namespace vikey { namespace host {

void init() { gst_init(nullptr, nullptr); }

std::string utf8(const std::u32string& w) { std::string o; for (char32_t c : w) { if (c < 0x80) o += (char)c; else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 0x3F)); } else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); } else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 0x3F)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); } } return o; }

// The switch sound (Tony picked "sparkle"): two tiny notes, rising into Vietnamese, falling into English. The same
// files as the Mac app (App/Resources/Sounds/*.caf → Linux/sounds/*.wav), played in-process with GStreamer so the
// sound follows the key with no process start; a system bell when GStreamer can't play them.
static GstElement* _player = nullptr;
void switchSound(int language) {
    std::string file = std::string(VIKEY_DATADIR) + "/sounds/" + (language == 1 ? "SwitchToVietnamese.wav" : "SwitchToEnglish.wav");
    if (!_player) _player = gst_element_factory_make("playbin", "vikey-sound");
    if (_player && g_file_test(file.c_str(), G_FILE_TEST_EXISTS)) {
        gst_element_set_state(_player, GST_STATE_NULL);
        gchar* uri = g_filename_to_uri(file.c_str(), nullptr, nullptr); g_object_set(_player, "uri", uri, nullptr); g_free(uri);
        if (gst_element_set_state(_player, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) return;
    }
    gchar* argv[] = {(gchar*)"canberra-gtk-play", (gchar*)"-i", (gchar*)"bell", nullptr};
    g_spawn_async(nullptr, argv, nullptr, (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL), nullptr, nullptr, nullptr, nullptr);
}

void launchSettings(const char* arg, const char* arg2) {
    gchar* argv[] = {(gchar*)"vikey-settings", (gchar*)arg, (gchar*)arg2, nullptr};
    // the picker must not type through the input method (it reads raw keys for its search), the other windows must (the guide's practice fields)
    gchar** env = g_get_environ(); if (!strcmp(arg, "--emoji")) { env = g_environ_setenv(env, "GTK_IM_MODULE", "gtk-im-context-simple", TRUE); env = g_environ_setenv(env, "GDK_BACKEND", "x11", TRUE); }   // X11: the picker centres itself
    g_spawn_async(nullptr, argv, env, (GSpawnFlags)G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr); g_strfreev(env);
}

// ---- the app in front (per-app memory): X11 / XWayland only -----------------------------------------
std::string frontAppX11() {
#ifdef VIKEY_HAVE_X11
    static Display* d = nullptr; static bool tried = false;
    if (!d && !tried) {
        tried = true;
        // the engine is started by ibus-daemon / fcitx5 (a user service) without the session's X variables: fill in GNOME's
        if (!getenv("DISPLAY")) setenv("DISPLAY", ":0", 0);
        if (!getenv("XAUTHORITY")) { const char* rt = getenv("XDG_RUNTIME_DIR"); std::string dir = rt ? rt : "/run/user/1000"; glob_t g = {}; if (glob((dir + "/.mutter-Xwaylandauth.*").c_str(), 0, nullptr, &g) == 0 && g.gl_pathc > 0) setenv("XAUTHORITY", g.gl_pathv[0], 0); globfree(&g); }
        d = XOpenDisplay(nullptr);
    }
    if (!d) return "";
    // XGetInputFocus works on XWayland too (its root has no _NET_ACTIVE_WINDOW); walk up to the window with a class
    Window w = 0; int revert = 0; XGetInputFocus(d, &w, &revert);
    for (int depth = 0; w && w != DefaultRootWindow(d) && depth < 8; depth++) {
        XClassHint h = {};
        if (XGetClassHint(d, w, &h)) { std::string name = h.res_class ? h.res_class : ""; if (h.res_class) XFree(h.res_class); if (h.res_name) XFree(h.res_name); if (!name.empty()) return name; }
        Window root = 0, parent = 0, *kids = nullptr; unsigned n = 0;
        if (!XQueryTree(d, w, &root, &parent, &kids, &n)) break; if (kids) XFree(kids); w = parent;
    }
    return "";
#else
    return "";
#endif
}

std::string hotkeyLabel() {   // "Alt+Z" (same names as vikey-settings)
    int hk = prefs::getInt("SwitchKeyStatus", 0); if (!hk) hk = 0x7A000206;
    static const struct { int k; const char* n; } N[] = {{0,"A"},{11,"B"},{8,"C"},{2,"D"},{14,"E"},{3,"F"},{5,"G"},{4,"H"},{34,"I"},{38,"J"},{40,"K"},{37,"L"},{46,"M"},{45,"N"},{31,"O"},{35,"P"},{12,"Q"},{15,"R"},{1,"S"},{17,"T"},{32,"U"},{9,"V"},{13,"W"},{7,"X"},{16,"Y"},{6,"Z"},
        {18,"1"},{19,"2"},{20,"3"},{21,"4"},{23,"5"},{22,"6"},{26,"7"},{28,"8"},{25,"9"},{29,"0"},{49,"Space"},{36,"Enter"},{48,"Tab"},{50,"`"},{27,"-"},{24,"="},{33,"["},{30,"]"},{42,"\\"},{41,";"},{39,"'"},{43,","},{47,"."},{44,"/"}};
    std::string s; if (hk & 0x100) s += "Ctrl+"; if (hk & 0x200) s += "Alt+"; if (hk & 0x800) s += "Shift+"; if (hk & 0x400) s += "Super+";
    int k = hk & 0xFF; if (k != 0xFE) { for (auto& e : N) if (e.k == k) s += e.n; } else if (!s.empty()) s.pop_back();
    return s;
}

bool readCommand(const std::function<void(const std::string&)>& onEmoji) {
    std::string path = prefs::commandPath(); FILE* f = fopen(path.c_str(), "r"); if (!f) return false;
    char line[256] = {0}; if (fgets(line, sizeof line, f)) { std::string l = line; while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (l.rfind("emoji ", 0) == 0) onEmoji(l.substr(6));   // committed now if the app already has the focus back, else when it gets the focus
        else core::command(l); }
    fclose(f); unlink(path.c_str()); return true;
}

// Auto update (Tự động cập nhật): once a day ask vikey-settings to compare vikey.app/release.json with this version
// and show a notification when there is a newer one (the package manager does the install).
void checkUpdateIfDue() {
    if (!prefs::getInt("AutoUpdate", 1)) return;
    long now = (long)time(nullptr); long last = prefs::getInt("LastUpdateCheck", 0);
    if (now - last < 86400) return;
    prefs::setInt("LastUpdateCheck", (int)now); launchSettings("--check-update", "--quiet");
}

}}
