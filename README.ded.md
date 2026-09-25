# ProTrail

**v0.1.5**

[English](README.md) | [Eesti](README.ee.md) | [Дед](README.ded.md) | [日本語](README.ja.md) | [Русский](README.ru.md) | [Українська](README.uk.md)

Курсорные эффекты для Windows. Нативно, без админских прав. Полный план — `ROADMAP/`.

## Сборка

Что нужно (Windows 10/11 x64):

- VS 2022 Build Tools, нагрузка "Desktop development with C++" (MSVC 14.4x, CMake 3.24+).
- Qt 6.8 MSVC 2022 64-bit в `C:\Qt\6.8.0\msvc2022_64` (иначе `-DCMAKE_PREFIX_PATH`).
  Без онлайн-установщика: `python -m aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -O C:\Qt`.

Настройка (Developer Command Prompt или после `vcvars64.bat`):

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
```

Сборка:

```bat
cmake --build build --config Release
cmake --build build --config Debug
```

Тесты:

```bat
build\Release\protrail_tests.exe
```

DLL-ки Qt берутся обычным поиском Qt: положи `C:\Qt\6.8.0\msvc2022_64\bin` и
`C:\Qt\6.8.0\msvc2022_64\plugins\platforms` в `PATH`, либо скопируй `Qt6Core.dll`, `Qt6Gui.dll`,
`Qt6Widgets.dll` и плагин `platforms\qwindows.dll` к экзешникам.

Выход: `build\Release\protrail.exe`.

## Что закрыто

- `00_PROJECT_BOOTSTRAP`: ЗАКРЫТ.
- `01_NATIVE_OVERLAY`: ЗАКРЫТ — прозрачный оверлей D2D/DComp, клики насквозь (`WS_EX_LAYERED` +
  `WS_EX_TRANSPARENT`, swapchain DirectComposition; проверено `tests/hittest_probe.ps1` и руками).
- `02_MOUSE_INPUT_AND_SAMPLING`: ЗАКРЫТ — глобальный raw-input (RIDEV_INPUTSINK, message-only окно),
  буфер `CursorHistory` (512 проб, дубликаты схлопнуты, переходы кнопок целы, метки QPC).
- `03_BASIC_TRAIL`: ЗАКРЫТ — след (жёлтый, ~350 мс, Catmull-Rom с непрерывным сглаживанием 0..1,
  затухание по времени, планировщик только при активности; `tests/test_trail_effect.cpp`).
- `04_CLICK_BUBBLE`: ЗАКРЫТ — пузырь клика (голубое кольцо, ease-out 8→26 px / 250 мс / 0.85,
  не больше 128 пузырей; `tests/test_click_bubble_effect.cpp` — 107 проверок).
- `05_SETTINGS_GUI`: ЗАКРЫТ — живой GUI настроек (General/Trail/Click, тема Golden Default,
  образцы с QColorDialog, фаска 2px, слайдер и спин в паре; `tests/test_settings_window.cpp` — 35 проверок).
- `06_CONFIGURATION_AND_PERSISTENCE`: ЗАКРЫТ — настройки в `%LOCALAPPDATA%\ProTrail\config.json`,
  атомарная запись ReplaceFileW, мусорный файл отбрасывается с копией `.corrupt`
  (`tests/test_config.cpp` — 7 тестов).
- `07_RENDER_SCHEDULER_AND_PERFORMANCE`: ЗАКРЫТ — `RenderScheduler` по частоте экрана
  (EnumDisplaySettings), стабильный delta-time, 0 пробуждений в простое, счётчики без утечек
  (`tests/test_render_scheduler.cpp` — 10 тестов).
- `08_MULTI_MONITOR_AND_DPI`: ЗАКРЫТ — оверлей на каждый монитор, явный `PER_MONITOR_AWARE_V2`,
  физические пиксели, отложенная реакция на топологию (`tests/test_multimonitor.cpp` — 28 тестов).
- `09_TRAY`: ЗАКРЫТ — значок в трее, закрытие прячет окно, меню трея (Settings, Enable/Disable, Exit),
  выход без висяков (`tests/test_tray.cpp` — 12 тестов).

### Тюрьма ядра

Ядро MVP (00–09) закрыто целиком. Оба автокомплекта чисты (/W4 /WX, CTest 12/12 PASS), плюс живая
проверка на трёх мониторах и в трее.

### После MVP

- `V1_COLORS_AND_PRESETS`: ЗАКРЫТ — два цвета следа (Start/Fade), палитры по 14 образцов, свой `QColorDialog`, 4 режима цвета (Full, Start only, Fade only, Gradient), палитра клика, 6 пресетов (Classic, Fire, Ice, Neon, Toxic, Violet), схема v2.
- `V2_TRAIL_EFFECTS`: ЗАКРЫТ — 8 стилей следа (Classic, Soft Glow, Comet, Neon, Dotted, Pulse, Ribbon, Spark), сила свечения и шаг сегментов, математика по стилю, политика штриха, схема v3.
- `V3_CLICK_EFFECTS`: ЗАКРЫТ — 7 стилей клика (Ring, Double Ring, Ripple, Burst, Spark Burst, Soft Flash, Dot + Ring), частицы 0..24, схема v4, смоук без порчи рабочего состояния.
- `V4_SYSTEM_STABILITY`: ЗАКРЫТ — мьютекс одного экземпляра (`Local\ProTrail_SingleInstance_Mutex`), активация окна через `ProTrail_ActivateInstance`, восстановление после потери устройства D2D/DXGI (`D2DERR_RECREATE_TARGET`, `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`) с сохранением HWND, кнопка `Restore All Defaults`.

<!-- source-digest: README.md sha256:9edadbe96fce3ad2 -->
