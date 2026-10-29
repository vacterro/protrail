// T-120 regression suite: the pure, deterministic core of the keyboard sound
// system. No Qt, no audio device, no raw-input window -- only the arbitration
// state machine and the config-layer arithmetic it rests on. Every assertion
// here is a behavior the BUILD phase wired up: 7-tier priority, AltGr
// disambiguation, modifier-tap isolation, scope/exclusion gating, scaled wheel
// accumulation, random-pool normalization, dB gain, and profile presets.

#include "../src/platform/keyboard_input.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_failures = 0;
int g_checks = 0;

void expect_true(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL %s\n", what);
    }
}

void expect_str(const std::string& got, const char* want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL %s: got \"%s\" want \"%s\"\n", what, got.c_str(), want);
    }
}

void expect_int(long long got, long long want, const char* what) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::printf("FAIL %s: got %lld want %lld\n", what, got, want);
    }
}

void expect_near(double got, double want, double tol, const char* what) {
    ++g_checks;
    if (std::fabs(got - want) > tol) {
        ++g_failures;
        std::printf("FAIL %s: got %.6f want %.6f\n", what, got, want);
    }
}

using ptd::AudioConfig;
using ptd::ClassifiedKeyEvent;
using ptd::EventSoundConfig;
using ptd::ForegroundContext;
using ptd::KeyboardInputRouter;
using ptd::KeyboardScope;
using ptd::RawKeyPacket;
using ptd::SoundPoolEntry;

// A fully-armed, app-scoped config with one sound assigned to every canonical
// event. That removes "no sound assigned" from the arbitration outcome so the
// tests can assert the tier that WON rather than the fallback.
AudioConfig make_config() {
    AudioConfig cfg = AudioConfig::validated(AudioConfig{});
    cfg.enabled = true;
    cfg.muted = false;
    cfg.volume = 1.0f;
    cfg.keyboard_enabled = true;
    cfg.keyboard_scope = KeyboardScope::AppOnly;
    cfg.typing_sounds_enabled = true;
    cfg.modifier_tap_sounds_enabled = true;
    for (const auto& desc : ptd::canonical_sound_events()) {
        EventSoundConfig ec;
        ec.enabled = true;
        ec.sound_id = "protrail_default.wav";
        cfg.set_event_config(desc.id, ec);
    }
    return cfg;
}

RawKeyPacket key(uint16_t vk, bool down, bool e0 = false, bool altgr_lctrl = false) {
    RawKeyPacket p;
    p.vkey = vk;
    p.make_code = 0;
    p.is_down = down;
    p.is_e0 = e0;
    p.is_altgr_synthetic_lctrl = altgr_lctrl;
    return p;
}

// ProTrail's own foreground window is where AppOnly scope is allowed to listen.
ForegroundContext own_window() {
    ForegroundContext c;
    c.is_protrail_foreground = true;
    c.window_handle_id = 1;
    return c;
}

ForegroundContext other_window(const char* proc = "notepad.exe") {
    ForegroundContext c;
    c.is_protrail_foreground = false;
    c.process_name = proc;
    c.window_handle_id = 2;
    return c;
}

void test_chord_formatting_and_normalization() {
    using ptd::KeyboardInputRouter;
    expect_str(KeyboardInputRouter::format_chord(false, false, false, false, 'A'),
               "A", "bare letter chord");
    expect_str(KeyboardInputRouter::format_chord(true, false, true, false, 'C'),
               "Ctrl+Shift+C", "modifiers emitted in canonical order");
    expect_str(KeyboardInputRouter::format_chord(false, true, false, false, VK_TAB),
               "Alt+Tab", "extended key uses its E0 token name");
    expect_str(KeyboardInputRouter::format_chord(false, false, false, false, VK_RETURN, true),
               "NumpadEnter", "E0 Enter is the numpad Enter");

    expect_str(KeyboardInputRouter::normalize_chord_string("ctrl+shift+a"),
               "Ctrl+Shift+A", "lowercase user chord canonicalizes");
    expect_str(KeyboardInputRouter::normalize_chord_string("  Control + B  "),
               "Ctrl+B", "Control alias and surrounding whitespace");
    expect_str(KeyboardInputRouter::normalize_chord_string("ctrl+"),
               "Ctrl+Plus", "trailing plus is the Plus key, not a separator");
    expect_str(KeyboardInputRouter::normalize_chord_string("ctrl++"),
               "Ctrl+Plus", "explicit double-plus is the Plus key too");
    expect_str(KeyboardInputRouter::normalize_chord_string("ctrl+insert"),
               "Ctrl+Insert", "Insert keeps its name");
    expect_str(KeyboardInputRouter::normalize_chord_string(""), "", "empty chord stays empty");
    expect_str(KeyboardInputRouter::normalize_chord_string("ctrl"), "",
               "bare modifier is not a bindable chord");
}

