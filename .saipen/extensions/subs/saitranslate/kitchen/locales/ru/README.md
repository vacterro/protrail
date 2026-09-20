# ProTrail

[English](README.md) | [Eesti](README.ee.md) | [Дед](README.ded.md) | [日本語](README.ja.md) | [Русский](README.ru.md) | [Українська](README.uk.md)

Нативное приложение эффектов курсора для Windows. Полный план — в `ROADMAP/`.

## Сборка

Требования (Windows 10/11 x64):

- Visual Studio 2022 Build Tools с рабочей нагрузкой "Desktop development with C++" (MSVC 14.4x, CMake 3.24+).
- Qt 6.8 MSVC 2022 64-bit, установленный в `C:\Qt\6.8.0\msvc2022_64` (другой путь — через `-DCMAKE_PREFIX_PATH`).
  Установка без онлайн-установщика: `python -m aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -O C:\Qt`.

Настройка (из "Developer Command Prompt" / после вызова `vcvars64.bat`):

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
```

Сборка:

```bat
cmake --build build --config Release
cmake --build build --config Debug
```

Запуск тестов:

```bat
build\Release\protrail_tests.exe
```

Runtime-DLL Qt загружаются по стандартному пути поиска Qt: добавьте `C:\Qt\6.8.0\msvc2022_64\bin`
и `C:\Qt\6.8.0\msvc2022_64\plugins\platforms` в `PATH`, либо скопируйте `Qt6Core.dll`, `Qt6Gui.dll`,
`Qt6Widgets.dll` и плагин `platforms\qwindows.dll` рядом с исполняемыми файлами.

Результат: `build\Release\protrail.exe`. Права администратора не требуются.

## Состояние

- MVP `00_PROJECT_BOOTSTRAP`: ЗАКРЫТ.
- MVP `01_NATIVE_OVERLAY`: ЗАКРЫТ — нативный прозрачный оверлей D2D/DComp, пропускающий клики
  (рецепт хит-теста `WS_EX_LAYERED` + `WS_EX_TRANSPARENT`, swapchain DirectComposition;
  проверено `tests/hittest_probe.ps1` и ручной проверкой).
- MVP `02_MOUSE_INPUT_AND_SAMPLING`: ЗАКРЫТ — глобальный raw-input (RIDEV_INPUTSINK,
  message-only окно), питающий ограниченный `CursorHistory` (512 сэмплов, объединённые
  дубликаты, сохранённые переходы кнопок, метки времени QPC).
- MVP `03_BASIC_TRAIL`: ЗАКРЫТ — проверенный пользователем след (жёлтый, ~350 мс жизни,
  центростремительный Catmull-Rom с непрерывным сглаживанием 0..1, затухание по времени,
  планировщик только при активности; регрессия `tests/test_trail_effect.cpp`).
- MVP `04_CLICK_BUBBLE`: ЗАКРЫТ — проверенный пользователем пузырь клика (голубое кольцо, ease-out
  8→26 px / 250 мс / 0.85, ограниченный жизненный цикл 128 пузырей, общий
  планировщик только при активности; `tests/test_click_bubble_effect.cpp` — 107 проверок).
- MVP `05_SETTINGS_GUI`: ЗАКРЫТ — проверенный пользователем живой GUI настроек (General/Trail/Click,
  тема Golden Default, интерактивные цветовые образцы с QColorDialog, фаска 2px,
  синхронизация слайдер+спин, планировщик только при активности; `tests/test_settings_window.cpp` — 35 проверок).
- MVP `06_CONFIGURATION_AND_PERSISTENCE`: ЗАКРЫТ — долговечные JSON-настройки в
  `%LOCALAPPDATA%\ProTrail\config.json`, атомарная запись ReplaceFileW, безопасный
  откат к значениям по умолчанию с резервной копией `.corrupt`, потокобезопасная публикация (`tests/test_config.cpp` — 7 тестов).
- MVP `07_RENDER_SCHEDULER_AND_PERFORMANCE`: ЗАКРЫТ — формальный `RenderScheduler`, работающий только при
  активности, с привязкой к частоте обновления дисплея (EnumDisplaySettings), стабильным delta-time,
  0 пробуждений в простое, счётчиками производительности и замерами без утечек (`tests/test_render_scheduler.cpp` — 10 тестов).
- MVP `08_MULTI_MONITOR_AND_DPI`: ЗАКРЫТ — архитектура оверлея на каждый монитор, явное
  `PER_MONITOR_AWARE_V2` DPI-осознание, преобразование в физические пиксели, отложенная реконсиляция топологии,
  регрессионный набор по нескольким мониторам (`tests/test_multimonitor.cpp` — 28 тестов).
- MVP `09_TRAY`: ЗАКРЫТ — значок в системном трее, жизненный цикл SettingsWindow со скрытием при закрытии,
  контекстное меню трея (Settings, Enable/Disable, Exit), чистое завершение приложения без процессов-сирот
  (`tests/test_tray.cpp` — 12 тестов).

### Состояние ядра MVP

Ядро MVP (этапы 00–09) завершено и полностью закрыто.
Все функциональные и приёмочные критерии проверены обоими автоматическими наборами тестов (сборка /W4 /WX чистая, CTest 12/12 PASS) и живым тестированием пользователем на нескольких мониторах и в трее.

### Расширения после MVP

- Post-MVP `V1_COLORS_AND_PRESETS`: ЗАКРЫТ — два цвета следа (Start/Fade), палитры из 14 образцов, свой `QColorDialog`, 4 режима цвета следа (Full, Start only, Fade only, Gradient), палитра из 14 образцов для клика, 6 быстрых пресетов (Classic, Fire, Ice, Neon, Toxic, Violet), обратно совместимая миграция схемы v2.
- Post-MVP `V2_TRAIL_EFFECTS`: ЗАКРЫТ — 8 стилей следа (Classic, Soft Glow, Comet, Neon, Dotted, Pulse, Ribbon, Spark), параметры силы свечения и шага сегментов, математика эффектов с учётом стиля и политика штриха рендерера, схема v3.
- Post-MVP `V3_CLICK_EFFECTS`: ЗАКРЫТ — 7 стилей клика (Ring, Double Ring, Ripple, Burst, Spark Burst, Soft Flash, Dot + Ring), количество частиц (0..24), схема v4, изолированное смоук-тестирование без изменения рабочего состояния.
- Post-MVP `V4_SYSTEM_STABILITY`: ЗАКРЫТ — мьютекс одного экземпляра (`Local\ProTrail_SingleInstance_Mutex`) с активацией окна через `ProTrail_ActivateInstance`, обработка и восстановление после потери устройства Direct2D/DXGI (`D2DERR_RECREATE_TARGET`, `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`) с сохранением HWND, мгновенное восстановление кнопкой `Restore All Defaults`.

<!-- source-digest: README.md sha256:0b904f769d59910f -->
