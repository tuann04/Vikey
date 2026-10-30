// ibus-engine-vikey: the IBus engine. Keys arrive in process_key_event, the core decides, and the
// result goes back as deleted characters + committed text (no pre-edit: the text is typed straight into
// the app, like on macOS and Windows). The panel shows V / E and a menu like the Mac menu bar.
#include <ibus.h>
#include <glib.h>
#include <glib-unix.h>
#include <string>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <ctime>
#include "Core.h"
#include "Prefs.h"
#include "Host.h"
#define VK_KEYSYM(name) IBUS_KEY_##name
#include "KeyMap.h"
#ifndef VIKEY_VERSION
#define VIKEY_VERSION "0.0.0"
#endif
using namespace vikey;
using host::utf8; using host::frontAppX11; using host::hotkeyLabel; using host::launchSettings;

typedef struct { IBusEngine parent; guint caps; gboolean enabled; IBusPropList* props; IBusProperty *pLang, *pAuto, *pOpen, *pLog, *pGuide, *pUpdate, *pAbout; } VikeyEngine;
typedef struct { IBusEngineClass parent; } VikeyEngineClass;
static GType vikey_engine_get_type();
#define VIKEY_TYPE_ENGINE (vikey_engine_get_type())
static VikeyEngine* _active = nullptr;   // the engine with the focus
static gint64 _lastOutputUs = 0;
static bool _appPending = false;          // focus_in can come before X has moved the focus: re-check on the first key          // when we last let a key through or committed text (GTK resets the IM context right after inserting)
static IBusBus* _bus = nullptr;


// ---- output to the app ---------------------------------------------------------------------------
static void outBackspaces(int n) {
    if (!_active) return;
    if (_active->caps & IBUS_CAP_SURROUNDING_TEXT) ibus_engine_delete_surrounding_text(IBUS_ENGINE(_active), -n, n);
    else for (int i = 0; i < n; i++) { ibus_engine_forward_key_event(IBUS_ENGINE(_active), IBUS_KEY_BackSpace, 14, 0); ibus_engine_forward_key_event(IBUS_ENGINE(_active), IBUS_KEY_BackSpace, 14, IBUS_RELEASE_MASK); }
}
static void outText(const std::u32string& t) { if (!_active) return; std::string s = utf8(t); ibus_engine_commit_text(IBUS_ENGINE(_active), ibus_text_new_from_string(s.c_str())); }
static void outControlKey(Uint16 key, bool shift) { if (!_active) return; guint ks = keysymOf(key); if (!ks) return; guint st = shift ? IBUS_SHIFT_MASK : 0; ibus_engine_forward_key_event(IBUS_ENGINE(_active), ks, 0, st); ibus_engine_forward_key_event(IBUS_ENGINE(_active), ks, 0, st | IBUS_RELEASE_MASK); }
// The emoji picker is a window of vikey-settings (--emoji). It takes the keyboard focus, so the engine sees a
// focus_out; its choice comes back through the command file ("emoji 🐴") and is committed to the app once the
// focus is back there. The picker uses GTK's simple input method (not IBus), so its own typing never reaches us.
static std::string _pendingEmoji;
static void outOpenEmoji() { launchSettings("--emoji", nullptr); }
static void commitPendingEmoji() {
    if (_pendingEmoji.empty() || !_active) return;
    core::resetTyping();
    ibus_engine_commit_text(IBUS_ENGINE(_active), ibus_text_new_from_string(_pendingEmoji.c_str()));
    _pendingEmoji.clear(); _lastOutputUs = g_get_monotonic_time();
}