void test_specific_key_and_typing_classification() {
    using ptd::KeyboardInputRouter;
    expect_str(KeyboardInputRouter::classify_specific_key(VK_BACK, 0, false),
               "key.backspace", "backspace is a specific editing event");
    expect_str(KeyboardInputRouter::classify_specific_key(VK_RETURN, 0, false),
               "key.enter", "main Enter is specific");
    expect_str(KeyboardInputRouter::classify_specific_key(VK_RETURN, 0, true),
               "numpad.enter", "extended Enter is the numpad Enter");
    expect_str(KeyboardInputRouter::classify_specific_key(VK_F1, 0, false),
               "key.f1", "function keys are specific");
    expect_str(KeyboardInputRouter::classify_typing_category('A', false, false),
               "typing.letter", "letters are routine typing");
    expect_str(KeyboardInputRouter::classify_typing_category('1', false, false),
               "typing.digit", "unshifted digits are digits");
    expect_str(KeyboardInputRouter::classify_typing_category('1', true, false),
               "typing.symbol", "shifted digits type symbols");
    expect_str(KeyboardInputRouter::classify_typing_category(VK_OEM_1, false, false),
               "typing.symbol", "OEM punctuation is a symbol");
    expect_true(KeyboardInputRouter::classify_specific_key('A', 0, false).empty(),
                "letters have no specific-key event");
}

void test_arbitration_priority() {
    AudioConfig cfg = make_config();
    KeyboardInputRouter r;
    r.apply_config(cfg);

    // Tier 1: an explicit user shortcut beats every built-in tier below it.
    ptd::CustomShortcutBinding user;
    user.id = "custom.user_action";
    user.chord = "Ctrl+Shift+K";
    user.is_app_shortcut = false;
    cfg.custom_shortcuts.push_back(user);
    r.apply_config(AudioConfig::validated(cfg));

    ForegroundContext ctx = own_window();
    r.process_key(key(VK_CONTROL, true), ctx);
    r.process_key(key(VK_SHIFT, true), ctx);
    ClassifiedKeyEvent e = r.process_key(key('K', true), ctx);
    expect_str(e.event_id, "custom.user_action", "tier 1 user exact shortcut wins");
    expect_true(e.is_shortcut, "tier 1 is reported as a shortcut");
    expect_int(e.priority_tier, 1, "tier 1 priority recorded");

    // A chord that is a registered app shortcut but not user-exact lands at tier 2.
    KeyboardInputRouter r2;
    AudioConfig cfg2 = make_config();
    ptd::CustomShortcutBinding app;
    app.id = "custom.app_action";
    app.chord = "Ctrl+Alt+P";
    app.is_app_shortcut = true;
    cfg2.custom_shortcuts.push_back(app);
    r2.apply_config(AudioConfig::validated(cfg2));
    r2.process_key(key(VK_CONTROL, true), ctx);
    r2.process_key(key(VK_MENU, true), ctx);
    e = r2.process_key(key('P', true), ctx);
    expect_str(e.event_id, "custom.app_action", "tier 2 app shortcut wins");
    expect_int(e.priority_tier, 2, "tier 2 priority recorded");
}

