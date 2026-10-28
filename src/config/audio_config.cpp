#include "audio_config.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace ptd {
namespace {

// The persisted legacy key family. A renamed key breaks old configurations, so this
// list is append-only and its indices are the AudioEvent values.
constexpr const char* kEventNames[] = {
    "left_down",   "left_up",
    "right_down",  "right_up",
    "middle_down", "middle_up",
    "scroll_up",   "scroll_down",
};
static_assert(std::size(kEventNames) == kAudioEventCount,
              "audio event key table must cover every AudioEvent");

constexpr const char* kCanonicalMouseEventNames[] = {
    "mouse.left_down",   "mouse.left_up",
    "mouse.right_down",  "mouse.right_up",
    "mouse.middle_down", "mouse.middle_up",
    "mouse.scroll_up",   "mouse.scroll_down",
};
static_assert(std::size(kCanonicalMouseEventNames) == kAudioEventCount,
              "canonical mouse event key table must cover every AudioEvent");

constexpr bool is_valid_name_char(unsigned char c) {
    // Printable, no path/qualifier separator, no Windows wildcard or control.
    if (c < 0x20 || c == 0x7F) return false;
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?') return false;
    if (c == '"' || c == '<' || c == '>' || c == '|') return false;
    return true;
}

bool has_wav_extension(const std::string& id) {
    if (id.size() < 5) return false;
    const std::size_t dot = id.size() - 4;
    if (id[dot - 1] == ' ') return false;
    const std::string ext = id.substr(id.size() - 4);
    std::string lower;
    lower.resize(ext.size());
    std::transform(ext.begin(), ext.end(), lower.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return lower == ".wav";
}

float safe_float(float v, float lo, float hi, float def) {
    if (v != v) return def; // NaN
    return std::clamp(v, lo, hi);
}

std::string to_lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

std::vector<SoundEventDescriptor> build_canonical_registry() {
    using Cat = SoundEventCategory;
    using Pol = PlaybackPolicy;
    using Scp = InterruptScope;

    std::vector<SoundEventDescriptor> v = {
        // TYPING
        {"typing.generic",     "Generic Typing",     Cat::Typing, Pol::Stack,    Scp::SameGroup, 15, 30, 6, true},
        {"typing.letter",      "Letter Key",         Cat::Typing, Pol::Stack,    Scp::SameGroup, 15, 35, 6, true},
        {"typing.digit",       "Digit Key",          Cat::Typing, Pol::Stack,    Scp::SameGroup, 15, 35, 6, true},
        {"typing.symbol",      "Symbol / Punctuation", Cat::Typing, Pol::Stack,  Scp::SameGroup, 15, 35, 6, true},
        {"typing.space",       "Spacebar",           Cat::Typing, Pol::Override, Scp::SameEvent, 20, 40, 2, true},
        {"typing.repeat",      "Key Auto-Repeat",    Cat::Typing, Pol::Coalesce, Scp::SameEvent, 35, 25, 2, true},

        // EDITING
        {"key.backspace",      "Backspace",          Cat::Editing, Pol::Override, Scp::SameEvent, 20, 45, 2, true},
        {"key.delete",         "Delete",             Cat::Editing, Pol::Override, Scp::SameEvent, 20, 45, 2, true},
        {"key.enter",          "Enter / Return",     Cat::Editing, Pol::Override, Scp::SameEvent, 25, 50, 2, true},
        {"key.tab",            "Tab",                Cat::Editing, Pol::Override, Scp::SameEvent, 20, 45, 2, true},
        {"key.escape",         "Escape",             Cat::Editing, Pol::Override, Scp::SameEvent, 25, 55, 2, true},
        {"key.insert",         "Insert",             Cat::Editing, Pol::Override, Scp::SameEvent, 25, 45, 2, true},

        // NAVIGATION
        {"key.arrow_up",       "Arrow Up",           Cat::Navigation, Pol::Coalesce, Scp::SameEvent, 25, 40, 2, true},
        {"key.arrow_down",     "Arrow Down",         Cat::Navigation, Pol::Coalesce, Scp::SameEvent, 25, 40, 2, true},
        {"key.arrow_left",     "Arrow Left",         Cat::Navigation, Pol::Coalesce, Scp::SameEvent, 25, 40, 2, true},
        {"key.arrow_right",    "Arrow Right",        Cat::Navigation, Pol::Coalesce, Scp::SameEvent, 25, 40, 2, true},
        {"key.home",           "Home",               Cat::Navigation, Pol::Override, Scp::SameEvent, 25, 45, 2, true},
        {"key.end",            "End",                Cat::Navigation, Pol::Override, Scp::SameEvent, 25, 45, 2, true},
        {"key.page_up",        "Page Up",            Cat::Navigation, Pol::Override, Scp::SameEvent, 25, 45, 2, true},
        {"key.page_down",      "Page Down",          Cat::Navigation, Pol::Override, Scp::SameEvent, 25, 45, 2, true},

        // SELECTION
        {"shortcut.select_all",        "Select All (Ctrl+A)",             Cat::Selection, Pol::Override, Scp::SameGroup, 40, 70, 2, true},
        {"shortcut.select_word_left",  "Select Word Left (Ctrl+Shift+Left)",  Cat::Selection, Pol::Coalesce, Scp::SameEvent, 30, 65, 2, true},
        {"shortcut.select_word_right", "Select Word Right (Ctrl+Shift+Right)", Cat::Selection, Pol::Coalesce, Scp::SameEvent, 30, 65, 2, true},
        {"shortcut.select_line_up",    "Select Line Up (Shift+Up)",       Cat::Selection, Pol::Coalesce, Scp::SameEvent, 30, 65, 2, true},
        {"shortcut.select_line_down",  "Select Line Down (Shift+Down)",   Cat::Selection, Pol::Coalesce, Scp::SameEvent, 30, 65, 2, true},

        // CLIPBOARD
        {"shortcut.copy",              "Copy (Ctrl+C)",                   Cat::Clipboard, Pol::Override, Scp::SameGroup, 40, 75, 2, true},
        {"shortcut.paste",             "Paste (Ctrl+V)",                  Cat::Clipboard, Pol::Override, Scp::SameGroup, 40, 75, 2, true},
        {"shortcut.cut",               "Cut (Ctrl+X)",                    Cat::Clipboard, Pol::Override, Scp::SameGroup, 40, 75, 2, true},
        {"shortcut.paste_plain_text",  "Paste Plain Text (Ctrl+Shift+V)", Cat::Clipboard, Pol::Override, Scp::SameGroup, 40, 75, 2, true},

        // HISTORY
        {"shortcut.undo",              "Undo (Ctrl+Z)",                   Cat::History, Pol::Override, Scp::SameGroup, 35, 75, 2, true},
        {"shortcut.redo",              "Redo (Ctrl+Y / Ctrl+Shift+Z)",    Cat::History, Pol::Override, Scp::SameGroup, 35, 75, 2, true},

        // COMMON ACTIONS
        {"shortcut.save",              "Save (Ctrl+S)",                   Cat::CommonActions, Pol::Override, Scp::SameGroup, 50, 80, 2, true},
        {"shortcut.open",              "Open (Ctrl+O)",                   Cat::CommonActions, Pol::Override, Scp::SameGroup, 50, 80, 2, true},
        {"shortcut.new",               "New (Ctrl+N)",                    Cat::CommonActions, Pol::Override, Scp::SameGroup, 50, 80, 2, true},
        {"shortcut.find",              "Find (Ctrl+F)",                   Cat::CommonActions, Pol::Override, Scp::SameGroup, 50, 75, 2, true},
        {"shortcut.print",             "Print (Ctrl+P)",                  Cat::CommonActions, Pol::Override, Scp::SameGroup, 50, 75, 2, true},
        {"shortcut.close",             "Close Tab/Window (Ctrl+W)",       Cat::CommonActions, Pol::Override, Scp::SameGroup, 50, 75, 2, true},
        {"shortcut.switch_window",     "Switch Window (Alt+Tab)",         Cat::CommonActions, Pol::Override, Scp::SameGroup, 60, 70, 2, false},
        {"shortcut.delete_word_left",  "Delete Word Left (Ctrl+Backspace)", Cat::CommonActions, Pol::Override, Scp::SameGroup, 35, 70, 2, true},
        {"shortcut.delete_word_right", "Delete Word Right (Ctrl+Delete)", Cat::CommonActions, Pol::Override, Scp::SameGroup, 35, 70, 2, true},
        {"shortcut.permanent_delete",  "Permanent Delete (Shift+Delete)", Cat::CommonActions, Pol::Override, Scp::SameGroup, 40, 75, 2, true},

        // FUNCTION KEYS (F1..F24)
        {"key.f1",  "F1",  Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f2",  "F2",  Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f3",  "F3",  Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f4",  "F4",  Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f5",  "F5",  Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f6",  "F6",  Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f7",  "F7",  Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f8",  "F8",  Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f9",  "F9",  Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f10", "F10", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f11", "F11", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f12", "F12", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, true},
        {"key.f13", "F13", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f14", "F14", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f15", "F15", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f16", "F16", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f17", "F17", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f18", "F18", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f19", "F19", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f20", "F20", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f21", "F21", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f22", "F22", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f23", "F23", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},
        {"key.f24", "F24", Cat::FunctionKeys, Pol::Override, Scp::SameEvent, 30, 50, 2, false},

        // MODIFIERS
        {"key.shift",       "Shift (Isolated Tap)", Cat::Modifiers, Pol::Override, Scp::SameEvent, 40, 35, 2, true},
        {"key.ctrl",        "Ctrl (Isolated Tap)",  Cat::Modifiers, Pol::Override, Scp::SameEvent, 40, 35, 2, true},
        {"key.alt",         "Alt (Isolated Tap)",   Cat::Modifiers, Pol::Override, Scp::SameEvent, 40, 35, 2, true},
        {"key.win",         "Win (Isolated Tap)",   Cat::Modifiers, Pol::Override, Scp::SameEvent, 40, 35, 2, false},
        {"key.caps_lock",   "Caps Lock",            Cat::Modifiers, Pol::Override, Scp::SameEvent, 40, 50, 2, true},
        {"key.num_lock",    "Num Lock",             Cat::Modifiers, Pol::Override, Scp::SameEvent, 40, 50, 2, true},
        {"key.scroll_lock", "Scroll Lock",          Cat::Modifiers, Pol::Override, Scp::SameEvent, 40, 50, 2, true},

        // NUMPAD
        {"numpad.digit",    "Numpad Digit (0-9)",   Cat::Numpad, Pol::Stack,    Scp::SameGroup, 15, 40, 4, true},
        {"numpad.decimal",  "Numpad Decimal",       Cat::Numpad, Pol::Override, Scp::SameEvent, 20, 40, 2, true},
        {"numpad.enter",    "Numpad Enter",         Cat::Numpad, Pol::Override, Scp::SameEvent, 25, 50, 2, true},
        {"numpad.add",      "Numpad Add (+)",       Cat::Numpad, Pol::Override, Scp::SameEvent, 20, 40, 2, true},
        {"numpad.subtract", "Numpad Subtract (-)",  Cat::Numpad, Pol::Override, Scp::SameEvent, 20, 40, 2, true},
        {"numpad.multiply", "Numpad Multiply (*)",  Cat::Numpad, Pol::Override, Scp::SameEvent, 20, 40, 2, true},
        {"numpad.divide",   "Numpad Divide (/)",    Cat::Numpad, Pol::Override, Scp::SameEvent, 20, 40, 2, true},

        // MEDIA
        {"media.volume_up",   "Volume Up",          Cat::Media, Pol::Coalesce, Scp::SameEvent, 35, 45, 2, false},
        {"media.volume_down", "Volume Down",        Cat::Media, Pol::Coalesce, Scp::SameEvent, 35, 45, 2, false},
        {"media.mute",        "Volume Mute",        Cat::Media, Pol::Override, Scp::SameEvent, 50, 55, 2, false},
        {"media.play_pause",  "Media Play/Pause",   Cat::Media, Pol::Override, Scp::SameEvent, 50, 55, 2, false},
        {"media.next",        "Media Next Track",   Cat::Media, Pol::Override, Scp::SameEvent, 50, 55, 2, false},
        {"media.previous",    "Media Prev Track",   Cat::Media, Pol::Override, Scp::SameEvent, 50, 55, 2, false},

        // MOUSE
        {"mouse.left_down",    "Left Button Down",   Cat::Mouse, Pol::Override, Scp::SameEvent, 0,  60, 2, true},
        {"mouse.left_up",      "Left Button Up",     Cat::Mouse, Pol::Override, Scp::SameEvent, 0,  55, 2, true},
        {"mouse.right_down",   "Right Button Down",  Cat::Mouse, Pol::Override, Scp::SameEvent, 0,  60, 2, true},
        {"mouse.right_up",     "Right Button Up",    Cat::Mouse, Pol::Override, Scp::SameEvent, 0,  55, 2, true},
        {"mouse.middle_down",  "Middle Button Down", Cat::Mouse, Pol::Override, Scp::SameEvent, 0,  60, 2, true},
        {"mouse.middle_up",    "Middle Button Up",   Cat::Mouse, Pol::Override, Scp::SameEvent, 0,  55, 2, true},
        {"mouse.scroll_up",    "Scroll Up",          Cat::Mouse, Pol::Coalesce, Scp::SameEvent, 20, 45, 2, true},
        {"mouse.scroll_down",  "Scroll Down",        Cat::Mouse, Pol::Coalesce, Scp::SameEvent, 20, 45, 2, true},
        {"mouse.scroll_left",  "Scroll Left",        Cat::Mouse, Pol::Coalesce, Scp::SameEvent, 20, 45, 2, true},
        {"mouse.scroll_right", "Scroll Right",       Cat::Mouse, Pol::Coalesce, Scp::SameEvent, 20, 45, 2, true},
        {"mouse.x1_down",      "XButton1 (Back) Down",    Cat::Mouse, Pol::Override, Scp::SameEvent, 0, 55, 2, true},
        {"mouse.x1_up",        "XButton1 (Back) Up",      Cat::Mouse, Pol::Override, Scp::SameEvent, 0, 50, 2, true},
        {"mouse.x2_down",      "XButton2 (Forward) Down", Cat::Mouse, Pol::Override, Scp::SameEvent, 0, 55, 2, true},
        {"mouse.x2_up",        "XButton2 (Forward) Up",   Cat::Mouse, Pol::Override, Scp::SameEvent, 0, 50, 2, true},
    };
    return v;
}

} // namespace

const char* audio_event_name(int index) {
    if (index < 0 || index >= kAudioEventCount) return "";
    return kEventNames[index];
}

const char* sound_event_category_id(SoundEventCategory cat) {
    switch (cat) {
        case SoundEventCategory::Typing:        return "typing";
        case SoundEventCategory::Editing:       return "editing";
        case SoundEventCategory::Navigation:    return "navigation";
        case SoundEventCategory::Selection:     return "selection";
        case SoundEventCategory::Clipboard:     return "clipboard";
        case SoundEventCategory::History:       return "history";
        case SoundEventCategory::CommonActions: return "common_actions";
        case SoundEventCategory::FunctionKeys:  return "function_keys";
        case SoundEventCategory::Modifiers:     return "modifiers";
        case SoundEventCategory::Numpad:        return "numpad";
        case SoundEventCategory::Media:         return "media";
        case SoundEventCategory::Mouse:         return "mouse";
        case SoundEventCategory::Custom:        return "custom";
        default:                                return "typing";
    }
}

const char* sound_event_category_label(SoundEventCategory cat) {
    switch (cat) {
        case SoundEventCategory::Typing:        return "Typing";
        case SoundEventCategory::Editing:       return "Editing";
        case SoundEventCategory::Navigation:    return "Navigation";
        case SoundEventCategory::Selection:     return "Selection";
        case SoundEventCategory::Clipboard:     return "Clipboard";
        case SoundEventCategory::History:       return "History";
        case SoundEventCategory::CommonActions: return "Common Actions";
        case SoundEventCategory::FunctionKeys:  return "Function Keys";
        case SoundEventCategory::Modifiers:     return "Modifiers";
        case SoundEventCategory::Numpad:        return "Numpad";
        case SoundEventCategory::Media:         return "Media";
        case SoundEventCategory::Mouse:         return "Mouse & Scroll";
        case SoundEventCategory::Custom:        return "Custom Shortcuts & Keys";
        default:                                return "Typing";
    }
}

const char* playback_policy_name(PlaybackPolicy p) {
    switch (p) {
        case PlaybackPolicy::Inherit:  return "inherit";
        case PlaybackPolicy::Stack:    return "stack";
        case PlaybackPolicy::Override: return "override";
        case PlaybackPolicy::Cut:      return "cut";
        case PlaybackPolicy::Queue:    return "queue";
        case PlaybackPolicy::Coalesce: return "coalesce";
    }
    return "inherit";
}

PlaybackPolicy parse_playback_policy(const std::string& s, PlaybackPolicy fallback) {
    const std::string low = to_lower_ascii(s);
    if (low == "inherit")  return PlaybackPolicy::Inherit;
    if (low == "stack")    return PlaybackPolicy::Stack;
    if (low == "override") return PlaybackPolicy::Override;
    if (low == "cut")      return PlaybackPolicy::Cut;
    if (low == "queue")    return PlaybackPolicy::Queue;
    if (low == "coalesce") return PlaybackPolicy::Coalesce;
    return fallback;
}

const char* interrupt_scope_name(InterruptScope s) {
    switch (s) {
        case InterruptScope::Inherit:   return "inherit";
        case InterruptScope::SameEvent: return "same_event";
        case InterruptScope::SameGroup: return "same_group";
        case InterruptScope::Global:    return "global";
    }
    return "inherit";
}

InterruptScope parse_interrupt_scope(const std::string& s, InterruptScope fallback) {
    const std::string low = to_lower_ascii(s);
    if (low == "inherit")    return InterruptScope::Inherit;
    if (low == "same_event") return InterruptScope::SameEvent;
    if (low == "same_group") return InterruptScope::SameGroup;
    if (low == "global")     return InterruptScope::Global;
    return fallback;
}

const char* keyboard_scope_name(KeyboardScope s) {
    switch (s) {
        case KeyboardScope::AppOnly:    return "app_only";
        case KeyboardScope::SystemWide: return "system_wide";
    }
    return "app_only";
}

KeyboardScope parse_keyboard_scope(const std::string& s) {
    const std::string low = to_lower_ascii(s);
    if (low == "system_wide") return KeyboardScope::SystemWide;
    return KeyboardScope::AppOnly;
}

const char* sound_profile_preset_name(SoundProfilePreset p) {
    switch (p) {
        case SoundProfilePreset::Default:         return "default";
        case SoundProfilePreset::Minimal:         return "minimal";
        case SoundProfilePreset::KeyboardFocused: return "keyboard_focused";
        case SoundProfilePreset::MouseFocused:    return "mouse_focused";
        case SoundProfilePreset::Quiet:           return "quiet";
        case SoundProfilePreset::Custom:          return "custom";
    }
    return "default";
}

SoundProfilePreset parse_sound_profile_preset(const std::string& s) {
    const std::string low = to_lower_ascii(s);
    if (low == "minimal")          return SoundProfilePreset::Minimal;
    if (low == "keyboard_focused") return SoundProfilePreset::KeyboardFocused;
    if (low == "mouse_focused")    return SoundProfilePreset::MouseFocused;
    if (low == "quiet")            return SoundProfilePreset::Quiet;
    if (low == "custom")           return SoundProfilePreset::Custom;
    return SoundProfilePreset::Default;
}

const std::vector<SoundEventDescriptor>& canonical_sound_events() {
    static const std::vector<SoundEventDescriptor> registry = build_canonical_registry();
    return registry;
}

int legacy_mouse_event_index(const std::string& event_id) {
    for (int i = 0; i < kAudioEventCount; ++i) {
        if (event_id == kEventNames[i] || event_id == kCanonicalMouseEventNames[i]) {
            return i;
        }
    }
    return -1;
}

std::string canonicalize_sound_event_id(const std::string& raw_id) {
    if (raw_id.empty()) return {};
    const int legacy_idx = legacy_mouse_event_index(raw_id);
    if (legacy_idx >= 0) {
        return kCanonicalMouseEventNames[legacy_idx];
    }
    if (!is_valid_event_id(raw_id)) return {};
    return raw_id;
}

const SoundEventDescriptor* find_sound_event_descriptor(const std::string& event_id) {
    const std::string cid = canonicalize_sound_event_id(event_id);
    if (cid.empty()) return nullptr;
    const auto& reg = canonical_sound_events();
    for (const auto& d : reg) {
        if (cid == d.id) return &d;
    }
    return nullptr;
}

bool is_valid_event_id(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    // Canonical ids exist in registry, or custom/key ids match [a-z0-9_]+(\.[a-z0-9_]+)+
    for (int i = 0; i < kAudioEventCount; ++i) {
        if (id == kEventNames[i] || id == kCanonicalMouseEventNames[i]) return true;
    }
    const auto& reg = canonical_sound_events();
    for (const auto& d : reg) {
        if (id == d.id) return true;
    }
    // Check custom.* or key.* syntax
    if (!(id.starts_with("custom.") || id.starts_with("key.") || id.starts_with("shortcut."))) {
        return false;
    }
    bool prev_dot = false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(id[i]);
        if (c == '.') {
            if (i == 0 || i + 1 == id.size() || prev_dot) return false;
            prev_dot = true;
            continue;
        }
        prev_dot = false;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return true;
}

EventSoundConfig EventSoundConfig::validated(const EventSoundConfig& in) {
    EventSoundConfig out = in;
    out.enabled = in.enabled;
    out.sound_id = sanitize_sound_id(in.sound_id);
    out.use_pool = in.use_pool;
    out.no_immediate_repeat = in.no_immediate_repeat;

    out.pool.clear();
    for (const auto& entry : in.pool) {
        if (static_cast<int>(out.pool.size()) >= kMaxPoolEntries) break;
        const std::string clean_id = sanitize_sound_id(entry.sound_id);
        if (clean_id.empty()) continue;
        SoundPoolEntry clean_entry;
        clean_entry.enabled = entry.enabled;
        clean_entry.sound_id = clean_id;
        clean_entry.weight = safe_float(entry.weight, 0.01f, 100.0f, 1.0f);
        clean_entry.gain_db = safe_float(entry.gain_db, kMinGainDb, kMaxGainDb, 0.0f);
        out.pool.push_back(std::move(clean_entry));
    }

    out.gain_db = safe_float(in.gain_db, kMinGainDb, kMaxGainDb, 0.0f);
    const int pol = static_cast<int>(in.policy);
    out.policy = (pol >= 0 && pol <= static_cast<int>(PlaybackPolicy::Coalesce))
                     ? in.policy
                     : PlaybackPolicy::Inherit;
    const int scp = static_cast<int>(in.interrupt_scope);
    out.interrupt_scope = (scp >= 0 && scp <= static_cast<int>(InterruptScope::Global))
                              ? in.interrupt_scope
                              : InterruptScope::Inherit;

    out.cooldown_ms = std::clamp(in.cooldown_ms, 0, kMaxCooldownMs);
    out.priority = std::clamp(in.priority, kMinPriority, kMaxPriority);
    out.fade_in_ms = std::clamp(in.fade_in_ms, 0, kMaxFadeMs);
    out.fade_out_ms = std::clamp(in.fade_out_ms, 0, kMaxFadeMs);
    out.min_retrigger_ms = std::clamp(in.min_retrigger_ms, 0, kMaxCooldownMs);
    out.polyphony_allowance = std::clamp(in.polyphony_allowance, kMinPolyphony, kMaxPolyphony);
    out.pitch_variation = safe_float(in.pitch_variation, 0.0f, kMaxPitchVariation, 0.0f);
    out.normalize_override = in.normalize_override;
    out.normalize_enabled = in.normalize_enabled;
    return out;
}

bool EventSoundConfig::is_default_unassigned() const {
    return enabled && sound_id.empty() && !use_pool && pool.empty()
        && no_immediate_repeat && std::abs(gain_db) < 0.0001f
        && policy == PlaybackPolicy::Inherit
        && interrupt_scope == InterruptScope::Inherit
        && cooldown_ms == 0 && priority == 50
        && fade_in_ms == 0 && fade_out_ms == 0 && min_retrigger_ms == 0
        && polyphony_allowance == 4 && std::abs(pitch_variation) < 0.0001f
        && !normalize_override && !normalize_enabled;
}

CustomShortcutBinding CustomShortcutBinding::validated(const CustomShortcutBinding& in) {
    CustomShortcutBinding out;
    out.id = is_valid_event_id(in.id) ? in.id : std::string{};
    out.display_name = in.display_name.substr(0, 64);
    // Strip control chars from display_name and chord
    out.display_name.erase(
        std::remove_if(out.display_name.begin(), out.display_name.end(),
                       [](unsigned char c) { return c < 0x20 || c == 0x7F; }),
        out.display_name.end());
    out.chord = in.chord.substr(0, 64);
    out.chord.erase(
        std::remove_if(out.chord.begin(), out.chord.end(),
                       [](unsigned char c) { return c <= 0x20 || c >= 0x7F; }),
        out.chord.end());
    out.is_app_shortcut = in.is_app_shortcut;
    out.enabled = in.enabled;
    return out;
}

EventSoundConfig AudioConfig::effective_event_config(const std::string& event_id) const {
    const std::string cid = canonicalize_sound_event_id(event_id);
    if (cid.empty()) return EventSoundConfig{};

    auto it = event_configs.find(cid);
    if (it != event_configs.end()) {
        EventSoundConfig cfg = EventSoundConfig::validated(it->second);
        const int legacy_idx = legacy_mouse_event_index(cid);
        if (legacy_idx >= 0 && cfg.sound_id.empty() && !sounds[static_cast<std::size_t>(legacy_idx)].empty()) {
            cfg.sound_id = sanitize_sound_id(sounds[static_cast<std::size_t>(legacy_idx)]);
        }
        return cfg;
    }

    EventSoundConfig cfg{};
    const int legacy_idx = legacy_mouse_event_index(cid);
    if (legacy_idx >= 0) {
        cfg.sound_id = sanitize_sound_id(sounds[static_cast<std::size_t>(legacy_idx)]);
    }
    return cfg;
}

void AudioConfig::set_event_config(const std::string& event_id, const EventSoundConfig& cfg) {
    const std::string cid = canonicalize_sound_event_id(event_id);
    if (cid.empty()) return;
    const EventSoundConfig clean = EventSoundConfig::validated(cfg);
    const int legacy_idx = legacy_mouse_event_index(cid);
    if (legacy_idx >= 0) {
        sounds[static_cast<std::size_t>(legacy_idx)] = clean.sound_id;
        EventSoundConfig stripped = clean;
        stripped.sound_id.clear();
        if (stripped.is_default_unassigned()) {
            event_configs.erase(cid);
        } else {
            event_configs[cid] = clean;
        }
        return;
    }
    if (clean.is_default_unassigned()) {
        event_configs.erase(cid);
    } else {
        event_configs[cid] = clean;
    }
}

void AudioConfig::reset_category(SoundEventCategory cat) {
    if (cat == SoundEventCategory::Custom) {
        for (auto it = event_configs.begin(); it != event_configs.end();) {
            if (find_sound_event_descriptor(it->first) == nullptr) {
                it = event_configs.erase(it);
            } else {
                ++it;
            }
        }
        return;
    }
    for (const auto& desc : canonical_sound_events()) {
        if (desc.category == cat) {
            event_configs.erase(desc.id);
            const int legacy_idx = legacy_mouse_event_index(desc.id);
            if (legacy_idx >= 0) {
                sounds[static_cast<std::size_t>(legacy_idx)].clear();
            }
        }
    }
}

void AudioConfig::apply_profile_preset(SoundProfilePreset preset,
                                       const std::vector<std::string>& available_sounds) {
    auto pick = [&](std::initializer_list<const char*> candidates) -> std::string {
        for (const char* cand : candidates) {
            for (const auto& avail : available_sounds) {
                if (to_lower_ascii(avail) == to_lower_ascii(cand)) {
                    return avail;
                }
            }
        }
        if (!available_sounds.empty()) return available_sounds.front();
        return {};
    };

    active_profile = preset;
    if (preset == SoundProfilePreset::Custom) {
        return;
    }

    event_configs.clear();
    for (auto& s : sounds) s.clear();

    if (preset == SoundProfilePreset::Default) {
        enabled = false;
        keyboard_enabled = false;
        typing_sounds_enabled = false;
        modifier_tap_sounds_enabled = false;
        volume = 1.0f;
        return;
    }

    const std::string click_snd  = pick({"Click.wav", "click.wav", "Pop.wav"});
    const std::string up_snd     = pick({"Toc.wav", "Click.wav", "click.wav"});
    const std::string scroll_snd = pick({"Tick.wav", "Click.wav", "click.wav"});
    const std::string key_snd    = pick({"Tap.wav", "Click.wav", "click.wav"});
    const std::string space_snd  = pick({"Thock.wav", "Toc.wav", "Click.wav", "click.wav"});
    const std::string enter_snd  = pick({"Clack.wav", "Pop.wav", "Click.wav", "click.wav"});
    const std::string short_snd  = pick({"Chime.wav", "Ding.wav", "Click.wav", "click.wav"});

    if (preset == SoundProfilePreset::Minimal) {
        enabled = true;
        keyboard_enabled = true;
        typing_sounds_enabled = false;
        volume = 0.70f;
        if (!click_snd.empty()) {
            EventSoundConfig ec{};
            ec.sound_id = click_snd;
            set_event_config("mouse.left_down", ec);
            set_event_config("shortcut.copy", ec);
            set_event_config("shortcut.paste", ec);
            set_event_config("shortcut.save", ec);
        }
    } else if (preset == SoundProfilePreset::KeyboardFocused) {
        enabled = true;
        keyboard_enabled = true;
        typing_sounds_enabled = true;
        volume = 0.85f;
        if (!key_snd.empty()) {
            EventSoundConfig ec{};
            ec.sound_id = key_snd;
            ec.policy = PlaybackPolicy::Stack;
            set_event_config("typing.generic", ec);
            set_event_config("typing.letter", ec);
            set_event_config("typing.digit", ec);
            set_event_config("typing.symbol", ec);
            set_event_config("key.backspace", ec);
            set_event_config("key.delete", ec);
        }
        if (!space_snd.empty()) {
            EventSoundConfig ec{};
            ec.sound_id = space_snd;
            set_event_config("typing.space", ec);
        }
        if (!enter_snd.empty()) {
            EventSoundConfig ec{};
            ec.sound_id = enter_snd;
            set_event_config("key.enter", ec);
        }
        if (!short_snd.empty()) {
            EventSoundConfig ec{};
            ec.sound_id = short_snd;
            set_event_config("shortcut.copy", ec);
            set_event_config("shortcut.paste", ec);
            set_event_config("shortcut.cut", ec);
            set_event_config("shortcut.undo", ec);
            set_event_config("shortcut.redo", ec);
            set_event_config("shortcut.save", ec);
        }
    } else if (preset == SoundProfilePreset::MouseFocused) {
        enabled = true;
        keyboard_enabled = false;
        typing_sounds_enabled = false;
        volume = 0.85f;
        if (!click_snd.empty()) {
            EventSoundConfig ec{};
            ec.sound_id = click_snd;
            set_event_config("mouse.left_down", ec);
            set_event_config("mouse.right_down", ec);
            set_event_config("mouse.middle_down", ec);
        }
        if (!up_snd.empty()) {
            EventSoundConfig ec{};
            ec.sound_id = up_snd;
            ec.gain_db = -3.0f;
            set_event_config("mouse.left_up", ec);
            set_event_config("mouse.right_up", ec);
        }
        if (!scroll_snd.empty()) {
            EventSoundConfig ec{};
            ec.sound_id = scroll_snd;
            ec.gain_db = -4.0f;
            ec.policy = PlaybackPolicy::Coalesce;
            set_event_config("mouse.scroll_up", ec);
            set_event_config("mouse.scroll_down", ec);
            set_event_config("mouse.scroll_left", ec);
            set_event_config("mouse.scroll_right", ec);
        }
    } else if (preset == SoundProfilePreset::Quiet) {
        enabled = true;
        keyboard_enabled = true;
        typing_sounds_enabled = true;
        volume = 0.35f;
        if (!key_snd.empty()) {
            EventSoundConfig ec{};
            ec.sound_id = key_snd;
            ec.gain_db = -6.0f;
            set_event_config("typing.generic", ec);
            set_event_config("mouse.left_down", ec);
        }
    }
}

AudioConfig AudioConfig::validated(const AudioConfig& in) {
    AudioConfig out{};
    out.enabled = in.enabled;
    out.muted = in.muted;
    out.volume = safe_float(in.volume, kMinVolume, kMaxVolume, kMaxVolume);

    for (std::size_t i = 0; i < kAudioEventCount; ++i) {
        out.sounds[i] = sanitize_sound_id(in.sounds[i]);
    }

    // Favorites: ordered, de-duplicated, bounded by kMaxFavorites.
    out.favorites.clear();
    out.favorites.reserve(std::min<std::size_t>(
        in.favorites.size(), static_cast<std::size_t>(kMaxFavorites)));
    for (const std::string& raw : in.favorites) {
        if (static_cast<int>(out.favorites.size()) >= kMaxFavorites) break;
        const std::string id = sanitize_sound_id(raw);
        if (id.empty()) continue;
        if (std::find(out.favorites.begin(), out.favorites.end(), id) == out.favorites.end()) {
            out.favorites.push_back(id);
        }
    }

    // Recent sounds: ordered, de-duplicated, bounded by kMaxRecentSounds.
    out.recent_sounds.clear();
    for (const std::string& raw : in.recent_sounds) {
        if (static_cast<int>(out.recent_sounds.size()) >= kMaxRecentSounds) break;
        const std::string id = sanitize_sound_id(raw);
        if (id.empty()) continue;
        if (std::find(out.recent_sounds.begin(), out.recent_sounds.end(), id) == out.recent_sounds.end()) {
            out.recent_sounds.push_back(id);
        }
    }

    out.preview_on_selection = in.preview_on_selection;
    out.preview_debounce_ms = std::clamp(in.preview_debounce_ms, 0, 250);

    out.keyboard_enabled = in.keyboard_enabled;
    out.keyboard_scope = (in.keyboard_scope == KeyboardScope::SystemWide)
                             ? KeyboardScope::SystemWide
                             : KeyboardScope::AppOnly;
    out.typing_sounds_enabled = in.typing_sounds_enabled;
    out.modifier_tap_sounds_enabled = in.modifier_tap_sounds_enabled;
    out.suppress_repeat_typing = in.suppress_repeat_typing;
    out.typing_repeat_cooldown_ms = std::clamp(in.typing_repeat_cooldown_ms, 0, 1000);
    out.suppress_sensitive_contexts = in.suppress_sensitive_contexts;

    out.excluded_processes.clear();
    for (const std::string& raw : in.excluded_processes) {
        if (static_cast<int>(out.excluded_processes.size()) >= kMaxProcessExclusions) break;
        const std::string proc = sanitize_process_name(raw);
        if (proc.empty()) continue;
        if (std::find(out.excluded_processes.begin(), out.excluded_processes.end(), proc)
            == out.excluded_processes.end()) {
            out.excluded_processes.push_back(proc);
        }
    }

    out.horizontal_scroll_enabled = in.horizontal_scroll_enabled;
    out.scroll_sensitivity = safe_float(in.scroll_sensitivity, 0.25f, 4.0f, 1.0f);
    out.scroll_cooldown_ms = std::clamp(in.scroll_cooldown_ms, 0, 1000);
    out.scroll_max_rate_hz = std::clamp(in.scroll_max_rate_hz, 1, 120);
    out.scroll_pitch_variation = safe_float(in.scroll_pitch_variation, 0.0f, 0.50f, 0.0f);

    const int prof = static_cast<int>(in.active_profile);
    out.active_profile = (prof >= 0 && prof <= static_cast<int>(SoundProfilePreset::Custom))
                             ? in.active_profile
                             : SoundProfilePreset::Default;

    out.custom_shortcuts.clear();
    for (const auto& cs : in.custom_shortcuts) {
        if (static_cast<int>(out.custom_shortcuts.size()) >= kMaxCustomShortcuts) break;
        CustomShortcutBinding clean = CustomShortcutBinding::validated(cs);
        if (clean.id.empty() || clean.chord.empty()) continue;
        bool dup = false;
        for (const auto& existing : out.custom_shortcuts) {
            if (existing.id == clean.id) {
                dup = true;
                break;
            }
        }
        if (!dup) out.custom_shortcuts.push_back(std::move(clean));
    }

    out.event_configs.clear();
    for (const auto& [raw_key, raw_cfg] : in.event_configs) {
        const std::string cid = canonicalize_sound_event_id(raw_key);
        if (cid.empty()) continue;
        EventSoundConfig clean = EventSoundConfig::validated(raw_cfg);
        if (!clean.is_default_unassigned()) {
            out.event_configs[cid] = std::move(clean);
        }
    }

    // Bidirectional sync between legacy `sounds[0..7]` and `event_configs["mouse.*"]`:
    // `sounds[0..7]` remains the canonical legacy slot for simple mouse WAV assignment,
    // while `event_configs["mouse.*"]` holds per-event advanced overrides (gain, policy, pool).
    // When `event_configs` only carries a plain sound_id and no overrides/pool, it does not need
    // a redundant entry in `event_configs` so default AudioConfig with only `sounds[i]` set
    // stays clean and equals an expected legacy AudioConfig.
    for (int i = 0; i < kAudioEventCount; ++i) {
        const std::string cid = kCanonicalMouseEventNames[i];
        std::string& s_id = out.sounds[static_cast<std::size_t>(i)];
        auto it = out.event_configs.find(cid);
        if (it != out.event_configs.end()) {
            if (!it->second.sound_id.empty()) {
                s_id = it->second.sound_id;
            } else if (!s_id.empty()) {
                it->second.sound_id = s_id;
            }
            // If `it->second` has no overrides beyond `sound_id == s_id`, omit the redundant
            // map entry so legacy `AudioConfig` equality (`cfg.audio.sounds[i] = "x.wav"`) holds.
            EventSoundConfig stripped = it->second;
            stripped.sound_id.clear();
            if (stripped.is_default_unassigned()) {
                out.event_configs.erase(it);
            }
        }
    }

    return out;
}

std::string sanitize_sound_id(const std::string& id) {
    if (id.empty()) return {};
    if (static_cast<int>(id.size()) > AudioConfig::kMaxSoundNameLength) return {};
    // A dot-name (".", "..", ".hidden.wav") is never a plain file reference.
    if (id.front() == '.') return {};
    if (id.find("..") != std::string::npos) return {};
    for (const char c : id) {
        if (!is_valid_name_char(static_cast<unsigned char>(c))) return {};
    }
    if (!has_wav_extension(id)) return {};
    return id;
}

std::string sanitize_process_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return {};
    std::string clean;
    clean.reserve(name.size());
    for (char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c <= 0x20 || c >= 0x7F) continue;
        if (!is_valid_name_char(c)) return {};
        clean.push_back(static_cast<char>(std::tolower(c)));
    }
    if (clean.empty() || clean.front() == '.' || clean.find("..") != std::string::npos) {
        return {};
    }
    return clean;
}