// ---- panel properties (the "menu bar")
static void updateLangProp(VikeyEngine* e) {
    if (!e || !e->pLang) return;
    int vi = core::language();
    ibus_property_set_label(e->pLang, ibus_text_new_from_string(("Tiếng Việt   " + hotkeyLabel()).c_str()));   // the Mac's "Tiếng Việt  ⌥Z", a switch on in Vietnamese
    ibus_property_set_symbol(e->pLang, ibus_text_new_from_string(vi ? "V" : "E"));
    ibus_property_set_state(e->pLang, vi ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED);
    ibus_engine_update_property(IBUS_ENGINE(e), e->pLang);
    if (e->pLog && (bool)ibus_property_get_visible(e->pLog) != (prefs::getInt("TypingLogEnabled", 0) != 0)) { ibus_property_set_visible(e->pLog, !ibus_property_get_visible(e->pLog)); ibus_engine_update_property(IBUS_ENGINE(e), e->pLog); }
    if (e->pAuto) { ibus_property_set_state(e->pAuto, prefs::getInt("AutoSwitchByTyping", 1) ? PROP_STATE_CHECKED : PROP_STATE_UNCHECKED); ibus_engine_update_property(IBUS_ENGINE(e), e->pAuto); }
}
static void buildProps(VikeyEngine* e) {
    e->props = ibus_prop_list_new(); g_object_ref_sink(e->props);
    e->pLang = ibus_property_new("lang", PROP_TYPE_TOGGLE, ibus_text_new_from_string("Tiếng Việt"), nullptr, ibus_text_new_from_string("Chuyển Anh/Việt (Alt+Z)"), TRUE, TRUE, PROP_STATE_CHECKED, nullptr);
    ibus_property_set_symbol(e->pLang, ibus_text_new_from_string("V"));
    e->pAuto = ibus_property_new("auto", PROP_TYPE_TOGGLE, ibus_text_new_from_string("Tự chuyển Anh/Việt"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_CHECKED, nullptr);
    e->pOpen = ibus_property_new("open", PROP_TYPE_NORMAL, ibus_text_new_from_string("Mở Vikey…"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    e->pLog = ibus_property_new("log", PROP_TYPE_NORMAL, ibus_text_new_from_string("Xem nhật ký gõ…"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    e->pGuide = ibus_property_new("guide", PROP_TYPE_NORMAL, ibus_text_new_from_string("Hướng dẫn…"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    e->pUpdate = ibus_property_new("update", PROP_TYPE_NORMAL, ibus_text_new_from_string("Kiểm tra cập nhật…"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    e->pAbout = ibus_property_new("about", PROP_TYPE_NORMAL, ibus_text_new_from_string("Giới thiệu Vikey"), nullptr, nullptr, TRUE, TRUE, PROP_STATE_UNCHECKED, nullptr);
    // the Mac menu bar's order; the typing log is a debug item there (Gỡ lỗi), shown only while logging is on
    ibus_property_set_visible(e->pLog, prefs::getInt("TypingLogEnabled", 0) != 0);
    for (IBusProperty* p : {e->pLang, e->pAuto, e->pOpen, e->pGuide, e->pUpdate, e->pAbout, e->pLog}) { g_object_ref_sink(p); ibus_prop_list_append(e->props, p); }
}

// ---- IBusEngine overrides --------------------------------------------------------------------------
static unsigned flagsOf(guint state) { unsigned f = 0; if (state & IBUS_SHIFT_MASK) f |= core::F_SHIFT; if (state & IBUS_CONTROL_MASK) f |= core::F_CONTROL; if (state & IBUS_MOD1_MASK) f |= core::F_ALT; if (state & IBUS_SUPER_MASK) f |= core::F_WIN; if (state & IBUS_LOCK_MASK) f |= core::F_CAPS; return f; }
static gboolean vikey_process_key_event(IBusEngine* engine, guint keyval, guint keycode, guint state) {
    VikeyEngine* e = (VikeyEngine*)engine; _active = e;
    bool release = state & IBUS_RELEASE_MASK;
    if (isModifierKeysym(keyval)) {
        unsigned bit = 0;
        if (keyval == IBUS_KEY_Shift_L || keyval == IBUS_KEY_Shift_R) bit = core::F_SHIFT; else if (keyval == IBUS_KEY_Control_L || keyval == IBUS_KEY_Control_R) bit = core::F_CONTROL;
        else if (keyval == IBUS_KEY_Alt_L || keyval == IBUS_KEY_Alt_R || keyval == IBUS_KEY_Meta_L || keyval == IBUS_KEY_Meta_R) bit = core::F_ALT; else if (keyval == IBUS_KEY_Super_L || keyval == IBUS_KEY_Super_R) bit = core::F_WIN;
        if (bit && core::modifier(bit, !release)) { updateLangProp(e); return TRUE; }
        return FALSE;
    }
    if (release) return FALSE;
    if (_appPending) { _appPending = false; std::string app = frontAppX11(); if (!app.empty() && app != core::frontApp()) core::focusChanged(app); }
    MappedKey m = engineKey(keyval);
    if (m.key == KEY_EMPTY) { if (keyval < 0x100 || (keyval >= 0x1000000)) core::click(); return FALSE; }   // a key we don't model ends the word
    unsigned flags = flagsOf(state);
    bool consumed = core::keyDown(m.key, m.shift || (flags & core::F_SHIFT), flags);
    updateLangProp(e);
    _lastOutputUs = g_get_monotonic_time();
    return consumed ? TRUE : FALSE;
}
static void vikey_focus_in(IBusEngine* engine) {
    VikeyEngine* e = (VikeyEngine*)engine; _active = e;
    ibus_engine_register_properties(engine, e->props);
    std::string app = frontAppX11();
    core::focusChanged(app);
    _appPending = true;
    updateLangProp(e);
    commitPendingEmoji();
}
static void vikey_focus_out(IBusEngine* engine) { core::click(); if (_active == (VikeyEngine*)engine) _active = nullptr; }
static void vikey_reset(IBusEngine* engine) { if (g_get_monotonic_time() - _lastOutputUs < 300000) return; core::click(); }   // a reset caused by our own key/commit is not a click
static void vikey_enable(IBusEngine* engine) { VikeyEngine* e = (VikeyEngine*)engine; e->enabled = TRUE; ibus_engine_get_surrounding_text(engine, nullptr, nullptr, nullptr); }
static void vikey_disable(IBusEngine* engine) { ((VikeyEngine*)engine)->enabled = FALSE; core::flushTypingLog(); }
static void vikey_set_capabilities(IBusEngine* engine, guint caps) { ((VikeyEngine*)engine)->caps = caps; }
static void vikey_property_activate(IBusEngine* engine, const gchar* name, guint state) {
    VikeyEngine* e = (VikeyEngine*)engine;
    if (!strcmp(name, "lang")) core::setLanguageByUser(state == PROP_STATE_CHECKED ? 1 : 0);
    else if (!strcmp(name, "auto")) { prefs::setInt("AutoSwitchByTyping", state == PROP_STATE_CHECKED ? 1 : 0); core::reloadSettings(); }
    else if (!strcmp(name, "open")) launchSettings("");
    else if (!strcmp(name, "log")) launchSettings("--log");
    else if (!strcmp(name, "guide")) launchSettings("--guide");
    else if (!strcmp(name, "update")) launchSettings("--check-update");
    else if (!strcmp(name, "about")) launchSettings("--about");
    updateLangProp(e);
}
static void vikey_engine_init(VikeyEngine* e) { e->caps = 0; e->enabled = FALSE; buildProps(e); }
static void vikey_engine_class_init(VikeyEngineClass* klass) {
    IBusEngineClass* c = IBUS_ENGINE_CLASS(klass);
    c->process_key_event = vikey_process_key_event; c->focus_in = vikey_focus_in; c->focus_out = vikey_focus_out; c->reset = vikey_reset;
    c->enable = vikey_enable; c->disable = vikey_disable; c->set_capabilities = vikey_set_capabilities; c->property_activate = vikey_property_activate;
}
static GType vikey_engine_get_type() {
    static GType t = 0;
    if (!t) { static const GTypeInfo info = {sizeof(VikeyEngineClass), nullptr, nullptr, (GClassInitFunc)vikey_engine_class_init, nullptr, nullptr, sizeof(VikeyEngine), 0, (GInstanceInitFunc)vikey_engine_init, nullptr}; t = g_type_register_static(IBUS_TYPE_ENGINE, "VikeyEngine", &info, (GTypeFlags)0); }
    return t;
}

// ---- settings file watch + periodic flush -------------------------------------------------------------
// Reload only when a write is done (not on every CHANGED: the file may be half-written).
static void readCommand() {
    if (host::readCommand([](const std::string& emoji) { _pendingEmoji = emoji; commitPendingEmoji(); })) updateLangProp(_active);
}
static gboolean onUpdateTick(gpointer) { host::checkUpdateIfDue(); return TRUE; }
static void onSettingsChanged(GFileMonitor*, GFile* file, GFile*, GFileMonitorEvent ev, gpointer) {
    if (file) { char* b = g_file_get_basename(file); bool isCmd = b && !strcmp(b, "command"); g_free(b); if (isCmd) { if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_MOVED_IN || ev == G_FILE_MONITOR_EVENT_RENAMED || ev == G_FILE_MONITOR_EVENT_CREATED) readCommand(); return; } } if (ev == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || ev == G_FILE_MONITOR_EVENT_MOVED_IN || ev == G_FILE_MONITOR_EVENT_RENAMED || ev == G_FILE_MONITOR_EVENT_CREATED) { if (prefs::changedByOthers()) core::reloadSettings(); updateLangProp(_active); } }
static gboolean onFlush(gpointer) { core::flushTypingLog(); return TRUE; }
static void onBusDisconnected(IBusBus*, gpointer) { core::shutdown(); ibus_quit(); }

int main(int argc, char** argv) {
    bool ibusMode = false; for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--ibus")) ibusMode = true;
    ibus_init(); host::init();
    core::init(core::Output{outBackspaces, outText, outControlKey, host::switchSound, outOpenEmoji});
    _bus = ibus_bus_new();
    if (!ibus_bus_is_connected(_bus)) { fprintf(stderr, "vikey: cannot connect to the IBus daemon\n"); return 1; }
    g_signal_connect(_bus, "disconnected", G_CALLBACK(onBusDisconnected), nullptr);
    IBusFactory* factory = ibus_factory_new(ibus_bus_get_connection(_bus));
    ibus_factory_add_engine(factory, "vikey", VIKEY_TYPE_ENGINE);
    if (ibusMode) ibus_bus_request_name(_bus, "org.freedesktop.IBus.Vikey", 0);
    else {   // run by hand (development): register the component ourselves
        IBusComponent* c = ibus_component_new("org.freedesktop.IBus.Vikey", "Vikey", VIKEY_VERSION, "GPL-3.0", "Tony Dinh", "https://vikey.app", "", "ibus-vikey");
        ibus_component_add_engine(c, ibus_engine_desc_new("vikey", "Vikey", "Vikey - Gõ tiếng Việt, tự chuyển Anh/Việt", "vi", "GPL-3.0", "Tony Dinh", "vikey", "us"));
        ibus_bus_register_component(_bus, c);
    }
    GFile* dir = g_file_new_for_path(prefs::configDir().c_str()); g_mkdir_with_parents(prefs::configDir().c_str(), 0700);
    GFileMonitor* mon = g_file_monitor_directory(dir, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr); if (mon) g_signal_connect(mon, "changed", G_CALLBACK(onSettingsChanged), nullptr);
    GFile* sf = g_file_new_for_path(prefs::settingsPath().c_str());
    GFileMonitor* fmon = g_file_monitor_file(sf, G_FILE_MONITOR_WATCH_MOVES, nullptr, nullptr); if (fmon) g_signal_connect(fmon, "changed", G_CALLBACK(onSettingsChanged), nullptr);
    g_timeout_add(1000, [](gpointer) -> gboolean { core::pollSettingsFile(); readCommand(); return TRUE; }, nullptr);   // belt and braces: mtime poll + command file
    g_timeout_add_seconds(10, onFlush, nullptr);
    g_timeout_add_seconds(90, [](gpointer) -> gboolean { onUpdateTick(nullptr); g_timeout_add_seconds(3600, onUpdateTick, nullptr); return FALSE; }, nullptr);
    ibus_main();
    core::shutdown();
    return 0;
}