void test_named_shortcut_and_chord_fallthrough_rules() {
    AudioConfig cfg = make_config();
    KeyboardInputRouter r;
    r.apply_config(cfg);
    ForegroundContext ctx = own_window();

    r.process_key(key(VK_CONTROL, true), ctx);
    ClassifiedKeyEvent e = r.process_key(key('C', true), ctx);
    expect_str(e.event_id, "shortcut.copy", "named shortcut recognized at tier 3");
    expect_int(e.priority_tier, 3, "named shortcut is tier 3");
    expect_str(r.process_key(key('C', false), ctx).event_id, "", "key up is silent");
    r.process_key(key(VK_CONTROL, false), ctx);

    // The load-bearing rule: a recognized physical shortcut MUST NOT fall
    // through to its individual key or the generic typing sound.
    r.reset_state();
    r.process_key(key(VK_CONTROL, true), ctx);
    e = r.process_key(key(VK_BACK, true), ctx);
    expect_str(e.event_id, "shortcut.delete_word_left",
               "Ctrl+Backspace stays a shortcut, never key.backspace");
    r.reset_state();

    // Unrecognized modifier chord stays silent instead of leaking a typing sound.
    r.process_key(key(VK_CONTROL, true), ctx);
    e = r.process_key(key('Q', true), ctx);
    expect_str(e.event_id, "", "unrecognized Ctrl+key is silent");
    expect_int(e.priority_tier, 7, "unrecognized chord is tier 7");
    r.reset_state();
}

void test_typing_categories_and_repeat() {
    AudioConfig cfg = make_config();
    KeyboardInputRouter r;
    r.apply_config(cfg);
    ForegroundContext ctx = own_window();

    ClassifiedKeyEvent e = r.process_key(key('A', true), ctx);
    expect_str(e.event_id, "typing.letter", "letter routes to its category");

    e = r.process_key(key('A', true), ctx);
    expect_true(e.is_repeat, "second KeyDown is a repeat");
    // A repeat with a dedicated assignment takes typing.repeat, which sounds
    // differently from the first press; that is the intended design, not a
    // fallthrough failure.
    expect_str(e.event_id, "typing.repeat", "assigned repeat event wins over the letter");

    // Without a repeat assignment the letter keeps typing on every repeat.
    AudioConfig no_repeat_event = make_config();
    no_repeat_event.set_event_config("typing.repeat", EventSoundConfig{});
    KeyboardInputRouter r3;
    r3.apply_config(AudioConfig::validated(no_repeat_event));
    r3.process_key(key('A', true), ctx);
    e = r3.process_key(key('A', true), ctx);
    expect_true(e.is_repeat, "repeat still flagged without a dedicated event");
    expect_str(e.event_id, "typing.letter", "unassigned repeat continues typing the letter");

    AudioConfig no_repeat = make_config();
    no_repeat.suppress_repeat_typing = true;
    KeyboardInputRouter r2;
    r2.apply_config(AudioConfig::validated(no_repeat));
    r2.process_key(key('A', true), ctx);
    e = r2.process_key(key('A', true), ctx);
    expect_str(e.event_id, "", "suppressed repeats are silent");
}

void test_specific_key_wins_over_typing_when_tier_four_falls_back() {
    // Backspace has no typing category; with typing sounds on and a sound
    // assigned to the generic category it must NOT become typing.generic.
    AudioConfig cfg = make_config();
    KeyboardInputRouter r;
    r.apply_config(cfg);
    ForegroundContext ctx = own_window();
    const ClassifiedKeyEvent e = r.process_key(key(VK_BACK, true), ctx);
    expect_str(e.event_id, "key.backspace", "specific key keeps its own event");
    expect_int(e.priority_tier, 4, "specific key is tier 4");
}

