// The fcitx5 addon (vikey.so): the same job as the IBus engine (Linux/ibus/Engine.cpp). Keys arrive in keyEvent,
// the core decides, and the result goes back as deleted characters + committed text (no pre-edit: the text is typed
// straight into the app, like on macOS and Windows). The status area shows V / E and a menu like the Mac menu bar.
// Unlike the IBus engine this runs inside the fcitx5 process, so it uses fcitx5's event loop for its timers.
#include <fcitx/action.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/event.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputmethodentry.h>
#include <fcitx/inputpanel.h>
#include <fcitx/text.h>
#include <fcitx/instance.h>
#include <fcitx/menu.h>
#include <fcitx/statusarea.h>
#include <fcitx/userinterfacemanager.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/eventloopinterface.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/keysym.h>
#include <fcitx-utils/trackableobject.h>
#include <algorithm>
#include <chrono>
#include <string>
#include "Core.h"
#include "Prefs.h"
#include "Host.h"
#define VK_KEYSYM(name) FcitxKey_##name
#include "KeyMap.h"
using namespace vikey;

namespace {
using fcitx::InputContext;
using fcitx::Key;
using fcitx::KeyState;

unsigned flagsOf(const fcitx::KeyStates& st) {
    unsigned f = 0;
    if (st.test(KeyState::Shift)) f |= core::F_SHIFT;
    if (st.test(KeyState::Ctrl)) f |= core::F_CONTROL;
    if (st.test(KeyState::Alt)) f |= core::F_ALT;
    if (st.test(KeyState::Super)) f |= core::F_WIN;
    if (st.test(KeyState::CapsLock)) f |= core::F_CAPS;
    return f;
}
long long nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

class VikeyEngine;
VikeyEngine* _self = nullptr;   // core::Output is a set of plain callbacks: they reach the addon through this

class VikeyEngine : public fcitx::InputMethodEngineV2 {
public:
    explicit VikeyEngine(fcitx::Instance* instance) : instance_(instance) {
        _self = this;
        host::init();
        prefs::reload();
        core::init(core::Output{
            [](int n) { if (_self) _self->outBackspaces(n); },
            [](const std::u32string& t) { if (_self) _self->outText(t); },
            [](Uint16 key, bool shift) { if (_self) _self->outControlKey(key, shift); },
            host::switchSound,
            [] { host::launchSettings("--emoji"); }});
        buildActions();
        auto& loop = instance_->eventLoop();
        // the settings app writes a file; the engine polls it (and the one-shot command file) once a second
        poll_ = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 1000000, 0, [this](fcitx::EventSourceTime* src, uint64_t) {
            core::pollSettingsFile();
            if (host::readCommand([this](const std::string& emoji) { pendingEmoji_ = emoji; commitPendingEmoji(); })) refresh();
            src->setTime(fcitx::now(CLOCK_MONOTONIC) + 1000000); src->setOneShot(); return true; });
        flush_ = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 10000000, 0, [](fcitx::EventSourceTime* src, uint64_t) {
            core::flushTypingLog();
            src->setTime(fcitx::now(CLOCK_MONOTONIC) + 10000000); src->setOneShot(); return true; });
        update_ = loop.addTimeEvent(CLOCK_MONOTONIC, fcitx::now(CLOCK_MONOTONIC) + 90000000ULL, 0, [](fcitx::EventSourceTime* src, uint64_t) {
            host::checkUpdateIfDue();   // first check 90 s after start, then every hour (it acts once a day)
            src->setTime(fcitx::now(CLOCK_MONOTONIC) + 3600000000ULL); src->setOneShot(); return true; });
    }
    ~VikeyEngine() override { core::flushTypingLog(); core::shutdown(); _self = nullptr; }

    void keyEvent(const fcitx::InputMethodEntry&, fcitx::KeyEvent& ev) override {
        setActive(ev.inputContext());
        const unsigned sym = ev.key().sym();
        const bool release = ev.isRelease();
        if (isModifierKeysym(sym)) {
            unsigned bit = 0;
            if (sym == FcitxKey_Shift_L || sym == FcitxKey_Shift_R) bit = core::F_SHIFT;
            else if (sym == FcitxKey_Control_L || sym == FcitxKey_Control_R) bit = core::F_CONTROL;
            else if (sym == FcitxKey_Alt_L || sym == FcitxKey_Alt_R || sym == FcitxKey_Meta_L || sym == FcitxKey_Meta_R) bit = core::F_ALT;
            else if (sym == FcitxKey_Super_L || sym == FcitxKey_Super_R) bit = core::F_WIN;
            if (bit && core::modifier(bit, !release)) { refresh(); ev.filterAndAccept(); }
            return;
        }
        if (release) return;
        InputContext* ic = ev.inputContext();
        if (ic->capabilityFlags().testAny(fcitx::CapabilityFlags{fcitx::CapabilityFlag::Password, fcitx::CapabilityFlag::Sensitive})) return;   // not in password fields
        if (appPending_) { appPending_ = false; std::string app = appOf(ic); if (!app.empty() && app != core::frontApp()) core::focusChanged(app); }
        MappedKey m = engineKey(sym);
        if (m.key == KEY_EMPTY) { if (sym < 0x100 || sym >= 0x1000000) core::click(); flush(); return; }   // a key we don't model ends the word
        const unsigned flags = flagsOf(ev.key().states());
        ctrlKeyPending_ = false; skipped_ = false;
        const bool consumed = core::keyDown(m.key, m.shift || (flags & core::F_SHIFT), flags) && !skipped_;   // a skipped fix: the key is an ordinary key
        refresh();
        lastOutputMs_ = nowMs();
        if (ctrlKeyPending_ && !skipped_) { ctrlKeyPending_ = false; flush(); return; }   // Enter/Tab after a restore: the text is in, the key itself goes on to the app
        if (consumed) { trim(); showTail(); ev.filterAndAccept(); return; }
        // The core let the key through to the app. Keep it in the tail instead, so a later fix (a tone key, auto switch, undo)
        // can still change it: the app never sees text we may have to take back.
        if (!(flags & (core::F_CONTROL | core::F_ALT | core::F_WIN))) {
            if (sym == FcitxKey_BackSpace && !tail_.empty()) { tail_.pop_back(); showTail(); ev.filterAndAccept(); return; }
            const char32_t ch = (char32_t)fcitx::Key::keySymToUnicode((fcitx::KeySym)sym);
            if (ch >= 0x20 && !(ch >= 0x7f && ch < 0xa0)) { tail_ += ch; trim(); showTail(); ev.filterAndAccept(); return; }
        }
        flush();   // Enter, Tab, arrows, shortcuts...: the tail goes into the text first, then the key
    }
    // fcitx5 calls activate when the input context gets the focus (or switches to Vikey) and deactivate when it loses it.
    void activate(const fcitx::InputMethodEntry&, fcitx::InputContextEvent& ev) override {
        InputContext* ic = ev.inputContext(); setActive(ic); tail_.clear();
        ic->statusArea().addAction(fcitx::StatusGroup::InputMethod, &langAction_);
        ic->statusArea().addAction(fcitx::StatusGroup::InputMethod, &menuAction_);
        core::focusChanged(appOf(ic));
        appPending_ = true;   // the focus can come before the window manager has moved it: check again on the first key
        refresh();
        commitPendingEmoji();
    }
    void deactivate(const fcitx::InputMethodEntry&, fcitx::InputContextEvent& ev) override {
        tail_.clear(); core::click();   // fcitx5 commits the client pre-edit itself when the focus goes (committing it here too would type it twice)
        if (active() == ev.inputContext()) active_.unwatch();
    }
    void reset(const fcitx::InputMethodEntry&, fcitx::InputContextEvent&) override {
        if (nowMs() - lastOutputMs_ < 300) return;   // a reset caused by our own key/commit is not a click
        flush(); core::click();
    }
    std::string subMode(const fcitx::InputMethodEntry&, InputContext&) override { return core::language() == 1 ? "Tiếng Việt" : "English"; }
    std::string subModeLabelImpl(const fcitx::InputMethodEntry&, InputContext&) override { return core::language() == 1 ? "V" : "E"; }

