// What the IBus engine and the fcitx5 addon both do around the core, with no framework in it: the switch
// sound, starting vikey-settings, the app in front on X11, the hotkey's name, the one-shot command file and
// the daily update check. Everything here works without a GLib main loop (the addon runs inside fcitx5).
#pragma once
#include <string>
#include <functional>
namespace vikey { namespace host {
void init();                                              // GStreamer, once
std::string utf8(const std::u32string& w);
void switchSound(int language);                           // rising into Vietnamese, falling into English
void launchSettings(const char* arg, const char* arg2 = nullptr);   // vikey-settings [--emoji|--log|--guide|--check-update|--about]
std::string frontAppX11();                                // WM_CLASS of the focused window ("" on pure Wayland)
std::string hotkeyLabel();                                // "Alt+Z"
/// Reads and deletes the command file the settings app (and tests) write. "emoji X" goes to onEmoji, the rest to core::command.
bool readCommand(const std::function<void(const std::string&)>& onEmoji);   // false when there was no command file
/// Once a day, when "Tự động cập nhật" is on: vikey-settings compares vikey.app/release.json with this version.
void checkUpdateIfDue();
}}