void test_scope_exclusion_and_sensitive_gates() {
    const AudioConfig base = make_config();

    // AppOnly ignores every other foreground window.
    KeyboardInputRouter r;
    r.apply_config(base);
    expect_str(r.process_key(key('A', true), other_window()).event_id, "",
               "AppOnly stays silent in a foreign window");
    expect_str(r.process_key(key('A', true), own_window()).event_id, "typing.letter",
               "AppOnly types in ProTrail's own window");

    // SystemWide with an explicit exclusion list for the focused process.
    AudioConfig sw = make_config();
    sw.keyboard_scope = KeyboardScope::SystemWide;
    sw.excluded_processes = {"keepass.exe"};
    KeyboardInputRouter r2;
    r2.apply_config(AudioConfig::validated(sw));
    expect_str(r2.process_key(key('A', true), other_window("keepass.exe")).event_id, "",
               "excluded process stays silent in SystemWide mode");
    expect_str(r2.process_key(key('A', true), other_window("notepad.exe")).event_id,
               "typing.letter", "unexcluded process types in SystemWide mode");

    // Sensitive contexts are suppressed by default.
    ForegroundContext sensitive = own_window();
    sensitive.is_sensitive_context = true;
    KeyboardInputRouter r3;
    r3.apply_config(base);
    expect_str(r3.process_key(key('A', true), sensitive).event_id, "",
               "sensitive context suppressed by default");
    AudioConfig allow_sensitive = make_config();
    allow_sensitive.suppress_sensitive_contexts = false;
    KeyboardInputRouter r4;
    r4.apply_config(AudioConfig::validated(allow_sensitive));
    expect_str(r4.process_key(key('A', true), sensitive).event_id, "typing.letter",
               "suppression is opt-out");

    // The master and keyboard switches are hard gates.
    AudioConfig disabled = make_config();
    disabled.enabled = false;
    KeyboardInputRouter r5;
    r5.apply_config(AudioConfig::validated(disabled));
    expect_str(r5.process_key(key('A', true), own_window()).event_id, "",
               "audio master switch off means silence");
    AudioConfig no_keyboard = make_config();
    no_keyboard.keyboard_enabled = false;
    KeyboardInputRouter r6;
    r6.apply_config(AudioConfig::validated(no_keyboard));
    expect_str(r6.process_key(key('A', true), own_window()).event_id, "",
               "keyboard subsystem off means silence");
}

void test_altgr_disambiguation_synthetic_lctrl() {
    AudioConfig cfg = make_config();
    KeyboardInputRouter r;
    r.apply_config(cfg);
    ForegroundContext ctx = own_window();

    // Windows emits a synthetic LCtrl (often E1-flagged) before RAlt on
    // AltGr layouts. Neither packet may sound, and the following character must
    // type as a symbol instead of resolving as a Ctrl chord.
    expect_str(r.process_key(key(VK_LCONTROL, true, false, true), ctx).event_id, "",
               "synthetic AltGr LCtrl is silent");
    expect_true(r.altgr_active(), "synthetic LCtrl arms AltGr state");
    expect_str(r.process_key(key(VK_LCONTROL, false, false, true), ctx).event_id, "",
               "synthetic LCtrl release is silent");
    expect_true(!r.ctrl_down(), "synthetic LCtrl is not a real held Ctrl");
    const ClassifiedKeyEvent e = r.process_key(key(VK_OEM_6, true), ctx);
    expect_str(e.event_id, "typing.symbol", "AltGr character types, not a shortcut chord");
}

void test_altgr_disambiguation_held_lctrl_plus_ralt() {
    AudioConfig cfg = make_config();
    KeyboardInputRouter r;
    r.apply_config(cfg);
    ForegroundContext ctx = own_window();

    // The other AltGr shape: a real LCtrl held, then RAlt. Windows reports that
    // pair to the app as Ctrl+Alt, so the router must recognize it as AltGr
    // and let the character type instead of playing a chord shortcut.
    r.process_key(key(VK_LCONTROL, true), ctx);
    r.process_key(key(VK_RMENU, true), ctx);
    expect_true(r.altgr_active(), "RAlt over held LCtrl is AltGr");
    const ClassifiedKeyEvent e = r.process_key(key(VK_OEM_6, true), ctx);
    expect_str(e.event_id, "typing.symbol", "AltGr character types, not a shortcut chord");

    // A genuine Ctrl+Alt shortcut chord (LCtrl + LAlt) still resolves as a
    // chord, proving the AltGr rule is not swallowing every Ctrl+Alt press.
    KeyboardInputRouter r2;
    r2.apply_config(cfg);
    r2.process_key(key(VK_LCONTROL, true), ctx);
    r2.process_key(key(VK_LMENU, true), ctx);
    const ClassifiedKeyEvent c = r2.process_key(key('P', true), ctx);
    expect_str(c.event_id, "", "unrecognized Ctrl+Alt chord stays silent, does not type");
    expect_int(c.priority_tier, 7, "unrecognized Ctrl+Alt chord is tier 7");
}

