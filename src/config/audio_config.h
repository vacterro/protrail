#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ptd {

// T-80: one assignable legacy mouse sound event. The integer value is the
// stable order of the assignments inside the persisted legacy audio.sounds
// block.
enum class AudioEvent : int {
    LeftDown = 0,
    LeftUp,
    RightDown,
    RightUp,
    MiddleDown,
    MiddleUp,
    ScrollUp,
    ScrollDown,
    Count,
};

constexpr int kAudioEventCount = static_cast<int>(AudioEvent::Count);

// Stable persisted key for each legacy mouse event slot. Used by the
// serializer, the settings surface and the tests, so the eight names exist
// exactly once.
const char* audio_event_name(int index);

// T-120: Canonical Event Categories.
enum class SoundEventCategory : int {
    Typing = 0,
    Editing,
    Navigation,
    Selection,
    Clipboard,
    History,
    CommonActions,
    FunctionKeys,
    Modifiers,
    Numpad,
    Media,
    Mouse,
    Custom,
    Count,
};

const char* sound_event_category_id(SoundEventCategory cat);
const char* sound_event_category_label(SoundEventCategory cat);

// T-120: Audio Playback Policies (Section 7).
enum class PlaybackPolicy : int {
    Inherit = 0,
    Stack,
    Override,
    Cut,
    Queue,
    Coalesce,
};

const char* playback_policy_name(PlaybackPolicy p);
PlaybackPolicy parse_playback_policy(const std::string& s, PlaybackPolicy fallback = PlaybackPolicy::Inherit);

// T-120: Interruption Scope (Section 7).
enum class InterruptScope : int {
    Inherit = 0,
    SameEvent,
    SameGroup,
    Global,
};

const char* interrupt_scope_name(InterruptScope s);
InterruptScope parse_interrupt_scope(const std::string& s, InterruptScope fallback = InterruptScope::Inherit);

// T-120: Keyboard Capture Scope (Section 3).
enum class KeyboardScope : int {
    AppOnly = 0,
    SystemWide,
};

const char* keyboard_scope_name(KeyboardScope s);
KeyboardScope parse_keyboard_scope(const std::string& s);

// T-120: Sound Profile Preset (Section 12).
enum class SoundProfilePreset : int {
    Default = 0,
    Minimal,
    KeyboardFocused,
    MouseFocused,
    Quiet,
    Custom,
};

const char* sound_profile_preset_name(SoundProfilePreset p);
SoundProfilePreset parse_sound_profile_preset(const std::string& s);

// T-120: Canonical Typed Event Descriptor (Section 2).
struct SoundEventDescriptor {
    const char* id = "";
    const char* display_name = "";
    SoundEventCategory category = SoundEventCategory::Typing;
    PlaybackPolicy default_policy = PlaybackPolicy::Override;
    InterruptScope default_scope = InterruptScope::SameEvent;
    int default_cooldown_ms = 0;
    int default_priority = 50;
    int default_polyphony = 4;
    bool observable_guarantee = true; // false for firmware/OEM keys that Windows may not expose
};

// Returns the complete built-in canonical event registry table.
const std::vector<SoundEventDescriptor>& canonical_sound_events();

// Look up a canonical descriptor by id (supports legacy aliases like "left_down" -> "mouse.left_down").
const SoundEventDescriptor* find_sound_event_descriptor(const std::string& event_id);

// Canonicalizes an event identifier: maps legacy mouse keys ("left_down" .. "scroll_down")
// to their canonical "mouse.*" identifiers, and validates custom/canonical ids.
std::string canonicalize_sound_event_id(const std::string& raw_id);

// Returns legacy AudioEvent index (0..7) if event_id refers to one of the 8 mouse events, else -1.
int legacy_mouse_event_index(const std::string& event_id);

// T-120: Random Sound Pool Entry (Section 9).
struct SoundPoolEntry {
    bool enabled = true;
    std::string sound_id; // validated .wav filename
    float weight = 1.0f;  // 0.01 .. 100.0
    float gain_db = 0.0f; // -24.0 .. 0.0 dB (attenuation only, see kMaxGainDb)

