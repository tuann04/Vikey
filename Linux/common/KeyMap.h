// Keysyms (what IBus and fcitx5 hand the engine; both use the X11 values) -> the engine's key codes (Engine/platforms/mac.h) + shift.
// Letters follow the active layout already (keysym 'a' is wherever the layout puts it).
// The includer defines VK_KEYSYM(name) before this header: IBUS_KEY_##name or FcitxKey_##name.
#pragma once
#include "Engine.h"
namespace vikey {
struct MappedKey { Uint16 key; bool shift; };
inline MappedKey engineKey(unsigned keyval) {
    if (keyval >= VK_KEYSYM(a) && keyval <= VK_KEYSYM(z)) { static const Uint16 L[] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z}; return {L[keyval - VK_KEYSYM(a)], false}; }
    if (keyval >= VK_KEYSYM(A) && keyval <= VK_KEYSYM(Z)) { static const Uint16 L[] = {KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z}; return {L[keyval - VK_KEYSYM(A)], true}; }
    switch (keyval) {
        case VK_KEYSYM(0): return {KEY_0, false}; case VK_KEYSYM(1): return {KEY_1, false}; case VK_KEYSYM(2): return {KEY_2, false}; case VK_KEYSYM(3): return {KEY_3, false}; case VK_KEYSYM(4): return {KEY_4, false};
        case VK_KEYSYM(5): return {KEY_5, false}; case VK_KEYSYM(6): return {KEY_6, false}; case VK_KEYSYM(7): return {KEY_7, false}; case VK_KEYSYM(8): return {KEY_8, false}; case VK_KEYSYM(9): return {KEY_9, false};
        case VK_KEYSYM(parenright): return {KEY_0, true}; case VK_KEYSYM(exclam): return {KEY_1, true}; case VK_KEYSYM(at): return {KEY_2, true}; case VK_KEYSYM(numbersign): return {KEY_3, true}; case VK_KEYSYM(dollar): return {KEY_4, true};
        case VK_KEYSYM(percent): return {KEY_5, true}; case VK_KEYSYM(asciicircum): return {KEY_6, true}; case VK_KEYSYM(ampersand): return {KEY_7, true}; case VK_KEYSYM(asterisk): return {KEY_8, true}; case VK_KEYSYM(parenleft): return {KEY_9, true};
        case VK_KEYSYM(space): return {KEY_SPACE, false};
        case VK_KEYSYM(Return): return {KEY_RETURN, false}; case VK_KEYSYM(KP_Enter): return {KEY_ENTER, false};
        case VK_KEYSYM(BackSpace): return {KEY_DELETE, false};
        case VK_KEYSYM(Tab): case VK_KEYSYM(ISO_Left_Tab): return {KEY_TAB, false};
        case VK_KEYSYM(Escape): return {KEY_ESC, false};
        case VK_KEYSYM(Left): return {KEY_LEFT, false}; case VK_KEYSYM(Right): return {KEY_RIGHT, false}; case VK_KEYSYM(Up): return {KEY_UP, false}; case VK_KEYSYM(Down): return {KEY_DOWN, false};
        case VK_KEYSYM(semicolon): return {KEY_SEMICOLON, false}; case VK_KEYSYM(colon): return {KEY_SEMICOLON, true};
        case VK_KEYSYM(slash): return {KEY_SLASH, false}; case VK_KEYSYM(question): return {KEY_SLASH, true};
        case VK_KEYSYM(grave): return {KEY_BACKQUOTE, false}; case VK_KEYSYM(asciitilde): return {KEY_BACKQUOTE, true};
        case VK_KEYSYM(bracketleft): return {KEY_LEFT_BRACKET, false}; case VK_KEYSYM(braceleft): return {KEY_LEFT_BRACKET, true};
        case VK_KEYSYM(bracketright): return {KEY_RIGHT_BRACKET, false}; case VK_KEYSYM(braceright): return {KEY_RIGHT_BRACKET, true};
        case VK_KEYSYM(backslash): return {KEY_BACK_SLASH, false}; case VK_KEYSYM(bar): return {KEY_BACK_SLASH, true};
        case VK_KEYSYM(apostrophe): return {KEY_QUOTE, false}; case VK_KEYSYM(quotedbl): return {KEY_QUOTE, true};
        case VK_KEYSYM(comma): return {KEY_COMMA, false}; case VK_KEYSYM(less): return {KEY_COMMA, true};
        case VK_KEYSYM(period): return {KEY_DOT, false}; case VK_KEYSYM(greater): return {KEY_DOT, true};
        case VK_KEYSYM(minus): return {KEY_MINUS, false}; case VK_KEYSYM(underscore): return {KEY_MINUS, true};
        case VK_KEYSYM(equal): return {KEY_EQUALS, false}; case VK_KEYSYM(plus): return {KEY_EQUALS, true};
        // word breaks the engine has no code for: act like Esc
        case VK_KEYSYM(Insert): case VK_KEYSYM(Home): case VK_KEYSYM(End): case VK_KEYSYM(Delete): case VK_KEYSYM(Page_Up): case VK_KEYSYM(Page_Down):
        case VK_KEYSYM(Print): case VK_KEYSYM(Num_Lock): case VK_KEYSYM(Scroll_Lock): case VK_KEYSYM(KP_Home): case VK_KEYSYM(KP_End): case VK_KEYSYM(KP_Delete):
            return {KEY_ESC, false};
        default: return {KEY_EMPTY, false};
    }
}
inline bool isModifierKeysym(unsigned k) {
    return k == VK_KEYSYM(Shift_L) || k == VK_KEYSYM(Shift_R) || k == VK_KEYSYM(Control_L) || k == VK_KEYSYM(Control_R) || k == VK_KEYSYM(Alt_L) || k == VK_KEYSYM(Alt_R) ||
           k == VK_KEYSYM(Meta_L) || k == VK_KEYSYM(Meta_R) || k == VK_KEYSYM(Super_L) || k == VK_KEYSYM(Super_R) || k == VK_KEYSYM(Caps_Lock) || k == VK_KEYSYM(ISO_Level3_Shift);
}
/// The keysym that types this engine key (for re-sending a control key after a restore).
inline unsigned keysymOf(Uint16 key) {
    switch (key) { case KEY_RETURN: return VK_KEYSYM(Return); case KEY_ENTER: return VK_KEYSYM(KP_Enter); case KEY_TAB: return VK_KEYSYM(Tab); case KEY_ESC: return VK_KEYSYM(Escape);
                   case KEY_SPACE: return VK_KEYSYM(space); case KEY_LEFT: return VK_KEYSYM(Left); case KEY_RIGHT: return VK_KEYSYM(Right); case KEY_UP: return VK_KEYSYM(Up); case KEY_DOWN: return VK_KEYSYM(Down); default: return 0; }
}
}
