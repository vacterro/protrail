#pragma once

#include "../config/audio_config.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace ptd {

// T-120: Normalized passive keyboard packet (derived from WM_INPUT / RAWKEYBOARD
// or Qt local key events without ever recording plaintext characters).
struct RawKeyPacket {
    uint16_t vkey = 0;         // Windows Virtual-Key code (VK_*)
    uint16_t make_code = 0;    // Physical scan code
    uint16_t flags = 0;        // RI_KEY_MAKE(0), RI_KEY_BREAK(1), RI_KEY_E0(2), RI_KEY_E1(4)
    bool is_down = true;       // KeyDown vs KeyUp
    bool is_e0 = false;        // Extended key prefix E0
    bool is_e1 = false;        // Extended key prefix E1 (e.g. Pause/Break, AltGr fake LCtrl)
    bool is_altgr_synthetic_lctrl = false; // True when Windows emits synthetic LCtrl before RAlt (AltGr)
    int64_t timestamp_ns = 0;
};

// Foreground context snapshot used for scope filtering, process exclusions,
// and conservative sensitive-context suppression.
struct ForegroundContext {
    bool is_protrail_foreground = false;
    std::string process_name;       // lowercase executable basename e.g. "notepad.exe"
    bool is_sensitive_context = false; // e.g. credential dialog / password prompt heuristic
    uintptr_t window_handle_id = 0; // opaque foreground HWND identity to detect focus changes
};

// Result of classifying a single RawKeyPacket through KeyboardInputRouter.
struct ClassifiedKeyEvent {
    std::string event_id;      // Canonical or custom event id ("" == silence / suppressed)
    int priority_tier = 7;     // 1 = User Exact, 2 = App Exact, 3 = Named Shortcut, 4 = Specific Key,
                               // 5 = Generic Typing Category, 6 = Category Fallback (typing.generic), 7 = Silence
    bool is_repeat = false;    // True when key was already held down (OS auto-repeat)
    bool is_shortcut = false;  // True for priority tiers 1..3
    bool is_modifier_tap = false; // True when emitted on KeyUp of an isolated modifier tap
};

// Pure deterministic state machine for keyboard state, auto-repeat tracking,
// AltGr disambiguation, isolated modifier-tap detection, foreground tracking,
// and 7-tier shortcut/key arbitration.
//
// Privacy & Safety Guarantees (Section 3):
// - Never calls ToUnicode / ToAscii or reconstructs typed text.
// - Never reads clipboard contents.
// - Holds only a fixed 256-bit boolean down-table for auto-repeat detection and
//   current modifier bitflags; zero key history is retained.
class KeyboardInputRouter {
public:
    KeyboardInputRouter() = default;

    void apply_config(const AudioConfig& cfg);
    const AudioConfig& config() const { return config_; }

    // Clear held-key state (called on foreground window change, scope disable, or Stop All).
    void reset_state();

    // Feed one passive key packet and optional foreground context.
    // Returns the single winning ClassifiedKeyEvent (or empty event_id for silence).
    ClassifiedKeyEvent process_key(const RawKeyPacket& pkt, const ForegroundContext& ctx);

    // Format a modifier+vkey combination into canonical chord string (e.g. "Ctrl+Shift+C" or "VK:0x41").
    static std::string format_chord(bool ctrl, bool alt, bool shift, bool win, uint16_t vkey, bool is_e0 = false);

    // Normalize a user-supplied chord string into the canonical representation.
    static std::string normalize_chord_string(const std::string& raw_chord);

    // Map an unmodified virtual key to its specific canonical event id (Tier 4) or empty if it is a routine typing key.
    static std::string classify_specific_key(uint16_t vkey, uint16_t make_code, bool is_e0);

    // Map an unmodified virtual key to its generic typing category (Tier 5: typing.letter / digit / symbol / space).
    static std::string classify_typing_category(uint16_t vkey, bool shift_down, bool is_e0);