private:
    InputContext* active() { return active_.get(); }
    void setActive(InputContext* ic) { active_ = ic->watch(); }
    // The app in front, for per-app memory. fcitx5's GTK and Qt modules tell it (so Wayland apps work too); else X11.
    std::string appOf(InputContext* ic) {
        if (ic && !ic->program().empty()) return ic->program();
        return host::frontAppX11();
    }

    // ---- output to the app ---------------------------------------------------------------------------
    // The last few words stay in the client's pre-edit (the "tail") and only older words are committed. The core's edits
    // (delete n, type text) are applied to the tail, so a tone key or an auto switch never has to delete text the app
    // already holds: Chromium/Electron on Wayland reports surrounding text but ignores deletes from the input method,
    // and forwarded BackSpace keys are not delivered the same way by every frontend. Only the words being typed stay in the tail (see trim).
    void outBackspaces(int n) {
        if (n <= (int)tail_.size()) { tail_.resize(tail_.size() - n); return; }
        // The core reaches past the tail: a look-back over earlier words, or the undo of a fix after its space. Those words are
        // already in the text. Through the client's surrounding text that works in GTK/Qt, but not in Chromium/Electron on
        // Wayland (text-input-v3), where the delete is ignored and the new text would be typed on top of the old. There the
        // fix is skipped: the text stays as typed (and the key goes on to the app, see keyEvent).
        InputContext* ic = active();
        if (ic && ic->capabilityFlags().test(fcitx::CapabilityFlag::SurroundingText) && ic->surroundingText().isValid() && std::string(ic->frontend()) != "wayland_v2") {
            flush(); ic->deleteSurroundingText(-n, n); return;
        }
        skipped_ = true;
    }
    void outText(const std::u32string& t) { if (!skipped_) tail_ += t; }
    void outControlKey(Uint16, bool) { ctrlKeyPending_ = true; }
    void showTail() {
        InputContext* ic = active(); if (!ic) return;
        fcitx::Text text; if (!tail_.empty()) text.append(host::utf8(tail_), fcitx::TextFormatFlag::NoFlag);
        text.setCursor(text.toString().size());
        ic->inputPanel().setClientPreedit(text);
        if (!ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) ic->inputPanel().setPreedit(text);   // the client has no pre-edit: the panel shows it
        ic->updatePreedit(); ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    }
    // Everything in the tail goes into the text.
    void flush() {
        InputContext* ic = active();
        if (!ic) { tail_.clear(); return; }
        if (tail_.empty()) return;
        const std::string t = host::utf8(tail_); tail_.clear();
        ic->inputPanel().reset(); ic->updatePreedit(); ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);   // clear the pre-edit first: some clients show both
        ic->commitString(t);
    }
    void trim() {
        // FcitxPreeditWords in settings.json: how many finished words stay in the pre-edit behind the one being typed. The client
        // underlines the pre-edit, so the default 0 underlines only the current word; 3 (the most the core can look back over)
        // keeps look-back and undo working in Chromium/Electron on Wayland, at the price of underlining the last words.
        const int keep = std::max(0, std::min(3, prefs::getInt("FcitxPreeditWords", 0)));
        int spaces = 0; for (char32_t c : tail_) if (c == U' ' || c == U'\n') spaces++;
        InputContext* ic = active(); if (!ic) return;
        while (spaces > keep) {
            size_t i = 0; while (tail_[i] != U' ' && tail_[i] != U'\n') i++;
            ic->commitString(host::utf8(tail_.substr(0, i + 1))); tail_.erase(0, i + 1); spaces--;
        }
    }
    // The emoji picker is a window of vikey-settings (--emoji). It takes the keyboard focus; its choice comes back through
    // the command file ("emoji 🐴") and is committed to the app once the focus is back there.
    void commitPendingEmoji() {
        InputContext* ic = active(); if (pendingEmoji_.empty() || !ic) return;
        flush(); core::resetTyping();
        ic->commitString(pendingEmoji_);
        pendingEmoji_.clear(); lastOutputMs_ = nowMs();
    }

    // ---- status area: V / E and the menu ------------------------------------------------------------
    void buildActions() {
        auto& ui = instance_->userInterfaceManager();
        langAction_.setCheckable(true); langAction_.setIcon("vikey");
        langAction_.connect<fcitx::SimpleAction::Activated>([this](InputContext*) { core::setLanguageByUser(core::language() == 1 ? 0 : 1); refresh(); });
        autoAction_.setCheckable(true); autoAction_.setShortText("Tự chuyển Anh/Việt");
        autoAction_.connect<fcitx::SimpleAction::Activated>([this](InputContext*) {
            prefs::setInt("AutoSwitchByTyping", prefs::getInt("AutoSwitchByTyping", 1) ? 0 : 1); core::reloadSettings(); refresh(); });
        const struct { fcitx::SimpleAction* a; const char* text; const char* arg; } items[] = {
            {&openAction_, "Mở Vikey…", ""}, {&guideAction_, "Hướng dẫn…", "--guide"}, {&updateAction_, "Kiểm tra cập nhật…", "--check-update"},
            {&aboutAction_, "Giới thiệu Vikey", "--about"}, {&logAction_, "Xem nhật ký gõ…", "--log"}};
        for (auto& it : items) { it.a->setShortText(it.text); const char* arg = it.arg; it.a->connect<fcitx::SimpleAction::Activated>([arg](InputContext*) { host::launchSettings(arg); }); }
        menuAction_.setShortText("Vikey"); menuAction_.setIcon("vikey"); menuAction_.setMenu(&menu_);
        // the Mac menu bar's order; the typing log is a debug item there (Gỡ lỗi), shown only while logging is on
        for (fcitx::Action* a : std::initializer_list<fcitx::Action*>{&autoAction_, &openAction_, &guideAction_, &updateAction_, &aboutAction_}) menu_.addAction(a);
        ui.registerAction("vikey-lang", &langAction_); ui.registerAction("vikey-menu", &menuAction_); ui.registerAction("vikey-auto", &autoAction_);
        ui.registerAction("vikey-open", &openAction_); ui.registerAction("vikey-guide", &guideAction_); ui.registerAction("vikey-update", &updateAction_);
        ui.registerAction("vikey-about", &aboutAction_); ui.registerAction("vikey-log", &logAction_);
    }
    void refresh() {
        const bool vi = core::language() == 1;
        langAction_.setShortText(vi ? "V" : "E");
        langAction_.setLongText("Tiếng Việt   " + host::hotkeyLabel());   // the Mac's "Tiếng Việt  ⌥Z", a switch on in Vietnamese
        langAction_.setChecked(vi);
        autoAction_.setChecked(prefs::getInt("AutoSwitchByTyping", 1) != 0);
        const bool wantLog = prefs::getInt("TypingLogEnabled", 0) != 0;
        if (wantLog != logShown_) { logShown_ = wantLog; if (wantLog) menu_.addAction(&logAction_); else menu_.removeAction(&logAction_); }
        InputContext* ic = active();
        langAction_.update(ic); autoAction_.update(ic); menuAction_.update(ic);
        if (ic) ic->updateUserInterface(fcitx::UserInterfaceComponent::StatusArea);
    }

    fcitx::Instance* instance_;
    fcitx::TrackableObjectReference<InputContext> active_;   // the input context with the focus
    long long lastOutputMs_ = 0;    // when we last let a key through or committed text (GTK resets the IM context right after inserting)
    bool appPending_ = false;
    bool skipped_ = false;          // this key's fix would have changed words already in the text: it was skipped
    bool ctrlKeyPending_ = false;   // the core wants Enter/Tab sent after a restore
    std::u32string tail_;           // the recent words, shown as the client's pre-edit
    bool logShown_ = false;
    std::string pendingEmoji_;
    fcitx::SimpleAction langAction_, autoAction_, openAction_, guideAction_, updateAction_, aboutAction_, logAction_, menuAction_;
    fcitx::Menu menu_;
    std::unique_ptr<fcitx::EventSourceTime> poll_, flush_, update_;
};

class VikeyFactory : public fcitx::AddonFactory {
public:
    fcitx::AddonInstance* create(fcitx::AddonManager* manager) override { return new VikeyEngine(manager->instance()); }
};
}  // namespace

FCITX_ADDON_FACTORY(VikeyFactory)
