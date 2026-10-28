#include "keyboard_input.h"

#include "mouse_input.h"
#include "../core/log.h"

#include <Psapi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace ptd {

namespace {

constexpr wchar_t kKeyboardClassName[] = L"ProTrailKeyboardInput";

std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string to_upper_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

bool is_modifier_vk(uint16_t vk) {
    switch (vk) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
        case VK_LWIN:
        case VK_RWIN:
            return true;
        default:
            return false;
    }
}

const char* modifier_tap_event_id(uint16_t vk) {
    switch (vk) {
        case VK_SHIFT:
        case VK_LSHIFT:
        case VK_RSHIFT:
            return "key.shift";
        case VK_CONTROL:
        case VK_LCONTROL:
        case VK_RCONTROL:
            return "key.ctrl";
        case VK_MENU:
        case VK_LMENU:
        case VK_RMENU:
            return "key.alt";
        case VK_LWIN:
        case VK_RWIN:
            return "key.win";
        default:
            return "";
    }
}

std::string vkey_token_name(uint16_t vk, bool is_e0) {
    if (vk >= 'A' && vk <= 'Z') {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= '0' && vk <= '9') {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return "F" + std::to_string(vk - VK_F1 + 1);
    }
    switch (vk) {
        case VK_BACK:     return "Backspace";
        case VK_DELETE:   return "Delete";
        case VK_RETURN:   return is_e0 ? "NumpadEnter" : "Enter";
        case VK_TAB:      return "Tab";
        case VK_ESCAPE:   return "Escape";
        case VK_INSERT:   return "Insert";
        case VK_SPACE:    return "Space";
        case VK_UP:       return "Up";
        case VK_DOWN:     return "Down";
        case VK_LEFT:     return "Left";
        case VK_RIGHT:    return "Right";
        case VK_HOME:     return "Home";
        case VK_END:      return "End";
        case VK_PRIOR:    return "PageUp";
        case VK_NEXT:     return "PageDown";
        case VK_CAPITAL:  return "CapsLock";
        case VK_NUMLOCK:  return "NumLock";
        case VK_SCROLL:   return "ScrollLock";
        case VK_ADD:      return "NumpadAdd";
        case VK_SUBTRACT: return "NumpadSubtract";
        case VK_MULTIPLY: return "NumpadMultiply";
        case VK_DIVIDE:   return "NumpadDivide";
        case VK_DECIMAL:  return "NumpadDecimal";
        case VK_OEM_PLUS:   return "Plus";
        case VK_OEM_MINUS:  return "Minus";
        case VK_OEM_COMMA:  return "Comma";
        case VK_OEM_PERIOD: return "Period";
        case VK_OEM_1:      return "Semicolon";
        case VK_OEM_2:      return "Slash";
        case VK_OEM_3:      return "Grave";
        case VK_OEM_4:      return "BracketLeft";
        case VK_OEM_5:      return "Backslash";
        case VK_OEM_6:      return "BracketRight";
        case VK_OEM_7:      return "Quote";
        default: break;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return "Numpad" + std::to_string(vk - VK_NUMPAD0);
    }
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "VK_0x%02X", static_cast<unsigned>(vk));
    return std::string(buf);
}