void test_modifier_tap_isolation() {
    AudioConfig cfg = make_config();
    KeyboardInputRouter r;
    r.apply_config(cfg);
    ForegroundContext ctx = own_window();

    // An isolated Ctrl press sounds on its KeyUp, and only once.
    r.process_key(key(VK_CONTROL, true), ctx);
    ClassifiedKeyEvent tap = r.process_key(key(VK_CONTROL, false), ctx);
    expect_str(tap.event_id, "key.ctrl", "isolated modifier tap fires on KeyUp");
    expect_true(tap.is_modifier_tap, "modifier tap flag set");
    expect_str(r.process_key(key(VK_CONTROL, true), ctx).event_id, "",
               "modifier KeyDown alone is silent");
    r.process_key(key(VK_CONTROL, false), ctx);

    // Ctrl held as part of a chord must not sound on release.
    r.reset_state();
    r.process_key(key(VK_CONTROL, true), ctx);
    r.process_key(key('C', true), ctx);
    r.process_key(key('C', false), ctx);
    expect_str(r.process_key(key(VK_CONTROL, false), ctx).event_id, "",
               "modifier used in a chord does not tap on release");

    // And the whole behaviour is switchable.
    AudioConfig off = make_config();
    off.modifier_tap_sounds_enabled = false;
    KeyboardInputRouter r2;
    r2.apply_config(AudioConfig::validated(off));
    r2.process_key(key(VK_CONTROL, true), ctx);
    expect_str(r2.process_key(key(VK_CONTROL, false), ctx).event_id, "",
               "modifier tap sounds are opt-in");
}

void test_registry_integrity() {
    const auto& events = ptd::canonical_sound_events();
    expect_true(events.size() > 50, "canonical registry is populated");
    bool ids_unique = true;
    for (std::size_t i = 0; i < events.size(); ++i) {
        for (std::size_t j = i + 1; j < events.size(); ++j) {
            if (std::string(events[i].id) == events[j].id) ids_unique = false;
        }
    }
    expect_true(ids_unique, "canonical event ids are unique");
    expect_true(ptd::find_sound_event_descriptor("typing.letter") != nullptr,
                "canonical descriptor lookup works");
    expect_true(ptd::find_sound_event_descriptor("left_down") != nullptr,
                "legacy alias resolves to a descriptor");
    expect_str(ptd::canonicalize_sound_event_id("scroll_up"), "mouse.scroll_up",
               "legacy mouse alias canonicalizes");
    expect_true(ptd::is_valid_event_id("typing.letter"), "canonical id validates");
    expect_true(!ptd::is_valid_event_id("../../etc/passwd"), "path traversal is not an event id");
}

void test_scaled_wheel_accumulation() {
    // Scaled accumulation must agree with plain accumulation at sensitivity 1.
    int acc_a = 0, acc_b = 0;
    expect_int(ptd::accumulate_wheel(acc_a, 120), 1, "one notch up");
    expect_int(ptd::accumulate_wheel_scaled(acc_b, 120, 1.0f), 1, "scaled one notch up");
    expect_int(acc_a, 0, "vertical accumulator settles at zero");
    expect_int(acc_b, 0, "scaled accumulator settles at zero");

    int down_a = 0, down_b = 0;
    expect_int(ptd::accumulate_wheel(down_a, -120), -1, "one notch down");
    expect_int(ptd::accumulate_wheel_scaled(down_b, -120, 1.0f), -1, "scaled one notch down");

    // High sensitivity fires on a smaller threshold; low sensitivity needs more.
    int high = 0, low = 0;
    expect_int(ptd::accumulate_wheel_scaled(high, 40, 4.0f), 1, "high sensitivity on 40 units");
    expect_int(ptd::accumulate_wheel_scaled(low, 40, 0.25f), 0, "low sensitivity ignores 40 units");

    // Sensitivity out of range clamps instead of dividing by zero.
    int clamped = 0;
    expect_int(ptd::accumulate_wheel_scaled(clamped, 480, 0.0f), 1,
               "zero sensitivity clamps to 0.25 and still emits on 480 units");
}