    bool operator==(const SoundPoolEntry&) const = default;
};

// T-120: Per-Event Audio Configuration (Sections 8 & 9).
struct EventSoundConfig {
    static constexpr int kMaxPoolEntries = 10;
    // Gain ceiling: the truth. The audio platform's volume domain is 0..1, so
    // an event or pool gain can only attenuate below the master volume. A
    // positive dB value has nothing to amplify INTO, so the UI never offers
    // one and a saved out-of-range value clamps down to unity.
    static constexpr float kMinGainDb = -24.0f;
    static constexpr float kMaxGainDb = 0.0f;
    static constexpr int kMaxCooldownMs = 5000;
    static constexpr int kMaxFadeMs = 2000;
    static constexpr int kMinPriority = 0;
    static constexpr int kMaxPriority = 100;
    static constexpr int kMinPolyphony = 1;
    static constexpr int kMaxPolyphony = 16;
    static constexpr float kMaxPitchVariation = 0.50f;

    bool enabled = true;
    std::string sound_id;     // "" == None / silence (single WAV mode)
    bool use_pool = false;    // false = single WAV, true = random pool
    std::vector<SoundPoolEntry> pool; // up to 10 entries
    bool no_immediate_repeat = true;

    float gain_db = 0.0f;     // -24 .. 0 dB (attenuation only, see kMaxGainDb)
    PlaybackPolicy policy = PlaybackPolicy::Inherit;
    InterruptScope interrupt_scope = InterruptScope::Inherit;
    int cooldown_ms = 0;      // 0 .. 5000 ms
    int priority = 50;        // 0 .. 100

    // Advanced controls
    int fade_in_ms = 0;
    int fade_out_ms = 0;
    int min_retrigger_ms = 0;
    int polyphony_allowance = 4;
    float pitch_variation = 0.0f; // 0.0 .. 0.5
    bool normalize_override = false;
    bool normalize_enabled = false;

    static EventSoundConfig validated(const EventSoundConfig& in);
    bool is_default_unassigned() const;
    bool operator==(const EventSoundConfig&) const = default;
};

// T-120: Custom user-defined shortcut or individual key binding (Sections 2 & 4).
struct CustomShortcutBinding {
    std::string id;           // e.g. "custom.shortcut_1" or "key.vk_192"
    std::string display_name; // user-visible label
    std::string chord;        // canonical normalized chord e.g. "Ctrl+Shift+K" or "VK:0xBA"
    bool is_app_shortcut = false; // true = Priority 2 (registered app shortcut), false = Priority 1 (user exact shortcut)
    bool enabled = true;

    static CustomShortcutBinding validated(const CustomShortcutBinding& in);
    bool operator==(const CustomShortcutBinding&) const = default;
};

// T-80 / T-107 / T-120: the whole audio configuration block (schema 12..14).
struct AudioConfig {
    static constexpr int kAudioSchema = 12;
    static constexpr int kAudioFavoritesSchema = 13;
    // T-120 INTRODUCTION BOUNDARY (schema 14).
    static constexpr int kAudioKeyboardSystemSchema = 14;

    static constexpr int kMaxSoundNameLength = 64;
    static constexpr int kMaxFavorites = 64;
    static constexpr int kMaxRecentSounds = 32;
    static constexpr int kMaxProcessExclusions = 64;
    static constexpr int kMaxCustomShortcuts = 64;

    static constexpr float kMinVolume = 0.0f;
    static constexpr float kMaxVolume = 1.0f;

    bool enabled = false;
    bool muted = false;
    float volume = 1.0f;

    // Legacy 8 mouse event assignments (preserved for schema 12/13 & existing UI/tests).
    // Synced bidirectionally with event_configs["mouse.*"] in validated().
    std::array<std::string, kAudioEventCount> sounds{};  // "" == None

    // T-107: user-curated favorites.
    std::vector<std::string> favorites{};

