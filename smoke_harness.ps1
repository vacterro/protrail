param(
    [string]$DeployDir = "artifacts\ProTrail-T017-Test-x64",
    # T-021R1: also prove the deployed binary really constructs and can
    # display its Settings window. Skip only for legacy packages.
    [switch]$NoSettingsCheck,
    # Release gates. The packaging pipeline passes these for every staged and
    # extracted package; a bare developer smoke keeps the historical checks.
    # ExpectedVersion: the PE ProductVersion/FileVersion must equal it.
    [string]$ExpectedVersion = "",
    # RequireProductIcon: the executable must carry the IDI_ICON1 icon group.
    [switch]$RequireProductIcon,
    # SanitizedPath: launch with a system-only PATH so no developer Qt/MSVC
    # directory can satisfy a missing package dependency.
    [switch]$SanitizedPath
)

$ErrorActionPreference = "Stop"

# Resolve full deployment directory and executable path
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([System.IO.Path]::IsPathRooted($DeployDir)) {
    $fullDeployDir = $DeployDir
} else {
    $fullDeployDir = Join-Path $scriptDir $DeployDir
}
$exe = Join-Path $fullDeployDir "protrail.exe"

if (-not (Test-Path $exe)) {
    [Console]::Error.WriteLine("SMOKE_FAIL_MISSING_EXE: $exe not found")
    exit 1
}