void test_pool_and_gain_arithmetic() {
    std::vector<SoundPoolEntry> pool(3);
    pool[0].sound_id = "a.wav";
    pool[0].weight = 3.0f;
    pool[1].sound_id = "b.wav";
    pool[1].weight = 1.0f;
    pool[2].sound_id = "c.wav";
    pool[2].weight = 9.0f;
    pool[2].enabled = false;

    const std::vector<float> w = ptd::normalized_pool_weights(pool);
    expect_true(w.size() == pool.size(), "weights kept aligned with entries");
    expect_near(w[0] + w[1] + w[2], 1.0, 1e-5, "enabled weights sum to one");
    expect_near(w[0], 0.75, 1e-5, "weight 3 of 4 dominates");
    expect_near(w[1], 0.25, 1e-5, "weight 1 of 4");
    expect_near(w[2], 0.0, 1e-6, "disabled entry contributes nothing");

    const std::vector<float> empty_w = ptd::normalized_pool_weights({});
    expect_true(empty_w.empty(), "empty pool yields no weights");

    expect_near(ptd::db_to_linear_gain(0.0f), 1.0, 1e-6, "0 dB is unity");
    expect_near(ptd::db_to_linear_gain(-6.0f), 0.5012, 1e-3, "-6 dB is half amplitude");
    expect_near(ptd::db_to_linear_gain(100.0f), 1.0, 1e-6, "gain clamps to one");
    expect_near(ptd::db_to_linear_gain(0.0f, 0.0), 0.0, 1e-6, "master volume zero silences");
}

void test_profile_presets_and_defaults() {
    const std::vector<std::string> sounds = {"Click.wav", "Tock.wav", "Ding.wav"};

    AudioConfig minimal;
    minimal.apply_profile_preset(ptd::SoundProfilePreset::Minimal, sounds);
    minimal = AudioConfig::validated(minimal);
    expect_true(minimal.enabled, "Minimal preset turns audio on");
    expect_true(minimal.keyboard_enabled, "Minimal preset enables keyboard routing");
    expect_true(!minimal.typing_sounds_enabled, "Minimal preset leaves typing off");
    expect_near(minimal.volume, 0.70f, 1e-4, "Minimal preset lowers volume");
    expect_str(minimal.effective_event_config("mouse.left_down").sound_id, "Click.wav",
               "Minimal preset assigns a click sound");

    AudioConfig typing;
    typing.apply_profile_preset(ptd::SoundProfilePreset::KeyboardFocused, sounds);
    typing = AudioConfig::validated(typing);
    expect_true(typing.keyboard_enabled, "KeyboardFocused preset enables keyboard");
    expect_true(typing.typing_sounds_enabled, "KeyboardFocused preset enables typing sounds");
    expect_str(typing.effective_event_config("typing.letter").sound_id, "Click.wav",
               "KeyboardFocused preset falls back to the first catalog sound when Tap.wav is absent");
    const AudioConfig with_tap_pool = [&] {
        AudioConfig c;
        c.apply_profile_preset(ptd::SoundProfilePreset::KeyboardFocused,
                               {"Click.wav", "Tap.wav", "Chime.wav"});
        return AudioConfig::validated(c);
    }();
    expect_str(with_tap_pool.effective_event_config("typing.letter").sound_id, "Tap.wav",
               "KeyboardFocused preset prefers Tap.wav when present");

    AudioConfig full;
    full = make_config();
    full.apply_profile_preset(ptd::SoundProfilePreset::Custom, sounds);
    full = AudioConfig::validated(full);
    expect_str(full.effective_event_config("typing.letter").sound_id, "protrail_default.wav",
               "Custom preset leaves assignments untouched");

    // Every persisted T-120 key must survive a validation round trip.
    AudioConfig sentinel = make_config();
    sentinel.recent_sounds = {"Click.wav"};
    sentinel.preview_on_selection = false;
    sentinel.preview_debounce_ms = 200;
    sentinel.typing_repeat_cooldown_ms = 250;
    sentinel.suppress_repeat_typing = true;
    sentinel.excluded_processes = {"keepass.exe"};
    sentinel.keyboard_scope = KeyboardScope::SystemWide;
    sentinel.horizontal_scroll_enabled = false;
    sentinel.scroll_sensitivity = 2.5f;
    sentinel.scroll_cooldown_ms = 300;
    sentinel.scroll_max_rate_hz = 90;
    sentinel.scroll_pitch_variation = 0.25f;
    sentinel.active_profile = ptd::SoundProfilePreset::Custom;
    const AudioConfig round = AudioConfig::validated(sentinel);
    expect_str(round.recent_sounds.empty() ? "" : round.recent_sounds.front(), "Click.wav",
               "recent sounds persist");
    expect_true(round.recent_sounds.size() == 1, "recent sound count persists");
    expect_true(round.preview_on_selection == false, "preview toggle persists");
    expect_int(round.preview_debounce_ms, 200, "preview debounce persists");
    expect_int(round.typing_repeat_cooldown_ms, 250, "typing cooldown persists");
    expect_true(round.keyboard_scope == KeyboardScope::SystemWide, "scope persists");
    expect_true(!round.horizontal_scroll_enabled, "horizontal scroll toggle persists");
    expect_near(round.scroll_sensitivity, 2.5f, 1e-4, "scroll sensitivity persists");
    expect_int(round.scroll_cooldown_ms, 300, "scroll cooldown persists");
    expect_int(round.scroll_max_rate_hz, 90, "scroll max rate persists");
    expect_near(round.scroll_pitch_variation, 0.25f, 1e-4, "scroll pitch variation persists");
    expect_true(round.excluded_processes.size() == 1 &&
                round.excluded_processes.front() == "keepass.exe",
               "excluded process persists");
    expect_true(round.active_profile == ptd::SoundProfilePreset::Custom,
               "active profile persists");
}