std::string canonicalize_key_token(const std::string& raw) {
    const std::string low = to_lower_ascii(raw);
    if (low.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(low[0]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            return to_upper_ascii(low);
        }
        if (c == '+') return "Plus";
        if (c == '-') return "Minus";
        if (c == ',') return "Comma";
        if (c == '.') return "Period";
        if (c == ';') return "Semicolon";
        if (c == '/') return "Slash";
        if (c == '`') return "Grave";
        if (c == '[') return "BracketLeft";
        if (c == '\\') return "Backslash";
        if (c == ']') return "BracketRight";
        if (c == '\'') return "Quote";
    }
    if (low.size() >= 2 && low[0] == 'f') {
        bool all_digits = true;
        for (std::size_t i = 1; i < low.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(low[i]))) all_digits = false;
        }
        if (all_digits) {
            const int fn = std::stoi(low.substr(1));
            if (fn >= 1 && fn <= 24) return "F" + std::to_string(fn);
        }
    }
    if (low == "bksp" || low == "backspace" || low == "back") return "Backspace";
    if (low == "del" || low == "delete") return "Delete";
    if (low == "ret" || low == "return" || low == "enter") return "Enter";
    if (low == "numpadenter" || low == "kp_enter") return "NumpadEnter";
    if (low == "tab") return "Tab";
    if (low == "esc" || low == "escape") return "Escape";
    if (low == "ins" || low == "insert") return "Insert";
    if (low == "space" || low == "spacebar") return "Space";
    if (low == "up" || low == "arrowup" || low == "arrow_up") return "Up";
    if (low == "down" || low == "arrowdown" || low == "arrow_down") return "Down";
    if (low == "left" || low == "arrowleft" || low == "arrow_left") return "Left";
    if (low == "right" || low == "arrowright" || low == "arrow_right") return "Right";
    if (low == "home") return "Home";
    if (low == "end") return "End";
    if (low == "pgup" || low == "pageup" || low == "page_up" || low == "prior") return "PageUp";
    if (low == "pgdn" || low == "pagedown" || low == "page_down" || low == "next") return "PageDown";
    if (low == "capslock" || low == "caps_lock") return "CapsLock";
    if (low == "numlock" || low == "num_lock") return "NumLock";
    if (low == "scrolllock" || low == "scroll_lock") return "ScrollLock";
    if (low == "plus" || low == "equal" || low == "equals") return "Plus";
    if (low == "minus" || low == "hyphen") return "Minus";
    if (low == "comma") return "Comma";
    if (low == "period" || low == "dot") return "Period";
    if (low == "semicolon") return "Semicolon";
    if (low == "slash") return "Slash";
    if (low == "grave" || low == "backtick" || low == "tilde") return "Grave";
    if (low == "bracketleft" || low == "lbracket") return "BracketLeft";
    if (low == "backslash") return "Backslash";
    if (low == "bracketright" || low == "rbracket") return "BracketRight";
    if (low == "quote" || low == "apostrophe") return "Quote";
    if (low.starts_with("vk_0x") || low.starts_with("vk:0x")) {
        try {
            const unsigned val = std::stoul(low.substr(5), nullptr, 16);
            if (val > 0 && val <= 0xFF) {
                return vkey_token_name(static_cast<uint16_t>(val), false);
            }
        } catch (...) {}
    }
    return raw;
}

} // namespace

void KeyboardInputRouter::apply_config(const AudioConfig& cfg) {
    config_ = AudioConfig::validated(cfg);
    if (!config_.keyboard_enabled) {
        reset_state();
    }
}

void KeyboardInputRouter::reset_state() {
    key_down_.fill(false);
    lctrl_down_ = false;
    rctrl_down_ = false;
    lalt_down_ = false;
    ralt_down_ = false;
    lshift_down_ = false;
    rshift_down_ = false;
    lwin_down_ = false;
    rwin_down_ = false;
    altgr_active_ = false;
    pending_modifier_tap_vk_ = 0;
    chord_used_while_mod_held_ = false;
}

std::string KeyboardInputRouter::format_chord(bool ctrl, bool alt, bool shift, bool win, uint16_t vkey, bool is_e0) {
    std::string out;
    if (ctrl)  out += "Ctrl+";
    if (alt)   out += "Alt+";
    if (shift) out += "Shift+";
    if (win)   out += "Win+";
    out += vkey_token_name(vkey, is_e0);
    return out;
}

std::string KeyboardInputRouter::normalize_chord_string(const std::string& raw_chord) {
    if (raw_chord.empty()) return {};
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool win = false;
    std::string key_part;

    std::size_t pos = 0;
    while (pos < raw_chord.size()) {
        std::size_t plus = raw_chord.find('+', pos);
        std::string token;
        if (plus == std::string::npos) {
            token = raw_chord.substr(pos);
            pos = raw_chord.size();
        } else if (plus == pos && pos + 1 == raw_chord.size()) {
            // Trailing '+' means the Plus key itself
            token = "+";
            pos = raw_chord.size();
        } else {
            token = raw_chord.substr(pos, plus - pos);
            pos = plus + 1;
        }
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        if (!token.empty()) {
            token.erase(token.find_last_not_of(" \t") + 1);
        }
        if (token.empty()) continue;

        const std::string low = to_lower_ascii(token);
        if (low == "ctrl" || low == "control" || low == "lctrl" || low == "rctrl") {
            ctrl = true;
        } else if (low == "alt" || low == "lalt" || low == "ralt" || low == "menu") {
            alt = true;
        } else if (low == "shift" || low == "lshift" || low == "rshift") {
            shift = true;
        } else if (low == "win" || low == "meta" || low == "super" || low == "lwin" || low == "rwin") {
            win = true;
        } else {
            key_part = canonicalize_key_token(token);
        }
    }
    if (key_part.empty() && raw_chord.back() == '+' && raw_chord.size() > 1) {
        // A trailing '+' is the Plus key itself, not a dangling separator: a
        // user binding "Ctrl+" means Ctrl plus the Plus key. Without this the
        // last '+' is eaten as a separator and the binding resolves to nothing.
        key_part = "Plus";
    }
    if (key_part.empty()) return {};
    std::string out;
    if (ctrl)  out += "Ctrl+";
    if (alt)   out += "Alt+";
    if (shift) out += "Shift+";
    if (win)   out += "Win+";
    out += key_part;
    return out;
}

