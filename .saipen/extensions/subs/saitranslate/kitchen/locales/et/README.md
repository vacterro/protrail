# ProTrail

[English](README.md) | [Eesti](README.ee.md) | [Дед](README.ded.md) | [日本語](README.ja.md) | [Русский](README.ru.md) | [Українська](README.uk.md)

Natiivne Windowsi kursoriefektide rakendus. Kogu plaan asub kaustas `ROADMAP/`.

## Ehitamine

Nõuded (Windows 10/11 x64):

- Visual Studio 2022 Build Tools koos töökoormusega "Desktop development with C++" (MSVC 14.4x, CMake 3.24+).
- Qt 6.8 MSVC 2022 64-bit, paigaldatud kausta `C:\Qt\6.8.0\msvc2022_64` (muu tee korral kasuta `-DCMAKE_PREFIX_PATH`).
  Paigaldus ilma veebipõhise paigaldajata: `python -m aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -O C:\Qt`.

Seadistamine (Developer Command Prompt aknast / pärast `vcvars64.bat` kutsumist):

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
```

Ehitus:

```bat
cmake --build build --config Release
cmake --build build --config Debug
```

Testide käivitamine:

```bat
build\Release\protrail_tests.exe
```

Qt runtime-DLL-id laaditakse Qt tavalise otsingutee kaudu: lisa `PATH`-i `C:\Qt\6.8.0\msvc2022_64\bin`
ja `C:\Qt\6.8.0\msvc2022_64\plugins\platforms`, või kopeeri `Qt6Core.dll`, `Qt6Gui.dll`,
`Qt6Widgets.dll` ning plugin `platforms\qwindows.dll` käivitatavate failide kõrvale.

Väljund: `build\Release\protrail.exe`. Administraatori õigusi ei ole vaja.

## Seis

- MVP `00_PROJECT_BOOTSTRAP`: SULETUD.
- MVP `01_NATIVE_OVERLAY`: SULETUD — natiivne läbipaistev, klikke läbilaskev D2D/DComp ülekate
  (`WS_EX_LAYERED` + `WS_EX_TRANSPARENT` tabamustesti retsept, DirectCompositioni swapchain;
  kontrollitud failiga `tests/hittest_probe.ps1` ja käsitsi).
- MVP `02_MOUSE_INPUT_AND_SAMPLING`: SULETUD — globaalne raw-input (RIDEV_INPUTSINK,
  message-only aken), mis toidab piiratud `CursorHistory` puhvrit (512 proovi, ühendatud
  duplikaadid, säilitatud nupumuutused, QPC ajatemplid).
- MVP `03_BASIC_TRAIL`: SULETUD — kasutaja poolt kinnitatud jälg (kollane, ~350 ms eluiga,
  tsentripetaalne Catmull-Rom pideva silumisega 0..1, ajapõhine hääbumine,
  ainult aktiivsuse ajal töötav planeerija; regressioon `tests/test_trail_effect.cpp`).
- MVP `04_CLICK_BUBBLE`: SULETUD — kasutaja poolt kinnitatud klikimull (tsüaansinine rõngas, ease-out
  8→26 px / 250 ms / 0.85, piiratud 128 mulli elutsükkel, ühine
  ainult aktiivsuse ajal töötav planeerija; `tests/test_click_bubble_effect.cpp` — 107 kontrolli).
- MVP `05_SETTINGS_GUI`: SULETUD — kasutaja poolt kinnitatud reaalajas seadete GUI (General/Trail/Click,
  Golden Default teema, interaktiivsed värvinäidised QColorDialogiga, 2px kallak,
  liuguri ja sisestusvälja sünkroon, ainult aktiivsuse ajal töötav planeerija; `tests/test_settings_window.cpp` — 35 kontrolli).
- MVP `06_CONFIGURATION_AND_PERSISTENCE`: SULETUD — püsivad JSON-seaded asukohas
  `%LOCALAPPDATA%\ProTrail\config.json`, aatomiline ReplaceFileW kirjutamine, rikutud faili
  tagasilangus varukoopiaga `.corrupt`, lõimkindel avaldamine (`tests/test_config.cpp` — 7 testi).
- MVP `07_RENDER_SCHEDULER_AND_PERFORMANCE`: SULETUD — vormiline `RenderScheduler`, mis töötab ainult
  aktiivsuse ajal, ekraani värskendussageduse rütmis (EnumDisplaySettings), stabiilne delta-time,
  0 ärkamist jõudeolekus, jõudluse loendurid ja lekkevabad ressursimõõtmised (`tests/test_render_scheduler.cpp` — 10 testi).
- MVP `08_MULTI_MONITOR_AND_DPI`: SULETUD — monitoripõhine ülekattearhitektuur, selgesõnaline
  `PER_MONITOR_AWARE_V2` DPI-teadlikkus, füüsilise piksli teisendus, edasilükatud topoloogia ühildamine,
  mitme monitori regressioonikomplekt (`tests/test_multimonitor.cpp` — 28 testi).
- MVP `09_TRAY`: SULETUD — süsteemisalve ikoon, sulgemisel peituv SettingsWindow elutsükkel, salve
  kontekstimenüü (Settings, Enable/Disable, Exit), puhas rakenduse sulgemine ilma orvuks jäänud protsessideta
  (`tests/test_tray.cpp` — 12 testi).

### MVP tuumiku seis

MVP tuumik (etapid 00–09) on valmis ja täielikult suletud.
Kõik funktsionaalsed ja vastuvõtukriteeriumid on kontrollitud mõlemas automaatses testikomplektis (/W4 /WX puhas, CTest 12/12 PASS) ning kasutaja reaalses mitme monitori ja salve testimises.

### Pärast-MVP laiendused

- Post-MVP `V1_COLORS_AND_PRESETS`: SULETUD — kaks jäljevärvi (Start/Fade), 14 näidisega paletid, oma `QColorDialog`, 4 jälje värvirežiimi (Full, Start only, Fade only, Gradient), klikile 14 näidisega palett, 6 kiirpresetit (Classic, Fire, Ice, Neon, Toxic, Violet), tagasiühilduv skeemi v2 migratsioon.
- Post-MVP `V2_TRAIL_EFFECTS`: SULETUD — 8 jäljestiiili (Classic, Soft Glow, Comet, Neon, Dotted, Pulse, Ribbon, Spark), hõõgumise tugevuse ja segmendivahe parameetrid, stiiliteadlik efektimatemaatika ja renderdaja joonepoliitika, skeem v3.
- Post-MVP `V3_CLICK_EFFECTS`: SULETUD — 7 klikistiili (Ring, Double Ring, Ripple, Burst, Spark Burst, Soft Flash, Dot + Ring), osakeste arv (0..24), skeem v4, isoleeritud suitsutestimine ilma 0 muudatuseta tööseisundis.
- Post-MVP `V4_SYSTEM_STABILITY`: SULETUD — ühe eksemplari mutex (`Local\ProTrail_SingleInstance_Mutex`) akna aktiveerimisega `ProTrail_ActivateInstance` kaudu, Direct2D/DXGI seadme kadumise käsitlemine ja taastamine (`D2DERR_RECREATE_TARGET`, `DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`) HWND-d säilitades, `Restore All Defaults` hetkeline taastamine.

<!-- source-digest: README.md sha256:0b904f769d59910f -->