void test_event_config_validation_clamps() {
    EventSoundConfig ec;
    ec.gain_db = 99.0f;
    ec.cooldown_ms = 999999;
    ec.priority = -5;
    ec.polyphony_allowance = 99;
    ec.pitch_variation = 9.0f;
    ec.enabled = true;
    ec.sound_id = "ok.wav";
    const EventSoundConfig v = EventSoundConfig::validated(ec);
    expect_near(v.gain_db, EventSoundConfig::kMaxGainDb, 1e-4, "gain clamps high");
    expect_int(v.cooldown_ms, EventSoundConfig::kMaxCooldownMs, "cooldown clamps");
    expect_int(v.priority, EventSoundConfig::kMinPriority, "priority clamps low");
    expect_int(v.polyphony_allowance, EventSoundConfig::kMaxPolyphony, "polyphony clamps");
    expect_near(v.pitch_variation, EventSoundConfig::kMaxPitchVariation, 1e-4, "pitch clamps");
    expect_str(v.sound_id, "ok.wav", "valid sound id survives");

    EventSoundConfig bad;
    bad.sound_id = "../../secrets.wav";
    bad.enabled = true;
    expect_str(EventSoundConfig::validated(bad).sound_id, "",
               "relative path escape is rejected");
}

void test_process_name_sanitization() {
    expect_str(ptd::sanitize_process_name("NOTEPAD.EXE"), "notepad.exe", "process name lowercased");
    expect_str(ptd::sanitize_process_name(""), "", "empty process name rejected");
    expect_str(ptd::sanitize_process_name("..\\evil.exe"), "", "relative escape rejected");
}

void test_focus_loss_clears_stuck_state() {
    AudioConfig cfg = make_config();
    cfg.set_event_config("typing.repeat", EventSoundConfig{});
    KeyboardInputRouter r;
    r.apply_config(AudioConfig::validated(cfg));

    r.process_key(key(VK_CONTROL, true), own_window());
    expect_true(r.ctrl_down(), "ctrl recorded as held");
    // A foreground change must clear held-key state so a chord started in one
    // window never completes in another.
    r.process_key(key('A', true), other_window("notepad.exe"));
    expect_true(!r.ctrl_down(), "focus change clears held modifiers");
    r.process_key(key('K', true), own_window());
    expect_str(r.process_key(key('K', true), own_window()).event_id, "typing.letter",
               "stale modifier no longer forms a chord after focus change");
}