int accumulate_wheel(int& accumulator, int signed_delta) {
    return accumulate_wheel_scaled(accumulator, signed_delta, 1.0f);
}

int accumulate_wheel_scaled(int& accumulator, int signed_delta, float sensitivity) {
    const float sens = safe_float(sensitivity, 0.25f, 4.0f, 1.0f);
    const int step = std::max(15, static_cast<int>(std::lround(static_cast<float>(kWheelDeltaUnit) / sens)));
    accumulator += signed_delta;
    const int limit = step * kMaxWheelEventsPerPacket;
    accumulator = std::clamp(accumulator, -limit, limit);
    int events = 0;
    while (accumulator >= step && events < kMaxWheelEventsPerPacket) {
        accumulator -= step;
        ++events;
    }
    while (accumulator <= -step && events > -kMaxWheelEventsPerPacket) {
        accumulator += step;
        --events;
    }
    return events;
}

std::vector<float> normalized_pool_weights(const std::vector<SoundPoolEntry>& pool) {
    std::vector<float> weights(pool.size(), 0.0f);
    double sum = 0.0;
    for (std::size_t i = 0; i < pool.size(); ++i) {
        if (pool[i].enabled && !sanitize_sound_id(pool[i].sound_id).empty() && pool[i].weight > 0.0f) {
            weights[i] = std::clamp(pool[i].weight, 0.01f, 100.0f);
            sum += static_cast<double>(weights[i]);
        }
    }
    if (sum <= 0.0) return weights;
    for (float& w : weights) {
        w = static_cast<float>(static_cast<double>(w) / sum);
    }
    return weights;
}

// Gain reference: the master volume. An event or pool gain is attenuation
// relative to it, so 0 dB is unity and the most negative setting is -24 dB.
// Maximum output level: the platform's 1.0 ceiling -- the result never exceeds
// the master volume, so no configuration can clip, and a gain above unity has
// nothing to amplify INTO (the volume domain ends at 1.0). The clamp is the
// trust-boundary guard, not the contract.
double db_to_linear_gain(float gain_db, double master_volume) {
    const float clamped_db = safe_float(gain_db, EventSoundConfig::kMinGainDb, EventSoundConfig::kMaxGainDb, 0.0f);
    const double linear = std::pow(10.0, static_cast<double>(clamped_db) / 20.0);
    const double master = std::clamp(master_volume, 0.0, 1.0);
    return std::clamp(master * linear, 0.0, 1.0);
}

} // namespace ptd