    // Current modifier state accessors for testing.
    bool ctrl_down() const { return lctrl_down_ || rctrl_down_; }
    bool alt_down() const { return lalt_down_ || ralt_down_; }
    bool shift_down() const { return lshift_down_ || rshift_down_; }
    bool win_down() const { return lwin_down_ || rwin_down_; }
    bool altgr_active() const { return altgr_active_; }

private:
    ClassifiedKeyEvent resolve_arbitration(const std::string& chord,
                                           uint16_t vkey,
                                           uint16_t make_code,
                                           bool is_e0,
                                           bool has_shortcut_mods,
                                           bool is_repeat) const;

    bool has_configured_output(const std::string& event_id) const;
    static std::string match_named_shortcut(const std::string& chord);

    AudioConfig config_{};
    std::array<bool, 256> key_down_{};
    uintptr_t last_foreground_hwnd_ = 0;

    bool lctrl_down_ = false;
    bool rctrl_down_ = false;
    bool lalt_down_ = false;
    bool ralt_down_ = false;
    bool lshift_down_ = false;
    bool rshift_down_ = false;
    bool lwin_down_ = false;
    bool rwin_down_ = false;
    bool altgr_active_ = false;

    // Isolated modifier tap tracking: records which modifier VKey was pressed alone,
    // cleared as soon as any non-modifier key or second chord key is pressed.
    uint16_t pending_modifier_tap_vk_ = 0;
    bool chord_used_while_mod_held_ = false;
};

// Passive Windows Raw Input listener for keyboard events (Section 3).
// - Uses WM_INPUT with RIDEV_INPUTSINK ONLY when `keyboard_enabled && keyboard_scope == SystemWide`.
// - When `keyboard_scope == AppOnly`, unregisters any global raw-input sink so zero
//   background keyboard notifications arrive from other applications; AppOnly events
//   are fed passively from ProTrail's own window event filter.
// - Never installs a WH_KEYBOARD_LL hook, so it can never delay, intercept, or swallow keys.
class KeyboardInput {
public:
    using EventCallback = std::function<void(int64_t timestamp_ns, const ClassifiedKeyEvent& ev)>;

    KeyboardInput() = default;
    ~KeyboardInput();

    KeyboardInput(const KeyboardInput&) = delete;
    KeyboardInput& operator=(const KeyboardInput&) = delete;

    // Initialize the message-only raw input sink window (does NOT register RIDEV_INPUTSINK
    // unless configure() enables SystemWide mode).
    bool create(HINSTANCE instance);
    void destroy();

    // Update configuration and dynamically register/unregister global Raw Input sink.
    void apply_config(const AudioConfig& cfg);

    // True when global RIDEV_INPUTSINK keyboard capture is currently registered with Windows.
    bool global_sink_registered() const { return registered_; }
    bool valid() const { return hwnd_ != nullptr; }

    void set_event_callback(EventCallback cb) { callback_ = std::move(cb); }

    // Feed an app-local key event (used when scope == AppOnly or when ProTrail settings window is active).
    void handle_local_key_event(uint16_t vkey, uint16_t scan_code, bool is_extended, bool is_down, int64_t timestamp_ns);

    // Direct access to the pure router for deterministic testing.
    KeyboardInputRouter& router() { return router_; }
    const KeyboardInputRouter& router() const { return router_; }

    uint64_t packet_count() const { return packet_count_; }
    uint64_t emitted_count() const { return emitted_count_; }

    // Query foreground context safely (process basename + ProTrail ownership + conservative sensitive-window check).
    static ForegroundContext query_foreground_context();

private:
    static LRESULT CALLBACK wnd_proc_static(HWND, UINT, WPARAM, LPARAM);
    static KeyboardInput* s_instance_;

    void handle_raw_input(LPARAM lp);
    bool update_sink_registration(bool want_system_wide);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    bool registered_ = false;
    KeyboardInputRouter router_;
    EventCallback callback_;

    uint64_t packet_count_ = 0;
    uint64_t emitted_count_ = 0;
};

} // namespace ptd