std::string KeyboardInputRouter::match_named_shortcut(const std::string& chord) {
    // SELECTION
    if (chord == "Ctrl+A")           return "shortcut.select_all";
    if (chord == "Ctrl+Shift+Left")  return "shortcut.select_word_left";
    if (chord == "Ctrl+Shift+Right") return "shortcut.select_word_right";
    if (chord == "Shift+Up")         return "shortcut.select_line_up";
    if (chord == "Shift+Down")       return "shortcut.select_line_down";

    // CLIPBOARD
    if (chord == "Ctrl+C" || chord == "Ctrl+Insert")  return "shortcut.copy";
    if (chord == "Ctrl+V" || chord == "Shift+Insert") return "shortcut.paste";
    if (chord == "Ctrl+X")                            return "shortcut.cut";
    if (chord == "Ctrl+Shift+V")                      return "shortcut.paste_plain_text";

    // HISTORY
    if (chord == "Ctrl+Z")                            return "shortcut.undo";
    if (chord == "Ctrl+Y" || chord == "Ctrl+Shift+Z") return "shortcut.redo";

    // COMMON ACTIONS (Section 4)
    if (chord == "Ctrl+S")         return "shortcut.save";
    if (chord == "Ctrl+O")         return "shortcut.open";
    if (chord == "Ctrl+N")         return "shortcut.new";
    if (chord == "Ctrl+F")         return "shortcut.find";
    if (chord == "Ctrl+P")         return "shortcut.print";
    if (chord == "Ctrl+W")         return "shortcut.close";
    if (chord == "Alt+Tab" || chord == "Alt+Shift+Tab") return "shortcut.switch_window";
    if (chord == "Ctrl+Backspace") return "shortcut.delete_word_left";
    if (chord == "Ctrl+Delete")    return "shortcut.delete_word_right";
    if (chord == "Shift+Delete")   return "shortcut.permanent_delete";

    return {};
}

std::string KeyboardInputRouter::classify_specific_key(uint16_t vkey, uint16_t /*make_code*/, bool is_e0) {
    // Editing
    switch (vkey) {
        case VK_BACK:   return "key.backspace";
        case VK_DELETE: return "key.delete";
        case VK_RETURN: return is_e0 ? "numpad.enter" : "key.enter";
        case VK_TAB:    return "key.tab";
        case VK_ESCAPE: return "key.escape";
        case VK_INSERT: return "key.insert";

        // Navigation
        case VK_UP:     return "key.arrow_up";
        case VK_DOWN:   return "key.arrow_down";
        case VK_LEFT:   return "key.arrow_left";
        case VK_RIGHT:  return "key.arrow_right";
        case VK_HOME:   return "key.home";
        case VK_END:    return "key.end";
        case VK_PRIOR:  return "key.page_up";
        case VK_NEXT:   return "key.page_down";

        // Lock Modifiers
        case VK_CAPITAL: return "key.caps_lock";
        case VK_NUMLOCK: return "key.num_lock";
        case VK_SCROLL:  return "key.scroll_lock";

        // Numpad operators
        case VK_DECIMAL:  return "numpad.decimal";
        case VK_ADD:      return "numpad.add";
        case VK_SUBTRACT: return "numpad.subtract";
        case VK_MULTIPLY: return "numpad.multiply";
        case VK_DIVIDE:   return "numpad.divide";

        // Media
        case VK_VOLUME_UP:        return "media.volume_up";
        case VK_VOLUME_DOWN:      return "media.volume_down";
        case VK_VOLUME_MUTE:      return "media.mute";
        case VK_MEDIA_PLAY_PAUSE: return "media.play_pause";
        case VK_MEDIA_NEXT_TRACK: return "media.next";
        case VK_MEDIA_PREV_TRACK: return "media.previous";
        default: break;
    }

    if (vkey >= VK_F1 && vkey <= VK_F24) {
        return "key.f" + std::to_string(vkey - VK_F1 + 1);
    }
    if (vkey >= VK_NUMPAD0 && vkey <= VK_NUMPAD9) {
        return "numpad.digit";
    }
    return {};
}