void test_app_only_local_feed_reaches_router() {
    // The AppOnly scope has no raw-input sink, so ProTrail's own window is the
    // ONLY event source. handle_local_key_event needs no HWND, which is what
    // makes this contract testable without a window.
    ptd::KeyboardInput kb;
    int calls = 0;
    std::string last_id;
    kb.set_event_callback([&](int64_t, const ptd::ClassifiedKeyEvent& ev) {
        ++calls;
        last_id = ev.event_id;
    });

    AudioConfig cfg = make_config();
    cfg.keyboard_scope = KeyboardScope::AppOnly;
    kb.apply_config(AudioConfig::validated(cfg));
    expect_true(!kb.global_sink_registered(),
                "AppOnly scope registers no global raw-input sink");
    expect_true(kb.valid() == false, "no window was created for the app-local path");

    kb.handle_local_key_event('A', 0x1E, false, true, 1000);
    expect_int(calls, 1, "app-local key press reaches the callback");
    expect_str(last_id, "typing.letter", "app-local press classifies through the router");

    // A repeated packet and a release must not add events.
    kb.handle_local_key_event('A', 0x1E, false, true, 2000);
    kb.handle_local_key_event('A', 0x1E, false, false, 3000);
    expect_int(calls, 2, "auto-repeat still types, release is silent");
    expect_true(kb.packet_count() == 3, "every local packet is counted");
    expect_true(kb.emitted_count() == 2, "only classified events are emitted");

    // A switch to SystemWide would register the sink; the router must not be
    // the thing that decides capture, and this test still proves the AppOnly
    // default is the safe one.
    AudioConfig sw = make_config();
    sw.keyboard_scope = KeyboardScope::SystemWide;
    kb.apply_config(AudioConfig::validated(sw));
    expect_true(!kb.global_sink_registered(),
                "SystemWide without a window cannot register a sink (fails silent)");
}

} // namespace

void test_fkey_overflow_safety() {
    // T-127: canonicalize_key_token must never throw on an over-long F-key digit
    // run. The old std::stoi(low.substr(1)) path raised std::out_of_range for
    // values beyond INT_MAX and would have terminated the GUI keyboard thread.
    expect_str(KeyboardInputRouter::normalize_chord_string("f1"), "F1", "F1 canonicalizes");
    expect_str(KeyboardInputRouter::normalize_chord_string("F1"), "F1", "F1 uppercased");
    expect_str(KeyboardInputRouter::normalize_chord_string("f24"), "F24", "F24 canonicalizes");
    expect_str(KeyboardInputRouter::normalize_chord_string("f0"), "f0", "F0 is below the F1..F24 range and returns the raw token");
    expect_str(KeyboardInputRouter::normalize_chord_string("f25"), "f25", "F25 exceeds the F24 range and returns the raw token");
    expect_str(KeyboardInputRouter::normalize_chord_string("f100"), "f100", "F100 exceeds the range and returns the raw token");
    // Arbitrarily long suffix must not throw or abort the process.
    expect_str(KeyboardInputRouter::normalize_chord_string("f99999999999999999999"),
               "f99999999999999999999", "extremely long f-key suffix does not throw");
}

int main() {
    test_fkey_overflow_safety();
    test_chord_formatting_and_normalization();
    test_specific_key_and_typing_classification();
    test_arbitration_priority();
    test_named_shortcut_and_chord_fallthrough_rules();
    test_typing_categories_and_repeat();
    test_specific_key_wins_over_typing_when_tier_four_falls_back();
    test_scope_exclusion_and_sensitive_gates();
    test_altgr_disambiguation_synthetic_lctrl();
    test_altgr_disambiguation_held_lctrl_plus_ralt();
    test_modifier_tap_isolation();
    test_registry_integrity();
    test_scaled_wheel_accumulation();
    test_pool_and_gain_arithmetic();
    test_profile_presets_and_defaults();
    test_event_config_validation_clamps();
    test_process_name_sanitization();
    test_app_only_local_feed_reaches_router();
    test_focus_loss_clears_stuck_state();

    if (g_failures != 0) {
        std::printf("keyboard_sound_tests: %d of %d checks FAILED\n", g_failures, g_checks);
        return 1;
    }
    std::printf("keyboard_sound_tests: all %d checks passed\n", g_checks);
    return 0;
}
