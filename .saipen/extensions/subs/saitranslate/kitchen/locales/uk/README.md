# ProTrail

[English](README.md) | [Eesti](README.ee.md) | [Дед](README.ded.md) | [日本語](README.ja.md) | [Русский](README.ru.md) | [Українська](README.uk.md)

Нативний застосунок ефектів курсора для Windows. Повний план — у `ROADMAP/`.

## Збірка

Вимоги (Windows 10/11 x64):

- Visual Studio 2022 Build Tools з робочим навантаженням "Desktop development with C++" (MSVC 14.4x, CMake 3.24+).
- Qt 6.8 MSVC 2022 64-bit, встановлений у `C:\Qt\6.8.0\msvc2022_64` (інший шлях — через `-DCMAKE_PREFIX_PATH`).
  Встановлення без онлайн-інсталятора: `python -m aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -O C:\Qt`.

Налаштування (з "Developer Command Prompt" / після виклику `vcvars64.bat`):

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
```

Збірка:

```bat
cmake --build build --config Release
cmake --build build --config Debug
```

Запуск тестів:

```bat
build\Release\protrail_tests.exe
```

Runtime-бібліотеки Qt завантажуються за стандартним шляхом пошуку Qt: додайте `C:\Qt\6.8.0\msvc2022_64\bin`
та `C:\Qt\6.8.0\msvc2022_64\plugins\platforms` до `PATH`, або скопіюйте `Qt6Core.dll`, `Qt6Gui.dll`,
`Qt6Widgets.dll` і плагін `platforms\qwindows.dll` поряд з виконуваними файлами.

Результат: `build\Release\protrail.exe`. Права адміністратора не потрібні.

## Стан

- MVP `00_PROJECT_BOOTSTRAP`: ЗАКРИТО.
- MVP `01_NATIVE_OVERLAY`: ЗАКРИТО — нативний прозорий оверлей D2D/DComp, що пропускає кліки
  (рецепт хіт-тесту `WS_EX_LAYERED` + `WS_EX_TRANSPARENT`, swapchain DirectComposition;
  перевірено `tests/hittest_probe.ps1` і вручну).
- MVP `02_MOUSE_INPUT_AND_SAMPLING`: ЗАКРИТО — глобальний raw-input (RIDEV_INPUTSINK,
  message-only вікно), що живить обмежений `CursorHistory` (512 семплів, об'єднані
  дублікати, збережені переходи кнопок, мітки часу QPC).
- MVP `03_BASIC_TRAIL`: ЗАКРИТО — перевірений користувачем слід (жовтий, ~350 мс життя,
  центрострімкий Catmull-Rom із безперервним згладжуванням 0..1, затухання за часом,
  планувальник лише під час активності; регресія `tests/test_trail_effect.cpp`).
- MVP `04_CLICK_BUBBLE`: ЗАКРИТО — перевірений користувачем клік-бульбашка (блакитне кільце, ease-out
  8→26 px / 250 мс / 0.85, обмежений життєвий цикл 128 бульбашок, спільний
  планувальник лише під час активності; `tests/test_click_bubble_effect.cpp` — 107 перевірок).
- MVP `05_SETTINGS_GUI`: ЗАКРИТО — перевірений користувачем живий GUI налаштувань (General/Trail/Click,
  тема Golden Default, інтерактивні зразки кольорів з QColorDialog, фаска 2px,
  синхронізація повзунка та поля, планувальник лише під час активності; `tests/test_settings_window.cpp` — 35 перевірок).
- MVP `06_CONFIGURATION_AND_PERSISTENCE`: ЗАКРИТО — тривкі JSON-налаштування у
  `%LOCALAPPDATA%\ProTrail\config.json`, атомарний запис ReplaceFileW, безпечний
  відкат до типових значень із резервною копією `.corrupt`, потокобезпечна публікація (`tests/test_config.cpp` — 7 тестів).
- MVP `07_RENDER_SCHEDULER_AND_PERFORMANCE`: ЗАКРИТО — формальний `RenderScheduler`, що працює лише
  під час активності, у ритмі частоти оновлення дисплея (EnumDisplaySettings), стабільний delta-time,
  0 пробуджень у простої, лічильники продуктивності та вимірювання без витоків (`tests/test_render_scheduler.cpp` — 10 тестів).
- MVP `08_MULTI_MONITOR_AND_DPI`: ЗАКРИТО — архітектура оверлея на кожен монітор, явне
  `PER_MONITOR_AWARE_V2` DPI-усвідомлення, перетворення у фізичні пікселі, відкладена реконсиляція топології,
  регресійний набір для кількох моніторів (`tests/test_multimonitor.cpp` — 28 тестів).
- MVP `09_TRAY`: ЗАКРИТО — іконка в системному лотку, життєвий цикл SettingsWindow із приховуванням при закритті,
  контекстне меню лотка (Settings, Enable/Disable, Exit), чисте завершення застосунку без процесів-сиріт
  (`tests/test_tray.cpp` — 12 тестів).

### Стан ядра MVP

Ядро MVP (етапи 00–09) завершено й повністю закрито.
Усі функціональні та приймальні критерії перевірено обома автоматичними наборами тестів (збірка /W4 /WX чиста, CTest 12/12 PASS) і живим тестуванням користувачем на кількох моніторах та в лотку.

### Розширення після MVP

- Post-MVP `V1_COLORS_AND_PRESETS`: ЗАКРИТО — два кольори сліду (Start/Fade), палітри з 14 зразків, власний `QColorDialog`, 4 режими кольору сліду (Full, Start only, Fade only, Gradient), палітра з 14 зразків для кліку, 6 швидких пресетів (Classic, Fire, Ice, Neon, Toxic, Violet), зворотно сумісна міграція схеми v2.
- Post-MVP `V2_TRAIL_EFFECTS`: ЗАКРИТО — 8 стилів сліду (Classic, Soft Glow, Comet, Neon, Dotted, Pulse, Ribbon, Spark), параметри сили світіння та кроку сегментів, математика ефектів з урахуванням стилю та політика штриха рендерера, схема v3.
- Post-MVP `V3_CLICK_EFFECTS`: ЗАКРИТО — 7 стилів кліку (Ring, Double Ring, Ripple, Burst, Spark Burst, Soft Flash, Dot + Ring), кількість частинок (0..24), схема v4, ізольоване дим-тестування без зміни робочого стану.
- Post-MVP `V4_SYSTEM_STABILITY`: ЗАКРИТО — м'ютекс одного екземпляра (`Local\ProTrail_SingleInstance_Mutex`) з активацією вікна через `ProTrail_ActivateInstance`, обробка та відновлення після втрати пристрою Direct2D/DXGI (`D2DERR_RECREATE_TARGET`, `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`) зі збереженням HWND, миттєве відновлення кнопкою `Restore All Defaults`.

<!-- source-digest: README.md sha256:0b904f769d59910f -->