std::string KeyboardInputRouter::classify_typing_category(uint16_t vkey, bool shift_down, bool /*is_e0*/) {
    if (vkey == VK_SPACE) {
        return "typing.space";
    }
    if (vkey >= 'A' && vkey <= 'Z') {
        return "typing.letter";
    }
    if (vkey >= '0' && vkey <= '9') {
        // Shift+Digit on standard layouts produces a symbol (!@#$%^&*())
        return shift_down ? "typing.symbol" : "typing.digit";
    }
    switch (vkey) {
        case VK_OEM_1:      // ;:
        case VK_OEM_PLUS:   // =+
        case VK_OEM_COMMA:  // ,<
        case VK_OEM_MINUS:  // -_
        case VK_OEM_PERIOD: // .>
        case VK_OEM_2:      // /?
        case VK_OEM_3:      // `~
        case VK_OEM_4:      // [{
        case VK_OEM_5:      // \|
        case VK_OEM_6:      // ]}
        case VK_OEM_7:      // '"
        case VK_OEM_8:
        case VK_OEM_102:    // ISO <>| key
            return "typing.symbol";
        default:
            break;
    }
    return {};
}

bool KeyboardInputRouter::has_configured_output(const std::string& event_id) const {
    const EventSoundConfig ec = config_.effective_event_config(event_id);
    if (!ec.enabled) return false;
    if (ec.use_pool) {
        for (const auto& entry : ec.pool) {
            if (entry.enabled && !entry.sound_id.empty()) return true;
        }
        return false;
    }
    return !ec.sound_id.empty();
}