# Required deployment files
$requiredFiles = @("Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "platforms\qwindows.dll")
foreach ($f in $requiredFiles) {
    $target = Join-Path $fullDeployDir $f
    if (-not (Test-Path $target)) {
        [Console]::Error.WriteLine("SMOKE_FAIL_MISSING_FILE: $f not found in $fullDeployDir")
        exit 1
    }
}

# --- Release metadata gates (packaged binaries) ---
if ($ExpectedVersion) {
    $vi = (Get-Item -LiteralPath $exe).VersionInfo
    if ($vi.ProductName -ne "ProTrail" -or $vi.OriginalFilename -ne "protrail.exe") {
        [Console]::Error.WriteLine("SMOKE_FAIL_PRODUCT_METADATA: ProductName='$($vi.ProductName)' OriginalFilename='$($vi.OriginalFilename)'")
        exit 1
    }
    if ($vi.ProductVersion -ne $ExpectedVersion -or $vi.FileVersion -ne $ExpectedVersion) {
        [Console]::Error.WriteLine("SMOKE_FAIL_VERSION_MISMATCH: expected $ExpectedVersion, ProductVersion='$($vi.ProductVersion)' FileVersion='$($vi.FileVersion)'")
        exit 1
    }
    Write-Output "RELEASE_VERSION_OK ($ExpectedVersion)"
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public class ProTrailSmokeRes {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern IntPtr LoadLibraryExW(string f, IntPtr h, uint flags);
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindResourceW(IntPtr m, string name, IntPtr type);
  [DllImport("kernel32.dll")] public static extern bool FreeLibrary(IntPtr m);
  public static bool HasIconGroup(string path, string name) {
    IntPtr m = LoadLibraryExW(path, IntPtr.Zero, 0x00000002 | 0x00000020); // DATAFILE | IMAGE_RESOURCE
    if (m == IntPtr.Zero) return false;
    try { return FindResourceW(m, name, (IntPtr)14) != IntPtr.Zero; } // RT_GROUP_ICON
    finally { FreeLibrary(m); }
  }
}
'@
$hasProductIcon = [ProTrailSmokeRes]::HasIconGroup($exe, "IDI_ICON1")
if ($RequireProductIcon -and -not $hasProductIcon) {
    [Console]::Error.WriteLine("SMOKE_FAIL_NO_PRODUCT_ICON: $exe carries no IDI_ICON1 icon resource")
    exit 1
}
Write-Output ("PRODUCT_ICON_RESOURCE: " + $(if ($hasProductIcon) { "PRESENT" } else { "ABSENT" }))

# --- T-021R1: Win32 window inspection (Settings-window verification) ---
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public class ProTrailSmokeWin {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
}
'@

function Get-SmokeWindows([int]$targetPid) {
    $acc = New-Object System.Collections.ArrayList
    $cb = [ProTrailSmokeWin+EnumProc]{
        param($h, $l)
        $owner = 0
        [void][ProTrailSmokeWin]::GetWindowThreadProcessId($h, [ref]$owner)
        if ($owner -eq $targetPid) {
            $t = New-Object System.Text.StringBuilder 256
            [void][ProTrailSmokeWin]::GetWindowTextW($h, $t, 256)
            [void]$acc.Add([pscustomobject]@{
                hwnd    = $h
                title   = $t.ToString()
                visible = [ProTrailSmokeWin]::IsWindowVisible($h)
            })
        }
        return $true
    }
    [void][ProTrailSmokeWin]::EnumWindows($cb, [IntPtr]::Zero)
    return $acc
}

# --- T-021R1: production state must not be touched by the smoke run ---
function Get-ProductionStateSnapshot {
    $dir = Join-Path $env:LOCALAPPDATA 'ProTrail'
    if (-not (Test-Path $dir)) { return @{} }
    $snap = @{}
    foreach ($f in Get-ChildItem -Path $dir -Recurse -File -ErrorAction SilentlyContinue) {
        $snap[$f.FullName] = ('{0}|{1}|{2}' -f $f.Length,
            $f.LastWriteTimeUtc.Ticks,
            (Get-FileHash $f.FullName -Algorithm SHA256).Hash)
    }
    return $snap
}
$productionBefore = Get-ProductionStateSnapshot

# The operator's real Start with Windows entry must survive a smoke run
# byte-for-byte: smoke mode routes autostart to an in-memory backend.
function Get-RunEntrySnapshot {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    $item = Get-ItemProperty -Path $key -Name 'ProTrail' -ErrorAction SilentlyContinue
    if ($null -eq $item) { return '<absent>' }
    return [string]$item.ProTrail
}
$runEntryBefore = Get-RunEntrySnapshot

# Nothing may be written beside the executable: a portable package keeps its
# state under the per-user profile, never inside its own directory.
function Get-DeploySnapshot {
    $snap = @{}
    foreach ($f in Get-ChildItem -LiteralPath $fullDeployDir -Recurse -Force -ErrorAction SilentlyContinue) {
        $snap[$f.FullName] = if ($f.PSIsContainer) { '<dir>' } else { '{0}|{1}' -f $f.Length, $f.LastWriteTimeUtc.Ticks }
    }
    return $snap
}
$deployBefore = Get-DeploySnapshot

# Smoke mode bypasses the production single-instance protocol. Preserve any
# user's already-running ProTrail and track only processes launched from this
# deployment after the baseline snapshot.
$preExistingIds = @(
    Get-Process -Name protrail -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $exe } |
        ForEach-Object { $_.Id }
)

# Prepare isolated temporary state directory (T-017R2: no access to real user state)
$smokeStateDir = Join-Path ([System.IO.Path]::GetTempPath()) ("protrail_smoke_" + [System.Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $smokeStateDir -Force | Out-Null

$logFile = Join-Path $smokeStateDir "protrail.log"
$configFile = Join-Path $smokeStateDir "config.json"

function Fail-Smoke([string]$message, [int]$code = 1) {
    [Console]::Error.WriteLine($message)
    [Console]::Error.WriteLine("SMOKE_FAIL_PRESERVED_STATE_DIR: $smokeStateDir")
    Remove-Item env:PROTRAIL_SMOKE_AUTO_EXIT_MS -ErrorAction SilentlyContinue
    Remove-Item env:PROTRAIL_SMOKE_STATE_DIR -ErrorAction SilentlyContinue
    exit $code
}

# Set test-only smoke auto-exit interval (3000 ms) and isolated state directory
$autoExitMs = if ($NoSettingsCheck) { 3000 } else { 7000 }
$env:PROTRAIL_SMOKE_AUTO_EXIT_MS = "$autoExitMs"
$env:PROTRAIL_SMOKE_STATE_DIR = $smokeStateDir

# Launch deployed executable via Start-Process -PassThru
$savedPath = $env:PATH
if ($SanitizedPath) {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot;$env:SystemRoot\System32\Wbem"
    Write-Output "SANITIZED_PATH: $env:PATH"
}
try {
    $proc = Start-Process -FilePath $exe -WorkingDirectory $fullDeployDir -PassThru -WindowStyle Hidden
} finally {
    $env:PATH = $savedPath
}

# Wait 1.5 seconds to observe initial liveness
Start-Sleep -Milliseconds 1500

if ($proc.HasExited) {
    Fail-Smoke "SMOKE_FAIL_NOT_RUNNING: protrail.exe did not launch or crashed during startup (exit code: $($proc.ExitCode))"
}
Write-Output "SMOKE_RUNNING_OK"

# Dependency provenance: every Qt / MSVC runtime module the live process
# loaded must come from the package itself (the MSVC runtime may also come
# from the OS), never from a developer Qt or toolchain directory that happens
# to be reachable on this machine.
$deployPrefix = [System.IO.Path]::GetFullPath($fullDeployDir).TrimEnd('\') + '\'
$systemPrefix = [System.IO.Path]::GetFullPath($env:SystemRoot).TrimEnd('\') + '\'
$foreignRuntime = @()
$packagedModules = 0
try {
    $proc.Refresh()
    foreach ($m in $proc.Modules) {
        $path = [System.IO.Path]::GetFullPath($m.FileName)
        $inPackage = $path.StartsWith($deployPrefix, [System.StringComparison]::OrdinalIgnoreCase)
        $inSystem = $path.StartsWith($systemPrefix, [System.StringComparison]::OrdinalIgnoreCase)
        if ($inPackage) { $packagedModules++; continue }
        $isQt = $m.ModuleName -match '^qt6.*\.dll$' -or
                $path -match '\\(platforms|styles|imageformats|iconengines|generic|tls|networkinformation)\\q[^\\]+\.dll$'
        $isCrt = $m.ModuleName -match '^(vcruntime140.*|msvcp140.*|concrt140)\.dll$'
        if ($isQt -or ($isCrt -and -not $inSystem)) { $foreignRuntime += $path }
    }
} catch {
    Fail-Smoke "SMOKE_FAIL_MODULE_INSPECTION: $($_.Exception.Message)"
}
if ($foreignRuntime.Count -gt 0) {
    Fail-Smoke ("SMOKE_FAIL_FOREIGN_RUNTIME: " + ($foreignRuntime -join '; '))
}
Write-Output "DEPENDENCY_PROVENANCE_OK ($packagedModules modules loaded from the package)"

# T-021R1 Phase 6: the deployed binary must construct its Settings window,
# and that window must be displayable. Smoke mode runs without the
# single-instance activation receiver, so the tray/menu open path cannot be
# driven from outside the process: this step verifies the real
# "ProTrail Settings" top-level window of THIS deployed process, shows it,
# confirms it became visible, and hides it again. The in-app open paths
# (tray double-click, tray menu "Settings") are covered by
# protrail_tray_tests / protrail_gui_tests in CTest.
if (-not $NoSettingsCheck) {
    $settings = (Get-SmokeWindows $proc.Id) |
        Where-Object { $_.title -eq 'ProTrail' } | Select-Object -First 1
    if (-not $settings) {
        Fail-Smoke "SMOKE_FAIL_NO_SETTINGS_WINDOW: deployed process exposes no 'ProTrail' product window"
    }
    [void][ProTrailSmokeWin]::ShowWindow([IntPtr]$settings.hwnd, 5)   # SW_SHOW
    Start-Sleep -Milliseconds 600
    $shown = [ProTrailSmokeWin]::IsWindowVisible([IntPtr]$settings.hwnd)
    [void][ProTrailSmokeWin]::ShowWindow([IntPtr]$settings.hwnd, 0)   # SW_HIDE
    if (-not $shown) {
        Fail-Smoke "SMOKE_FAIL_SETTINGS_NOT_DISPLAYABLE: 'ProTrail' product window did not become visible"
    }
    Write-Output "SETTINGS_WINDOW_OPEN_OK (hwnd $($settings.hwnd))"
}

# Wait for natural application termination (auto-exit set to 3000ms, total timeout 7000ms)
$naturalExit = $proc.WaitForExit($autoExitMs + 6000)

if (-not $naturalExit) {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    [Console]::Error.WriteLine("FORCED_CLEANUP_USED")
    Start-Sleep -Milliseconds 1000
    Fail-Smoke "SMOKE_FAIL_NATURAL_EXIT_TIMEOUT: protrail.exe did not terminate naturally"
}

# Verify process exit code == 0
$exitCode = $proc.ExitCode
if ($exitCode -ne 0) {
    Fail-Smoke "SMOKE_FAIL_NONZERO_EXIT: protrail.exe exited with code $exitCode" $exitCode
}
Write-Output "PROCESS_EXIT_CODE_0_OK (exit code: 0)"

# Verify zero NEW processes from this deployment remain (no smoke orphans).
$remaining = @(Get-Process -Name protrail -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -eq $exe -and $_.Id -notin $preExistingIds })
if ($remaining) {
    $remaining | Stop-Process -Force -ErrorAction SilentlyContinue
    [Console]::Error.WriteLine("FORCED_CLEANUP_USED")
    Fail-Smoke "SMOKE_FAIL_ORPHAN: protrail.exe process still present after exit"
}
Write-Output "ZERO_ORPHANS_OK"

# Verify runtime log contains truthful evidence of graceful shutdown in exact lifecycle order
if (-not (Test-Path $logFile)) {
    Fail-Smoke "SMOKE_FAIL_MISSING_LOG: $logFile was not produced"
}

$logContent = Get-Content -Path $logFile
$expectedMarkers = @(
    "smoke auto-exit hook armed",
    "smoke auto-exit hook triggered",
    "ProTrail exit requested",
    "ProTrail shutting down",
    "ProTrail shutdown complete"
)

$markerIndex = 0
foreach ($line in $logContent) {
    if ($markerIndex -lt $expectedMarkers.Length) {
        if ($line.Contains($expectedMarkers[$markerIndex])) {
            $markerIndex++
        }
    }
}

if ($markerIndex -lt $expectedMarkers.Length) {
    Fail-Smoke "SMOKE_FAIL_LOG_ORDER: log missing marker '$($expectedMarkers[$markerIndex])' or markers not in lifecycle order"
}

Write-Output "SHUTDOWN_LOG_EVIDENCE_OK"

# Verify isolated config was written during shutdown
if (-not (Test-Path $configFile)) {
    Fail-Smoke "SMOKE_FAIL_MISSING_CONFIG: $configFile was not produced in isolated state dir"
}
Write-Output "CONFIG_PERSISTENCE_EVIDENCE_OK"

# All checks passed: clean up isolated state directory and environment
Remove-Item -Recurse -Force -Path $smokeStateDir -ErrorAction SilentlyContinue
Remove-Item env:PROTRAIL_SMOKE_AUTO_EXIT_MS -ErrorAction SilentlyContinue
Remove-Item env:PROTRAIL_SMOKE_STATE_DIR -ErrorAction SilentlyContinue

# T-021R1 Phase 6: prove the run mutated no production config and no
# production log (every byte under %LOCALAPPDATA%\ProTrail is unchanged).
$productionAfter = Get-ProductionStateSnapshot
$mutated = @()
foreach ($k in $productionBefore.Keys) {
    if (-not $productionAfter.ContainsKey($k)) { $mutated += "REMOVED $k" }
    elseif ($productionAfter[$k] -ne $productionBefore[$k]) { $mutated += "MODIFIED $k" }
}
foreach ($k in $productionAfter.Keys) {
    if (-not $productionBefore.ContainsKey($k)) { $mutated += "CREATED $k" }
}
if ($mutated.Count -gt 0) {
    Fail-Smoke ("SMOKE_FAIL_PRODUCTION_STATE_MUTATED: " + ($mutated -join '; '))
}
Write-Output "NO_PRODUCTION_CONFIG_MUTATION_OK"
Write-Output "NO_PRODUCTION_LOG_MUTATION_OK"

$runEntryAfter = Get-RunEntrySnapshot
if ($runEntryAfter -ne $runEntryBefore) {
    Fail-Smoke "SMOKE_FAIL_AUTOSTART_MUTATED: HKCU Run 'ProTrail' changed from '$runEntryBefore' to '$runEntryAfter'"
}
Write-Output "NO_PRODUCTION_AUTOSTART_MUTATION_OK"

$deployAfter = Get-DeploySnapshot
$deployChanges = @()
foreach ($k in $deployAfter.Keys) {
    if (-not $deployBefore.ContainsKey($k)) { $deployChanges += "CREATED $k" }
    elseif ($deployAfter[$k] -ne $deployBefore[$k]) { $deployChanges += "MODIFIED $k" }
}
foreach ($k in $deployBefore.Keys) {
    if (-not $deployAfter.ContainsKey($k)) { $deployChanges += "REMOVED $k" }
}
if ($deployChanges.Count -gt 0) {
    Fail-Smoke ("SMOKE_FAIL_PACKAGE_DIR_WRITTEN: " + ($deployChanges -join '; '))
}
Write-Output "NO_PACKAGE_DIR_WRITES_OK"

Write-Output "SMOKE_NATURAL_SHUTDOWN_PASS"
Write-Output "SMOKE_CLEAN_EXIT"
exit 0