    // T-120: recently selected sounds in the browser (most recent first, bounded).
    std::vector<std::string> recent_sounds{};

    // T-120: Sound Browser live preview settings.
    bool preview_on_selection = true;
    int preview_debounce_ms = 35; // 0 .. 250 ms

    // T-120: Keyboard sound subsystem (Sections 3, 4, 6).
    // System-wide keyboard capture MUST default to OFF (AppOnly).
    // New typing sounds MUST default to OFF until enabled.
    bool keyboard_enabled = false;
    KeyboardScope keyboard_scope = KeyboardScope::AppOnly;
    bool typing_sounds_enabled = false;
    bool modifier_tap_sounds_enabled = false;
    bool suppress_repeat_typing = false;
    int typing_repeat_cooldown_ms = 35; // 0 .. 1000 ms
    bool suppress_sensitive_contexts = true;
    std::vector<std::string> excluded_processes{}; // e.g. "keepass.exe", "1password.exe"

    // T-120: Scroll sound subsystem (Section 5).
    bool horizontal_scroll_enabled = true;
    float scroll_sensitivity = 1.0f; // 0.25 .. 4.0 (scales 120 wheel delta threshold)
    int scroll_cooldown_ms = 25;     // 0 .. 1000 ms
    int scroll_max_rate_hz = 30;     // 1 .. 120 events/sec
    float scroll_pitch_variation = 0.0f; // 0.0 .. 0.5

    // T-120: Active preset template name ("default", "minimal", "keyboard_focused",
    // "mouse_focused", "quiet", "custom").
    SoundProfilePreset active_profile = SoundProfilePreset::Default;

    // T-120: Custom shortcut & custom key bindings.
    std::vector<CustomShortcutBinding> custom_shortcuts{};

    // T-120: Per-event configuration map keyed by canonical event id (e.g. "typing.letter",
    // "shortcut.copy", "mouse.left_down", or custom shortcut id).
    std::unordered_map<std::string, EventSoundConfig> event_configs{};

    // Look up effective EventSoundConfig for any canonical or legacy event id.
    EventSoundConfig effective_event_config(const std::string& event_id) const;

    // Update or insert an event configuration and keep legacy `sounds` array in sync.
    void set_event_config(const std::string& event_id, const EventSoundConfig& cfg);

    // Reset all events in a category to their inherited defaults.
    void reset_category(SoundEventCategory cat);

    // Apply a built-in sound profile template using available catalog sounds.
    void apply_profile_preset(SoundProfilePreset preset, const std::vector<std::string>& available_sounds);

    static AudioConfig validated(const AudioConfig& in);
    bool operator==(const AudioConfig&) const = default;
};

// Returns the identifier unchanged when it is a valid relative sound name,
// and the empty string otherwise.
std::string sanitize_sound_id(const std::string& id);

// Validates a process executable name for the exclusion list (e.g. "notepad.exe").
std::string sanitize_process_name(const std::string& name);

// Validates a canonical or custom event identifier.
bool is_valid_event_id(const std::string& id);

// T-80 / T-120: bounded wheel accumulation.
constexpr int kWheelDeltaUnit = 120;           // WHEEL_DELTA
constexpr int kMaxWheelEventsPerPacket = 8;    // ceiling on one packet

// Pure: folds one wheel packet's signed delta into `accumulator` and returns
// how many whole vertical events it produced (+N up, -N down).
int accumulate_wheel(int& accumulator, int signed_delta);

// Sensitivity-scaled wheel accumulation (sensitivity > 1.0 = smaller threshold = more sensitive).
int accumulate_wheel_scaled(int& accumulator, int signed_delta, float sensitivity);

// Normalize pool weights deterministically so enabled entries sum to 1.0 (or 0 if none enabled).
std::vector<float> normalized_pool_weights(const std::vector<SoundPoolEntry>& pool);

// Convert dB gain (-24..+12) to linear amplitude multiplier, clamped safely to avoid clipping.
double db_to_linear_gain(float gain_db, double master_volume = 1.0);

} // namespace ptd