ClassifiedKeyEvent KeyboardInputRouter::resolve_arbitration(
    const std::string& chord,
    uint16_t vkey,
    uint16_t make_code,
    bool is_e0,
    bool has_shortcut_mods,
    bool is_repeat) const {

    ClassifiedKeyEvent out{};
    out.is_repeat = is_repeat;

    // Priority 1: Explicit user-defined exact shortcut (!is_app_shortcut).
    for (const auto& cs : config_.custom_shortcuts) {
        if (!cs.enabled || cs.is_app_shortcut) continue;
        if (normalize_chord_string(cs.chord) == chord) {
            const EventSoundConfig ec = config_.effective_event_config(cs.id);
            if (!ec.enabled) {
                // Explicitly disabled shortcut suppresses lower fallbacks (no double sound).
                out.priority_tier = 7;
                return out;
            }
            out.event_id = cs.id;
            out.priority_tier = 1;
            out.is_shortcut = true;
            return out;
        }
    }

    // Priority 2: Registered exact application shortcut (is_app_shortcut).
    for (const auto& cs : config_.custom_shortcuts) {
        if (!cs.enabled || !cs.is_app_shortcut) continue;
        if (normalize_chord_string(cs.chord) == chord) {
            const EventSoundConfig ec = config_.effective_event_config(cs.id);
            if (!ec.enabled) {
                out.priority_tier = 7;
                return out;
            }
            out.event_id = cs.id;
            out.priority_tier = 2;
            out.is_shortcut = true;
            return out;
        }
    }

    // Priority 3: Named keyboard shortcut (Ctrl+C, Ctrl+V, Ctrl+Backspace, Shift+Delete, etc.).
    const std::string named_sc = match_named_shortcut(chord);
    if (!named_sc.empty()) {
        // Section 4: A single physical shortcut action (like Ctrl+C or Ctrl+Backspace)
        // must NEVER fall through to play Backspace or Generic Typing!
        const EventSoundConfig ec = config_.effective_event_config(named_sc);
        if (!ec.enabled) {
            out.priority_tier = 7;
            return out;
        }
        out.event_id = named_sc;
        out.priority_tier = 3;
        out.is_shortcut = true;
        return out;
    }

    // If Ctrl, Alt, or Win is held (and not AltGr), it is an unrecognized shortcut chord.
    // Never fall through to ordinary key or typing sound for Ctrl+<Key> / Alt+<Key> / Win+<Key>.
    if (has_shortcut_mods) {
        out.priority_tier = 7;
        return out;
    }

    // Priority 4: Specific individual key event (Backspace, Enter, Arrows, F1..F24, Numpad, Media, or custom per-key override).
    const std::string specific_id = classify_specific_key(vkey, make_code, is_e0);
    if (!specific_id.empty()) {
        const EventSoundConfig ec = config_.effective_event_config(specific_id);
        if (!ec.enabled) {
            out.priority_tier = 7;
            return out;
        }
        // If the specific key has a sound configured (or typing fallback isn't applicable), return it;
        // if it has no sound configured and is an editing key when typing_sounds_enabled is true,
        // allow fallback to typing.generic ONLY if typing.generic is configured.
        if (has_configured_output(specific_id) || !config_.typing_sounds_enabled) {
            out.event_id = specific_id;
            out.priority_tier = 4;
            return out;
        }
        // Check if typing.generic fallback applies for unassigned editing/numpad keys
        if (config_.typing_sounds_enabled && has_configured_output("typing.generic")) {
            out.event_id = "typing.generic";
            out.priority_tier = 6;
            return out;
        }
        out.event_id = specific_id;
        out.priority_tier = 4;
        return out;
    }

    // Routine typing keys (letters, digits, symbols, space) require typing_sounds_enabled.
    if (!config_.typing_sounds_enabled) {
        out.priority_tier = 7;
        return out;
    }

    if (is_repeat && config_.suppress_repeat_typing) {
        out.priority_tier = 7;
        return out;
    }

    // If it's a repeat and typing.repeat has a dedicated assignment, use typing.repeat.
    if (is_repeat && has_configured_output("typing.repeat")) {
        out.event_id = "typing.repeat";
        out.priority_tier = 5;
        return out;
    }

    // Priority 5: Generic typing category (typing.letter, typing.digit, typing.symbol, typing.space).
    const std::string typing_cat = classify_typing_category(vkey, shift_down(), is_e0);
    if (!typing_cat.empty()) {
        const EventSoundConfig cat_ec = config_.effective_event_config(typing_cat);
        if (!cat_ec.enabled) {
            // Explicitly disabled category (e.g. user muted Spacebar) does not fall back to generic typing.
            out.priority_tier = 7;
            return out;
        }
        if (has_configured_output(typing_cat)) {
            out.event_id = typing_cat;
            out.priority_tier = 5;
            return out;
        }
        // Priority 6: Category fallback -> typing.generic
        const EventSoundConfig gen_ec = config_.effective_event_config("typing.generic");
        if (gen_ec.enabled) {
            out.event_id = "typing.generic";
            out.priority_tier = 6;
            return out;
        }
    }

    // Priority 7: Silence.
    out.priority_tier = 7;
    return out;
}

