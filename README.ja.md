# ProTrail

**v0.1.6**

[English](README.md) | [Eesti](README.ee.md) | [Дед](README.ded.md) | [日本語](README.ja.md) | [Русский](README.ru.md) | [Українська](README.uk.md)

Windows ネイティブのカーソルエフェクトアプリケーション。全体計画は `ROADMAP/` を参照。

## ビルド

要件 (Windows 10/11 x64):

- Visual Studio 2022 Build Tools とワークロード "Desktop development with C++" (MSVC 14.4x、CMake 3.24 以上)。
- Qt 6.8 MSVC 2022 64-bit、`C:\Qt\6.8.0\msvc2022_64` にインストール (別の場所なら `-DCMAKE_PREFIX_PATH` で指定)。
  オンラインインストーラーを使わない場合: `python -m aqt install-qt windows desktop 6.8.0 win64_msvc2022_64 -O C:\Qt`。

構成 ("Developer Command Prompt" から、または `vcvars64.bat` 実行後):

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
```

ビルド:

```bat
cmake --build build --config Release
cmake --build build --config Debug
```

テスト実行:

```bat
build\Release\protrail_tests.exe
```

Qt のランタイム DLL は標準の Qt 検索パスから読み込まれます。`C:\Qt\6.8.0\msvc2022_64\bin`
と `C:\Qt\6.8.0\msvc2022_64\plugins\platforms` を `PATH` に追加するか、`Qt6Core.dll`、`Qt6Gui.dll`、
`Qt6Widgets.dll` とプラグイン `platforms\qwindows.dll` を実行ファイルの隣にコピーしてください。

出力: `build\Release\protrail.exe`。管理者権限は不要です。

## 状態

- MVP `00_PROJECT_BOOTSTRAP`: クローズ。
- MVP `01_NATIVE_OVERLAY`: クローズ — ネイティブで透明、クリック透過の D2D/DComp オーバーレイ
  (`WS_EX_LAYERED` + `WS_EX_TRANSPARENT` のヒットテスト手順、DirectComposition スワップチェーン。
  `tests/hittest_probe.ps1` と手動確認で検証済み)。
- MVP `02_MOUSE_INPUT_AND_SAMPLING`: クローズ — グローバル raw-input (RIDEV_INPUTSINK、
  message-only ウィンドウ) が上限付き `CursorHistory` を供給 (512 サンプル、重複の統合、
  ボタン遷移の保持、QPC タイムスタンプ)。
- MVP `03_BASIC_TRAIL`: クローズ — ユーザー確認済みのトレイル (黄色、約 350 ms の寿命、
  連続スムージング 0..1 の求心 Catmull-Rom、時間ベースのフェード、
  アクティブ時のみのスケジューラ。回帰テスト `tests/test_trail_effect.cpp`)。
- MVP `04_CLICK_BUBBLE`: クローズ — ユーザー確認済みのクリックバブル (シアンのリング、ease-out
  8→26 px / 250 ms / 0.85、上限 128 のバブル寿命、アクティブ時のみの共有
  スケジューラ。`tests/test_click_bubble_effect.cpp` — 107 チェック)。
- MVP `05_SETTINGS_GUI`: クローズ — ユーザー確認済みのライブ設定 GUI (General/Trail/Click、
  Golden Default テーマ、QColorDialog による対話的なカラースウォッチ、2px ベベル、
  スライダーと数値入力の同期、アクティブ時のみのスケジューラ。`tests/test_settings_window.cpp` — 35 チェック)。
- MVP `06_CONFIGURATION_AND_PERSISTENCE`: クローズ — `%LOCALAPPDATA%\ProTrail\config.json` への
  永続 JSON 設定、ReplaceFileW による原子的書き込み、破損時の `.corrupt` バックアップ付き
  安全なフォールバック、スレッドセーフな公開 (`tests/test_config.cpp` — 7 テスト)。
- MVP `07_RENDER_SCHEDULER_AND_PERFORMANCE`: クローズ — アクティブ時のみ動作する正式な `RenderScheduler`、
  ディスプレイ更新レートに同期 (EnumDisplaySettings)、安定した delta-time、アイドル時 0 回の起床、
  パフォーマンスカウンターとリークなしのリソース測定 (`tests/test_render_scheduler.cpp` — 10 テスト)。
- MVP `08_MULTI_MONITOR_AND_DPI`: クローズ — モニターごとのオーバーレイ構成、明示的な
  `PER_MONITOR_AWARE_V2` DPI 対応、物理ピクセル変換、遅延トポロジー調整、
  マルチモニター回帰スイート (`tests/test_multimonitor.cpp` — 28 テスト)。
- MVP `09_TRAY`: クローズ — システムトレイアイコン、閉じると隠れる SettingsWindow のライフサイクル、トレイ
  コンテキストメニュー (Settings、Enable/Disable、Exit)、孤児プロセスを残さないクリーンな終了
  (`tests/test_tray.cpp` — 12 テスト)。

### コア MVP の状態

コア MVP (マイルストーン 00〜09) は完了し、完全にクローズ済みです。
すべての機能要件と受け入れ基準を、両方の自動テストスイート (/W4 /WX クリーン、CTest 12/12 PASS) と、ユーザーによるマルチモニターおよびトレイの実機テストで検証しました。

### MVP 後の拡張

- Post-MVP `V1_COLORS_AND_PRESETS`: クローズ — トレイルの2色 (Start/Fade)、14 スウォッチのパレット、独自 `QColorDialog`、4 つのトレイル色モード (Full、Start only、Fade only、Gradient)、クリック用 14 スウォッチパレット、6 つのクイックプリセット (Classic、Fire、Ice、Neon、Toxic、Violet)、後方互換のスキーマ v2 移行。
- Post-MVP `V2_TRAIL_EFFECTS`: クローズ — 8 つのトレイルスタイル (Classic、Soft Glow、Comet、Neon、Dotted、Pulse、Ribbon、Spark)、グロー強度とセグメント間隔のパラメータ、スタイル対応のエフェクト演算とレンダラーのストロークポリシー、スキーマ v3。
- Post-MVP `V3_CLICK_EFFECTS`: クローズ — 7 つのクリックスタイル (Ring、Double Ring、Ripple、Burst、Spark Burst、Soft Flash、Dot + Ring)、パーティクル数 (0..24)、スキーマ v4、本番状態を変更しない分離スモークテスト。
- Post-MVP `V4_SYSTEM_STABILITY`: クローズ — 単一インスタンスのミューテックス (`Local\ProTrail_SingleInstance_Mutex`) と `ProTrail_ActivateInstance` によるウィンドウ起動、Direct2D/DXGI デバイス消失の処理と復旧 (`D2DERR_RECREATE_TARGET`、`DXGI_ERROR_DEVICE_REMOVED`、`DXGI_ERROR_DEVICE_RESET`) で HWND を保持、`Restore All Defaults` による即時復旧。

<!-- source-digest: README.md sha256:9edadbe96fce3ad2 -->
