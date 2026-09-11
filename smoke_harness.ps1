param(
    [string]$DeployDir = "artifacts\ProTrail-T017-Test-x64"
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

# Clean slate: kill any pre-existing orphans from previous runs (allowed before test run)
$preExisting = Get-Process -Name protrail -ErrorAction SilentlyContinue
if ($preExisting) {
    Stop-Process -Name protrail -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 1000
}
$survived = Get-Process -Name protrail -ErrorAction SilentlyContinue
if ($survived) {
    [Console]::Error.WriteLine("SMOKE_FAIL_PREEXISTING_ORPHAN: protrail.exe could not be killed before smoke test")
    exit 1
}

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
$env:PROTRAIL_SMOKE_AUTO_EXIT_MS = "3000"
$env:PROTRAIL_SMOKE_STATE_DIR = $smokeStateDir

# Launch deployed executable via Start-Process -PassThru
$proc = Start-Process -FilePath $exe -WorkingDirectory $fullDeployDir -PassThru

# Wait 1.5 seconds to observe initial liveness
Start-Sleep -Milliseconds 1500

if ($proc.HasExited) {
    Fail-Smoke "SMOKE_FAIL_NOT_RUNNING: protrail.exe did not launch or crashed during startup (exit code: $($proc.ExitCode))"
}
Write-Output "SMOKE_RUNNING_OK"

# Wait for natural application termination (auto-exit set to 3000ms, total timeout 7000ms)
$naturalExit = $proc.WaitForExit(7000)

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

# Verify zero protrail.exe processes remain (no orphans)
$remaining = Get-Process -Name protrail -ErrorAction SilentlyContinue
if ($remaining) {
    Stop-Process -Name protrail -Force -ErrorAction SilentlyContinue
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

Write-Output "SMOKE_NATURAL_SHUTDOWN_PASS"
Write-Output "SMOKE_CLEAN_EXIT"
exit 0