ClassifiedKeyEvent KeyboardInputRouter::process_key(const RawKeyPacket& pkt, const ForegroundContext& ctx) {
    ClassifiedKeyEvent result{};

    // Detect foreground window change: clear stuck key/modifier state so a chord
    // started in another window never leaks across focus changes.
    if (ctx.window_handle_id != 0 && ctx.window_handle_id != last_foreground_hwnd_) {
        if (last_foreground_hwnd_ != 0) {
            reset_state();
        }
        last_foreground_hwnd_ = ctx.window_handle_id;
    }

    // Master & keyboard gates
    if (!config_.enabled || !config_.keyboard_enabled) {
        return result;
    }

    // Scope enforcement: AppOnly rejects non-ProTrail foreground contexts
    if (config_.keyboard_scope == KeyboardScope::AppOnly && !ctx.is_protrail_foreground) {
        return result;
    }

    // Process exclusions in SystemWide mode
    if (config_.keyboard_scope == KeyboardScope::SystemWide && !ctx.process_name.empty()) {
        const std::string proc_low = sanitize_process_name(ctx.process_name);
        for (const std::string& excl : config_.excluded_processes) {
            if (proc_low == excl) {
                return result;
            }
        }
    }

    // Conservative sensitive-context suppression (e.g. credential UI / password prompt)
    if (config_.suppress_sensitive_contexts && ctx.is_sensitive_context) {
        return result;
    }

    // Reject unsupported / bogus virtual key codes (0 or >= 255 / 0xFF OEM unassigned)
    if (pkt.vkey == 0 || pkt.vkey >= 0xFF || pkt.vkey == VK_PACKET) {
        return result;
    }
    // IME composition key (VK_PROCESSKEY = 0xE5): handle conservatively without claiming character knowledge
    if (pkt.vkey == VK_PROCESSKEY) {
        return result;
    }

    // AltGr synthetic LCtrl suppression: Windows keyboards with AltGr emit a fake
    // LCtrl (often flagged E1 or paired with RMENU). Ignore synthetic LCtrl packets.
    if (pkt.is_altgr_synthetic_lctrl) {
        altgr_active_ = pkt.is_down;
        return result;
    }

    // Normalize left/right modifier flags from VKey + E0
    uint16_t norm_vk = pkt.vkey;
    if (norm_vk == VK_CONTROL) {
        norm_vk = pkt.is_e0 ? VK_RCONTROL : VK_LCONTROL;
    } else if (norm_vk == VK_MENU) {
        norm_vk = pkt.is_e0 ? VK_RMENU : VK_LMENU;
    } else if (norm_vk == VK_SHIFT) {
        norm_vk = (pkt.make_code == 0x36) ? VK_RSHIFT : VK_LSHIFT;
    }

    const bool was_down = key_down_[norm_vk];
    key_down_[norm_vk] = pkt.is_down;

    // Update modifier state
    if (is_modifier_vk(norm_vk)) {
        switch (norm_vk) {
            case VK_LCONTROL: lctrl_down_ = pkt.is_down; break;
            case VK_RCONTROL: rctrl_down_ = pkt.is_down; break;
            case VK_LMENU:    lalt_down_  = pkt.is_down; break;
            case VK_RMENU:
                ralt_down_ = pkt.is_down;
                // On European/AltGr layouts, RAlt while LCtrl is down is AltGr.
                if (pkt.is_down && lctrl_down_) {
                    altgr_active_ = true;
                } else if (!pkt.is_down) {
                    altgr_active_ = false;
                }
                break;
            case VK_LSHIFT:   lshift_down_ = pkt.is_down; break;
            case VK_RSHIFT:   rshift_down_ = pkt.is_down; break;
            case VK_LWIN:     lwin_down_   = pkt.is_down; break;
            case VK_RWIN:     rwin_down_   = pkt.is_down; break;
            default: break;
        }

        if (pkt.is_down) {
            if (!was_down) {
                // Initial modifier press: arm isolated tap only if no other modifier was already used in a chord
                if (pending_modifier_tap_vk_ == 0 && !chord_used_while_mod_held_) {
                    pending_modifier_tap_vk_ = norm_vk;
                } else {
                    // Multiple modifiers pressed together (e.g. Ctrl+Shift): cancel isolated single-modifier tap
                    pending_modifier_tap_vk_ = 0;
                    chord_used_while_mod_held_ = true;
                }
            }
            // Never play modifier sounds on KeyDown or auto-repeat (prevents Ctrl/Shift spam during chords).
            return result;
        } else {
            // Modifier KeyUp: if this modifier was tapped in isolation and modifier_tap_sounds_enabled is ON
            const bool isolated_tap = (pending_modifier_tap_vk_ == norm_vk && !chord_used_while_mod_held_ && !altgr_active_);
            pending_modifier_tap_vk_ = 0;
            if (!ctrl_down() && !alt_down() && !shift_down() && !win_down()) {
                chord_used_while_mod_held_ = false;
            }
            if (isolated_tap && config_.modifier_tap_sounds_enabled) {
                const char* mod_ev = modifier_tap_event_id(norm_vk);
                if (mod_ev[0] != '\0') {
                    const EventSoundConfig ec = config_.effective_event_config(mod_ev);
                    if (ec.enabled) {
                        result.event_id = mod_ev;
                        result.priority_tier = 4;
                        result.is_modifier_tap = true;
                    }
                }
            }
            return result;
        }
    }

    // Non-modifier key: any non-modifier press cancels pending isolated modifier taps.
    if (pkt.is_down) {
        pending_modifier_tap_vk_ = 0;
        if (ctrl_down() || alt_down() || shift_down() || win_down()) {
            chord_used_while_mod_held_ = true;
        }
    } else {
        // Non-modifier KeyUp produces silence.
        return result;
    }

    const bool is_repeat = was_down;

    // Section 4: Handle AltGr (LCtrl + RAlt) so its Ctrl+Alt representation does NOT
    // accidentally invoke Ctrl+* or Ctrl+Alt+* shortcuts. When AltGr is active (or
    // LCtrl + RAlt are held without RCtrl/LAlt), treat it as layout character input (typing.symbol / letter).
    const bool is_altgr_chord = altgr_active_ || (lctrl_down_ && ralt_down_ && !rctrl_down_ && !lalt_down_);
    const bool effective_ctrl = is_altgr_chord ? false : ctrl_down();
    const bool effective_alt  = is_altgr_chord ? false : alt_down();
    const bool effective_shift = shift_down();
    const bool effective_win   = win_down();
    const bool has_shortcut_mods = effective_ctrl || effective_alt || effective_win;

    const std::string chord = format_chord(effective_ctrl, effective_alt, effective_shift, effective_win, norm_vk, pkt.is_e0);
    return resolve_arbitration(chord, norm_vk, pkt.make_code, pkt.is_e0, has_shortcut_mods, is_repeat);
}

// ============================================================================
// KeyboardInput (Windows Raw Input passive adapter)
// ============================================================================

KeyboardInput* KeyboardInput::s_instance_ = nullptr;

KeyboardInput::~KeyboardInput() {
    destroy();
}

LRESULT CALLBACK KeyboardInput::wnd_proc_static(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = s_instance_;
    if (msg == WM_INPUT && self) {
        self->handle_raw_input(lp);
        if (GET_RAWINPUT_CODE_WPARAM(wp) == RIM_INPUT) {
            DefWindowProcW(hwnd, msg, wp, lp);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool KeyboardInput::create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &KeyboardInput::wnd_proc_static;
    wc.hInstance = instance;
    wc.lpszClassName = kKeyboardClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        ptd::log_write(ptd::LogLevel::Error, "keyboard_input: RegisterClassExW failed");
        return false;
    }

    hwnd_ = CreateWindowExW(0, kKeyboardClassName, L"ProTrailKeyboardInput", WS_OVERLAPPED,
                            0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
    if (!hwnd_) {
        ptd::log_write(ptd::LogLevel::Error, "keyboard_input: CreateWindowExW failed");
        return false;
    }
    s_instance_ = this;
    return true;
}

void KeyboardInput::destroy() {
    update_sink_registration(false);
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (s_instance_ == this) {
        s_instance_ = nullptr;
    }
    callback_ = nullptr;
    router_.reset_state();
}

bool KeyboardInput::update_sink_registration(bool want_system_wide) {
    if (want_system_wide && !registered_) {
        if (!hwnd_) return false;
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
        rid.usUsage = 0x06;     // HID_USAGE_GENERIC_KEYBOARD
        rid.dwFlags = RIDEV_INPUTSINK;
        rid.hwndTarget = hwnd_;
        if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
            ptd::log_write(ptd::LogLevel::Warn,
                           "keyboard_input: RegisterRawInputDevices failed; failing silent");
            return false;
        }
        registered_ = true;
        ptd::log_write(ptd::LogLevel::Info,
                       "keyboard_input: passive system-wide raw input sink registered");
        return true;
    }
    if (!want_system_wide && registered_) {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x06;
        rid.dwFlags = RIDEV_REMOVE;
        rid.hwndTarget = nullptr;
        RegisterRawInputDevices(&rid, 1, sizeof(rid));
        registered_ = false;
        router_.reset_state();
        ptd::log_write(ptd::LogLevel::Info,
                       "keyboard_input: passive system-wide raw input sink unregistered");
    }
    return true;
}

void KeyboardInput::apply_config(const AudioConfig& cfg) {
    router_.apply_config(cfg);
    const bool want_global = cfg.enabled && cfg.keyboard_enabled &&
                             (cfg.keyboard_scope == KeyboardScope::SystemWide);
    update_sink_registration(want_global);
}

ForegroundContext KeyboardInput::query_foreground_context() {
    ForegroundContext ctx{};
    HWND fg = GetForegroundWindow();
    if (!fg) return ctx;
    ctx.window_handle_id = reinterpret_cast<uintptr_t>(fg);

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) {
        ctx.is_protrail_foreground = true;
    }

    if (pid != 0) {
        HANDLE hproc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hproc) {
            wchar_t path_buf[MAX_PATH]{};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hproc, 0, path_buf, &size) && size > 0) {
                const wchar_t* base = wcsrchr(path_buf, L'\\');
                const wchar_t* name = base ? (base + 1) : path_buf;
                std::string narrow;
                for (const wchar_t* p = name; *p != L'\0'; ++p) {
                    if (*p < 0x80) {
                        narrow.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*p))));
                    }
                }
                ctx.process_name = std::move(narrow);
            }
            CloseHandle(hproc);
        }
    }

    // Conservative sensitive-context detection (Section 3):
    // Check known credential/security process names or password dialog class names
    // without ever reading window text or edit control contents.
    if (ctx.process_name == "credentialuibroker.exe" ||
        ctx.process_name == "consent.exe" ||
        ctx.process_name == "keepass.exe" ||
        ctx.process_name == "keepassxc.exe" ||
        ctx.process_name == "1password.exe" ||
        ctx.process_name == "bitwarden.exe") {
        ctx.is_sensitive_context = true;
    } else {
        wchar_t cls_buf[64]{};
        if (GetClassNameW(fg, cls_buf, 64) > 0) {
            if (wcscmp(cls_buf, L"Credential Dialog Xaml Host") == 0) {
                ctx.is_sensitive_context = true;
            }
        }
    }
    return ctx;
}

void KeyboardInput::handle_raw_input(LPARAM lp) {
    if (!registered_) return;
    UINT size = 0;
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, nullptr, &size,
                        sizeof(RAWINPUTHEADER)) != 0 || size == 0 || size > 512) {
        return;
    }
    BYTE buf[512];
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lp), RID_INPUT, buf, &size,
                        sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) {
        return;
    }
    const auto* raw = reinterpret_cast<const RAWINPUT*>(buf);
    if (raw->header.dwType != RIM_TYPEKEYBOARD) return;
    const RAWKEYBOARD& kb = raw->data.keyboard;

    // Ignore overrun placeholder
    if (kb.VKey == 0xFF) return;

    RawKeyPacket pkt{};
    pkt.vkey = kb.VKey;
    pkt.make_code = kb.MakeCode;
    pkt.flags = kb.Flags;
    pkt.is_down = (kb.Flags & RI_KEY_BREAK) == 0;
    pkt.is_e0 = (kb.Flags & RI_KEY_E0) != 0;
    pkt.is_e1 = (kb.Flags & RI_KEY_E1) != 0;
    // AltGr fake LCtrl on Windows Raw Input carries MakeCode 0x1D with RI_KEY_E1 or paired E0 RMENU
    if (kb.VKey == VK_CONTROL && pkt.is_e1) {
        pkt.is_altgr_synthetic_lctrl = true;
    }
    pkt.timestamp_ns = now_ns();
    ++packet_count_;

    const ForegroundContext ctx = query_foreground_context();
    const ClassifiedKeyEvent ev = router_.process_key(pkt, ctx);
    if (!ev.event_id.empty() && callback_) {
        ++emitted_count_;
        callback_(pkt.timestamp_ns, ev);
    }
}

void KeyboardInput::handle_local_key_event(uint16_t vkey, uint16_t scan_code, bool is_extended, bool is_down, int64_t timestamp_ns) {
    // If global sink is active, WM_INPUT already delivers ProTrail-local keys too;
    // avoid double-delivering the same key press.
    if (registered_) return;

    RawKeyPacket pkt{};
    pkt.vkey = vkey;
    pkt.make_code = scan_code;
    pkt.is_down = is_down;
    pkt.is_e0 = is_extended;
    pkt.flags = static_cast<uint16_t>((is_down ? 0 : RI_KEY_BREAK) | (is_extended ? RI_KEY_E0 : 0));
    pkt.timestamp_ns = timestamp_ns;
    ++packet_count_;

    ForegroundContext ctx{};
    ctx.is_protrail_foreground = true;
    ctx.process_name = "protrail.exe";
    ctx.window_handle_id = 1;

    const ClassifiedKeyEvent ev = router_.process_key(pkt, ctx);
    if (!ev.event_id.empty() && callback_) {
        ++emitted_count_;
        callback_(timestamp_ns, ev);
    }
}

} // namespace ptd
