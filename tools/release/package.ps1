<#
.SYNOPSIS
    Builds, verifies and packages the ProTrail Windows x64 portable release.

.DESCRIPTION
    One command from a clean checkout produces the staged package, the
    portable ZIP and SHA256SUMS.txt, and proves them:

      1. provenance   VERSION, clean tree, HEAD / existing tag agreement
      2. branding     approved icon + APPROVAL.md hash (official mode only)
      3. build        fresh Release configure/build, /W4 /WX, zero warnings
      4. tests        complete CTest suite, 100% pass
      5. stage        protrail.exe + windeployqt runtime (incl. the Qt
                      Multimedia module and its backend plugin) + app-local
                      MSVC CRT [+ sound catalog, opt-in via -IncludeSounds]
      6. closure      every PE dependency resolves inside the package or to
                      the OS; no developer path is embedded in protrail.exe
      7. smoke        staged package, sanitized PATH, release metadata gates
      8. path matrix  plain / spaces / nested / Unicode deployment paths
      9. archive      deterministic ZIP, SHA256SUMS.txt, hash re-verified
     10. extract      fresh extraction re-hashed and smoked independently

    Official mode refuses a dirty tree and a missing or unapproved icon.
    -Rehearsal runs the identical pipeline without those two gates; every
    rehearsal output carries a REHEARSAL name so it can never be mistaken
    for, or uploaded as, the official artifact.

    T-81 LOCAL-ONLY SOUND REHEARSAL. The shipped sound catalog is third-party
    material whose redistribution rights are not established, so no official
    package may ever carry it. For a local deployment where the operator has
    cleared the sound folder for their own use:

        powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\package.ps1 `
            -Rehearsal -IncludeSounds

    That produces the exact official pipeline (clean build, /W4 /WX, full
    CTest, windeployqt incl. the multimedia backend, dependency closure,
    sanitized-PATH smoke) with the user's sound directory staged beside the
    executable, and it verifies actual deployed playback through the
    AUDIO_SELFCHECK line the app writes in smoke mode. That line is
    DEVICE-REPORTED playback (a voice reached Playing); the pipeline never
    claims a human heard it -- only -HumanAudibleVerified records physical
    acceptance, and only after a person has genuinely heard it. The output is
    named REHEARSAL and must never be published.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\package.ps1
.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\package.ps1 -Rehearsal
#>
param(
    [switch]$Rehearsal,
    [string]$QtDir = "",
    [string]$OutDir = "",
    [string]$BuildDir = "",
    # T-80: stage the WAV catalog next to the executable. OFF by default on
    # purpose: the catalog is third-party material whose redistribution rights
    # are not established, so an artifact must never carry it implicitly.
    # Pass -IncludeSounds only for a LOCAL rehearsal where the operator has
    # cleared that folder for their own use; the official path stays silent.
    [switch]$IncludeSounds,
    # T-82: operator attestation. Pass ONLY after a person has genuinely heard
    # the deployed audio on real hardware. This is the sole way the manifest
    # ever records HUMAN_AUDIBLE_VERIFIED; without it every run -- however
    # green -- records HUMAN_AUDIBLE_NOT_VERIFIED.
    [switch]$HumanAudibleVerified
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 3

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $OutDir)   { $OutDir = Join-Path $repo "dist" }
if (-not $BuildDir) { $BuildDir = Join-Path $repo "build_release" }
# Every later path computation (archive entry names, extraction checks) needs
# absolute directories; a relative argument resolves against the caller's
# PowerShell location, never the process working directory.
function Resolve-Absolute([string]$path) {
    if (-not [System.IO.Path]::IsPathRooted($path)) { $path = Join-Path (Get-Location).Path $path }
    return [System.IO.Path]::GetFullPath($path)
}
$OutDir = Resolve-Absolute $OutDir
$BuildDir = Resolve-Absolute $BuildDir
$mode = if ($Rehearsal) { "REHEARSAL" } else { "OFFICIAL" }

function Step([string]$text) { Write-Output ""; Write-Output "==> $text" }
function Fail([string]$code, [string]$detail) {
    [Console]::Error.WriteLine("RELEASE_FAIL $code`: $detail")
    exit 1
}
function Sha256([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
# Windows PowerShell 5.1 turns redirected native stderr into terminating
# errors under ErrorActionPreference=Stop; native tools report through their
# exit code instead, which every caller checks explicitly.
function Invoke-Checked([string]$what, [scriptblock]$body) {
    $ErrorActionPreference = "Continue"
    & $body
    if ($LASTEXITCODE -ne 0) { Fail "COMMAND_FAILED" "$what (exit $LASTEXITCODE)" }
}
function Get-PeFiles([string]$dir) {
    @(Get-ChildItem -LiteralPath $dir -Recurse -File | Where-Object { $_.Extension -in @(".exe", ".dll") })
}

# ---------------------------------------------------------------- provenance
Step "Provenance ($mode)"
$version = (Get-Content -LiteralPath (Join-Path $repo "VERSION") -TotalCount 1).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { Fail "VERSION_INVALID" "VERSION holds '$version'" }
$tag = "v$version"

Push-Location $repo
try {
    $head = (git rev-parse HEAD).Trim()
    $dirty = @(git status --porcelain --untracked-files=normal)
    $commitTime = (git log -1 --format=%cI HEAD).Trim()
    $tagCommit = (git rev-parse -q --verify "refs/tags/$tag^{commit}" 2>$null)
} finally { Pop-Location }
if ($tagCommit) { $tagCommit = $tagCommit.Trim() }

Write-Output "version     $version"
Write-Output "commit      $head"
Write-Output "dirty paths $($dirty.Count)"
if ($dirty.Count -gt 0 -and -not $Rehearsal) {
    Fail "DIRTY_TREE" "official packages are built only from a clean committed tree:`n$($dirty -join "`n")"
}
if ($tagCommit -and $tagCommit -ne $head) {
    Fail "TAG_MISMATCH" "$tag already exists at $tagCommit but HEAD is $head; tags are immutable, choose the next version"
}

# ------------------------------------------------------------------ branding
Step "Branding gate"
$iconFile = Join-Path $repo "resources\branding\protrail.ico"
$approvalFile = Join-Path $repo "resources\branding\APPROVAL.md"
$iconStatus = "ABSENT"
if (Test-Path -LiteralPath $iconFile) {
    $iconHash = Sha256 $iconFile
    $approved = (Test-Path -LiteralPath $approvalFile) -and
                ((Get-Content -LiteralPath $approvalFile -Raw) -match "sha256:\s*$iconHash")
    $iconStatus = if ($approved) { "APPROVED" } else { "UNAPPROVED" }
}
Write-Output "product icon $iconStatus"
if ($iconStatus -ne "APPROVED" -and -not $Rehearsal) {
    Fail "USER_PRODUCT_ICON_PENDING" "official release needs resources\branding\protrail.ico and an APPROVAL.md recording its sha256 (see RELEASE_BLOCKERS.md)"
}

# ----------------------------------------------------------------- toolchain
if (-not $QtDir) {
    $QtDir = if ($env:Qt6_DIR) { (Resolve-Path (Join-Path $env:Qt6_DIR "..\..\..")).Path } else { "C:\Qt\6.8.0\msvc2022_64" }
}
$windeployqt = Join-Path $QtDir "bin\windeployqt.exe"
if (-not (Test-Path -LiteralPath $windeployqt)) { Fail "QT_NOT_FOUND" "$windeployqt" }
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $vsRoot) { Fail "MSVC_NOT_FOUND" "vswhere found no VC x64 toolset" }
$msvcVer = (Get-ChildItem (Join-Path $vsRoot "VC\Tools\MSVC") | Sort-Object Name -Descending | Select-Object -First 1).Name
$dumpbin = Join-Path $vsRoot "VC\Tools\MSVC\$msvcVer\bin\Hostx64\x64\dumpbin.exe"
$crtDir = Get-ChildItem (Join-Path $vsRoot "VC\Redist\MSVC") -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName "x64\Microsoft.VC143.CRT") } |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $crtDir) { Fail "CRT_NOT_FOUND" "no VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT under $vsRoot" }
$crtDir = Join-Path $crtDir.FullName "x64\Microsoft.VC143.CRT"
Write-Output "qt          $QtDir"
Write-Output "msvc        $vsRoot ($msvcVer)"

# --------------------------------------------------------------------- build
Step "Clean Release build"
if (Test-Path -LiteralPath $BuildDir) { Remove-Item -LiteralPath $BuildDir -Recurse -Force }
New-Item -ItemType Directory -Path $BuildDir | Out-Null
$requireIcon = if ($Rehearsal) { "OFF" } else { "ON" }
$buildLog = Join-Path $BuildDir "build-release.log"
# Pin every embedded timestamp (Qt rcc resource dates) to the commit time so
# the archive depends only on the committed sources.
$env:SOURCE_DATE_EPOCH = [string][DateTimeOffset]::Parse($commitTime).ToUnixTimeSeconds()
Invoke-Checked "configure" {
    cmake -S $repo -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
        "-DCMAKE_PREFIX_PATH=$QtDir" "-DPROTRAIL_REQUIRE_FINAL_ICON=$requireIcon" 2>&1 |
        Tee-Object -FilePath (Join-Path $BuildDir "configure.log")
}
Invoke-Checked "build" {
    cmake --build $BuildDir --config Release --parallel 2>&1 | Tee-Object -FilePath $buildLog | Out-Null
}
$warnings = @(Select-String -LiteralPath $buildLog -Pattern '\b(warning|error)\s+[A-Z]+\d+' |
    Where-Object { $_.Line -notmatch 'MSB8029' })
if ($warnings.Count -gt 0) {
    Fail "BUILD_WARNINGS" "$($warnings.Count) compiler/linker diagnostics:`n$(($warnings | Select-Object -First 20 | ForEach-Object Line) -join "`n")"
}
Write-Output "build       0 errors, 0 warnings (/W4 /WX)"

# --------------------------------------------------------------------- tests
Step "Complete CTest suite (Release)"
$ctestLog = Join-Path $BuildDir "ctest-release.log"
$ErrorActionPreference = "Continue"
& ctest --test-dir $BuildDir -C Release --output-on-failure 2>&1 | Tee-Object -FilePath $ctestLog | Select-Object -Last 4
$ctestRc = $LASTEXITCODE
$ErrorActionPreference = "Stop"
if ($ctestRc -ne 0) { Fail "TESTS_FAILED" "see $ctestLog" }
# "100% tests passed out of N" on success; "..., K tests failed out of N" otherwise.
$summary = @(Select-String -LiteralPath $ctestLog -Pattern '(\d+)% tests passed(?:, \d+ tests failed)? out of (\d+)')
$testTotal = 0
if ($summary.Count -gt 0) {
    if ($summary[-1].Matches[0].Groups[1].Value -ne "100") { Fail "TESTS_FAILED" "see $ctestLog" }
    $testTotal = [int]$summary[-1].Matches[0].Groups[2].Value
}
if ($testTotal -lt 1) { Fail "TESTS_UNCOUNTED" "no CTest summary in $ctestLog" }

# --------------------------------------------------------- audio evidence
# T-82: a green CTest summary alone cannot say what the audio suite actually
# proved -- a skipped catalog check inside a passing suite must never be
# readable as a successful catalog verification. Both audio binaries run
# directly here so their EVIDENCE lines and exit codes are captured verbatim
# and a missing evidence line fails the release instead of passing silently.
Step "Audio backend evidence"
$audioEvidence = [ordered]@{}
$audioStatuses = @()
foreach ($case in @(
    @{ name = "protrail_audio_tests";        allow = @(0);    required = @(
            "staged_wav_count", "distinct_preview_source_count",
            "reparse_symlink_guard", "shipped_catalog_decode") },
    @{ name = "protrail_audio_runtime_tests"; allow = @(0, 2); required = @(
            "staged_wav_count", "shipped_format_sample_count",
            "distinct_preview_test_count", "voice_pool_capacity",
            "backend_test_exit_code", "audio_device_available",
            "natural_completion") }
)) {
    $exe = Join-Path $BuildDir "Release\$($case.name).exe"
    if (-not (Test-Path -LiteralPath $exe)) { Fail "AUDIO_TEST_MISSING" "$exe not built" }
    $log = Join-Path $BuildDir "$($case.name)-evidence.log"
    $ErrorActionPreference = "Continue"
    # CTest normally prepends the Qt bin directory for these executables; a
    # direct run needs the same PATH or the Qt DLLs cannot load.
    $env:PATH = (Join-Path $QtDir "bin") + ";" + $env:PATH
    Push-Location $repo
    try { & $exe 2>&1 | Tee-Object -FilePath $log | Out-Null } finally { Pop-Location }
    $rc = $LASTEXITCODE
    $ErrorActionPreference = "Stop"
    # Exit 2 of the runtime suite means BACKEND_VERIFIED without an output
    # device: acceptable for a portable package, but only with the evidence
    # lines saying so. Anything else is a test failure.
    if ($case.allow -notcontains $rc) {
        Fail "AUDIO_TEST_FAILED" "$($case.name) exit $rc (see $log)"
    }
    $perCase = [ordered]@{ "exit_code" = "$rc" }
    foreach ($m in (Select-String -LiteralPath $log -Pattern '^EVIDENCE (\S+): (.+)$')) {
        $perCase[$m.Matches[0].Groups[1].Value] = $m.Matches[0].Groups[2].Value.Trim()
    }
    foreach ($m in (Select-String -LiteralPath $log -Pattern '^STATUS (.+)$')) {
        $audioStatuses += $m.Matches[0].Groups[1].Value.Trim()
    }
    foreach ($requiredKey in $case.required) {
        if (-not $perCase.Contains($requiredKey)) {
            Fail "AUDIO_EVIDENCE_MISSING" "$($case.name) printed no EVIDENCE $requiredKey line"
        }
    }
    $audioEvidence[$case.name] = $perCase
    foreach ($kv in $perCase.GetEnumerator()) {
        Write-Output ("evidence    {0}.{1} = {2}" -f $case.name, $kv.Key, $kv.Value)
    }
}
# When the sound material is being STAGED, its checks must have RUN. A SKIP or
# PARTIAL here is an unverified guard, never a verified one.
#
# T-95: the two lines that happened to be enumerated here were value-checked
# while every other REQUIRED sound line -- notably reparse_symlink_guard -- was
# only checked for EXISTENCE, so an escape guard that reported "SKIP
# (create_symlink failed; needs elevation or Developer Mode) -- NOT VERIFIED"
# was recorded into verification.audio and the release still exited 0 with
# RELEASE_PACKAGE_PASS. The value gate now enumerates every sound line whose
# verdict can be non-verifying, so a newly required line cannot silently skip
# the same way, and it accepts the guard's PARTIAL variant as the
# non-verification it is (test_sound_engine.cpp:868).
if ($IncludeSounds) {
    $soundVerdictGates = @(
        @{ case = "protrail_audio_tests";         key = "shipped_catalog_decode"     },
        @{ case = "protrail_audio_tests";         key = "reparse_symlink_guard"      },
        @{ case = "protrail_audio_runtime_tests"; key = "shipped_format_sample_count" }
    )
    $unverifiedSounds = @()
    foreach ($gate in $soundVerdictGates) {
        $verdict = [string]$audioEvidence[$gate.case][$gate.key]
        if ($verdict -like "SKIP*" -or $verdict -like "PARTIAL*" -or $verdict -like "*NOT VERIFIED*") {
            $unverifiedSounds += ("{0}.{1} = {2}" -f $gate.case, $gate.key, $verdict)
        }
    }
    if ($unverifiedSounds.Count -gt 0) {
        Fail "SOUND_CATALOG_UNVERIFIED" "-IncludeSounds requires the shipped-catalog and escape-guard checks to RUN, not SKIP: $($unverifiedSounds -join '; ')"
    }
}
$humanAudibleStatus = if ($HumanAudibleVerified) {
    "HUMAN_AUDIBLE_VERIFIED (operator attested after physical acceptance)"
} else {
    "HUMAN_AUDIBLE_NOT_VERIFIED"
}
Write-Output ("statuses    " + ($audioStatuses -join "; "))

# --------------------------------------------------------------------- stage
$packageName = "ProTrail-v$version-win-x64-portable"
if ($Rehearsal) {
    $OutDir = Join-Path $OutDir "rehearsal"
    $packageName = "$packageName-REHEARSAL"
}
$stage = Join-Path $OutDir $packageName
$zipPath = Join-Path $OutDir "$packageName.zip"
$sumsPath = Join-Path $OutDir "SHA256SUMS.txt"
Step "Stage $packageName"
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
foreach ($p in @($stage, $zipPath, $sumsPath)) {
    if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Recurse -Force }
}
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDir "Release\protrail.exe") -Destination $stage
Invoke-Checked "windeployqt" {
    # T-80: the audio engine links Qt6::Multimedia, so windeployqt must deploy
    # the multimedia module AND its backend plugin. The old --no-ffmpeg flag is
    # gone on purpose: the ffmpeg backend is what decodes the WAV catalog, and
    # Qt6Multimedia.dll alone cannot play anything.
    & $windeployqt --release --no-translations --no-system-d3d-compiler --no-system-dxc-compiler `
        --no-opengl-sw --no-compiler-runtime --no-quick-import `
        --skip-plugin-types "iconengines,imageformats,generic,networkinformation,tls,platforminputcontexts" `
        (Join-Path $stage "protrail.exe") 2>&1 | Tee-Object -FilePath (Join-Path $BuildDir "windeployqt.log") | Out-Null
}

# T-80: prove the multimedia RUNTIME is actually deployable, not just linked.
if (-not (Test-Path -LiteralPath (Join-Path $stage "Qt6Multimedia.dll"))) {
    Fail "MULTIMEDIA_INCOMPLETE" "Qt6Multimedia.dll was not deployed"
}
# windeployqt puts the backend plugin in <appDir>/multimedia (the layout this
# Qt uses for platforms/ and styles/ too), so accept that and the nested
# plugins/multimedia form.
$mmPlugins = @(Get-ChildItem -LiteralPath (Join-Path $stage "multimedia") -Filter *.dll -File -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Name.ToLowerInvariant() }) +
    @(Get-ChildItem -LiteralPath (Join-Path $stage "plugins\multimedia") -Filter *.dll -File -ErrorAction SilentlyContinue |
    ForEach-Object { $_.Name.ToLowerInvariant() })
if ($mmPlugins.Count -lt 1) {
    Fail "MULTIMEDIA_BACKEND_MISSING" "no multimedia backend dll staged; Qt6Multimedia.dll alone cannot decode the WAV catalog"
}

# T-81: the sound catalog is a sibling of the executable, never a
# working-directory relative path, so a portable copy keeps working from any
# launch location. Opt-in only (see the param comment above).
$soundCount = 0
# A skip and a verification are different facts; the manifest records which
# one actually happened.
$soundCatalogStatus = "NOT_STAGED (opt-in omitted) -- package sound catalog NOT VERIFIED"
if ($IncludeSounds) {
    $soundSrc = Join-Path $repo "sound"
    if (-not (Test-Path -LiteralPath $soundSrc)) { Fail "SOUND_DIR_MISSING" "$soundSrc" }
    Copy-Item -LiteralPath $soundSrc -Destination $stage -Recurse -Force
    $soundCount = @(Get-ChildItem -LiteralPath (Join-Path $stage "sound") -Filter *.wav -File).Count
    if ($soundCount -lt 1) { Fail "SOUND_DIR_EMPTY" "no .wav staged into $stage\sound" }
    $srcWav = @(Get-ChildItem -LiteralPath $soundSrc -Filter *.wav -File).Count
    if ($soundCount -ne $srcWav) {
        Fail "SOUND_CATALOG_INCOMPLETE" "staged $soundCount of $srcWav source WAV files"
    }
    $soundCatalogStatus = "STAGED_VERIFIED ($soundCount assets, matches the source catalog)"
    foreach ($required in @("Qt6Multimedia.dll")) {
        if (-not (Test-Path -LiteralPath (Join-Path $stage $required))) {
            Fail "MULTIMEDIA_INCOMPLETE" "-IncludeSounds needs $required (audio engine links Qt6::Multimedia)"
        }
    }
}

function Get-Dependents([string]$pe) {
    $ErrorActionPreference = "Continue"
    $out = & $dumpbin /nologo /dependents $pe
    if ($LASTEXITCODE -ne 0) { Fail "DUMPBIN_FAILED" $pe }
    return @($out | Where-Object { $_ -match '^\s+(\S+\.dll)\s*$' } | ForEach-Object { $Matches[1] })
}

# App-local MSVC runtime: only the CRT DLLs the package actually imports.
$crtNames = @(Get-ChildItem -LiteralPath $crtDir -Filter *.dll | ForEach-Object Name)
for ($pass = 0; $pass -lt 3; $pass++) {
    $needed = @(Get-PeFiles $stage |
        ForEach-Object { Get-Dependents $_.FullName } | Sort-Object -Unique |
        Where-Object { $crtNames -contains $_ -and -not (Test-Path (Join-Path $stage $_)) })
    if ($needed.Count -eq 0) { break }
    foreach ($n in $needed) { Copy-Item -LiteralPath (Join-Path $crtDir $n) -Destination $stage }
}

# ------------------------------------------------------------------- closure
Step "Dependency closure and leak scan"
$stageRootNames = @(Get-ChildItem -LiteralPath $stage -File | ForEach-Object { $_.Name.ToLowerInvariant() })
$system32 = Join-Path $env:SystemRoot "System32"
$unresolved = @()
$pes = Get-PeFiles $stage
foreach ($pe in $pes) {
    foreach ($dep in (Get-Dependents $pe.FullName)) {
        $lower = $dep.ToLowerInvariant()
        if ($stageRootNames -contains $lower) { continue }
        if ($lower -match '^(api|ext)-ms-') { continue }
        $isBundled = $lower -match '^(qt6|vcruntime|msvcp|concrt)'
        if (-not $isBundled -and (Test-Path -LiteralPath (Join-Path $system32 $dep))) { continue }
        $unresolved += "$($pe.Name) -> $dep"
    }
}
if ($unresolved.Count -gt 0) { Fail "DEPENDENCY_UNRESOLVED" ($unresolved -join "; ") }
foreach ($required in @("protrail.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "Qt6Multimedia.dll", "platforms\\qwindows.dll")) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $required))) { Fail "PACKAGE_INCOMPLETE" "$required missing" }
}
$forbidden = @(Get-ChildItem -LiteralPath $stage -Recurse -File |
    Where-Object { $_.Extension -in @(".pdb", ".obj", ".ilk", ".lib", ".exp", ".log", ".json") -or $_.Name -like "*_tests.exe" })
if ($forbidden.Count -gt 0) { Fail "PACKAGE_POLLUTED" (($forbidden | ForEach-Object FullName) -join "; ") }

$exeBytes = [System.IO.File]::ReadAllBytes((Join-Path $stage "protrail.exe"))
$latin = [System.Text.Encoding]::GetEncoding(28591).GetString($exeBytes)
$utf16 = [System.Text.Encoding]::Unicode.GetString($exeBytes)
foreach ($needle in @($repo, $repo.Replace('\', '/'), $BuildDir, $BuildDir.Replace('\', '/'))) {
    foreach ($hay in @($latin, $utf16)) {
        if ($hay.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
            Fail "DEVELOPER_PATH_LEAK" "protrail.exe embeds '$needle'"
        }
    }
}
$packageFiles = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName)
Write-Output "closure     $($pes.Count) PE files, all imports resolved; multimedia backends: $($mmPlugins -join ', '); sound catalog: $soundCatalogStatus; package assets: $($packageFiles.Count); no developer path in protrail.exe"

# ---------------------------------------------------------------------- smoke
$harness = Join-Path $repo "smoke_harness.ps1"
function Invoke-Smoke([string]$dir, [string]$label, [string]$WorkingDirectory = "") {
    $ErrorActionPreference = "Continue"
    $smokeArgs = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $harness,
                   "-DeployDir", $dir, "-ExpectedVersion", $version, "-SanitizedPath")
    if (-not $Rehearsal) { $smokeArgs += "-RequireProductIcon" }
    # A staged catalog must be heard through the deployed backend, never
    # accepted on file existence alone.
    if ($IncludeSounds) { $smokeArgs += "-RequireSounds" }
    if ($WorkingDirectory) { $smokeArgs += "-WorkingDirectory", $WorkingDirectory }
    $output = & powershell.exe @smokeArgs 2>&1
    $rc = $LASTEXITCODE
    $output | ForEach-Object { Write-Output "    [$label] $_" }
    if ($rc -ne 0) { Fail "SMOKE_FAILED" "$label ($dir), exit $rc" }
}
Step "Smoke: staged package"
Invoke-Smoke $stage "staged"

# T-81: the catalog is executable-relative, so the same staged package must
# also play from an unrelated working directory. Gated on -IncludeSounds
# because that is the only working-directory-sensitive asset.
if ($IncludeSounds) {
    Step "Smoke: unrelated working directory"
    Invoke-Smoke $stage "cwd:temp" -WorkingDirectory ([System.IO.Path]::GetTempPath())
}

Step "Portable path matrix"
$matrixRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("protrail_paths_" + [guid]::NewGuid().ToString("N"))
$unicodeLeaf = "ProTrail " + [char]0x00FC + [char]0x00F1 + [char]0x00EF + " " + [char]0x6E2C + [char]0x8A66
$cases = [ordered]@{
    "plain"   = Join-Path $matrixRoot "plain\ProTrail"
    "spaces"  = Join-Path $matrixRoot "with spaces\Pro Trail Portable"
    "nested"  = Join-Path $matrixRoot "nested\a\b\c\d\ProTrail"
    "unicode" = Join-Path $matrixRoot ("unicode\" + $unicodeLeaf)
}
foreach ($case in $cases.GetEnumerator()) {
    New-Item -ItemType Directory -Path $case.Value -Force | Out-Null
    Copy-Item -Path (Join-Path $stage "*") -Destination $case.Value -Recurse
    Invoke-Smoke $case.Value "path:$($case.Key)"
}
Remove-Item -LiteralPath $matrixRoot -Recurse -Force

# -------------------------------------------------------------------- archive
Step "Deterministic ZIP"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$stamp = [DateTimeOffset]::Parse($commitTime)
$stagePrefix = $stage.TrimEnd('\') + '\'
$fs = [System.IO.File]::Open($zipPath, [System.IO.FileMode]::CreateNew)
try {
    $zip = New-Object System.IO.Compression.ZipArchive($fs, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        # Ordinal entry order + one fixed timestamp (the commit time): the same
        # staged bytes always produce the same archive bytes.
        $byName = @{}
        foreach ($f in $packageFiles) {
            $byName["$packageName/" + $f.FullName.Substring($stagePrefix.Length).Replace('\', '/')] = $f.FullName
        }
        [string[]]$names = @($byName.Keys)
        [Array]::Sort($names, [StringComparer]::Ordinal)
        foreach ($name in $names) {
            $entry = $zip.CreateEntry($name, [System.IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $stamp
            $dst = $entry.Open()
            try {
                $src = [System.IO.File]::OpenRead($byName[$name])
                try { $src.CopyTo($dst) } finally { $src.Dispose() }
            } finally { $dst.Dispose() }
        }
    } finally { $zip.Dispose() }
} finally { $fs.Dispose() }

$zipHash = Sha256 $zipPath
$zipName = Split-Path -Leaf $zipPath
[System.IO.File]::WriteAllText($sumsPath, "$zipHash  $zipName`n", (New-Object System.Text.UTF8Encoding($false)))
$verify = (Get-Content -LiteralPath $sumsPath -TotalCount 1) -split '\s+'
if ($verify[0] -ne (Sha256 $zipPath) -or $verify[1] -ne $zipName) { Fail "CHECKSUM_MISMATCH" "$sumsPath does not verify" }
Write-Output "zip         $zipName"
Write-Output "sha256      $zipHash"

# -------------------------------------------------------------------- extract
Step "Independent extraction and smoke"
$extractRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("protrail extract " + [guid]::NewGuid().ToString("N"))
[System.IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $extractRoot)
$extracted = Join-Path $extractRoot $packageName
foreach ($f in $packageFiles) {
    $rel = $f.FullName.Substring($stagePrefix.Length)
    $copy = Join-Path $extracted $rel
    if (-not (Test-Path -LiteralPath $copy) -or (Sha256 $copy) -ne (Sha256 $f.FullName)) {
        Fail "EXTRACT_MISMATCH" "$rel differs after extraction"
    }
}
$extraFiles = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File).Count - $packageFiles.Count
if ($extraFiles -ne 0) { Fail "EXTRACT_MISMATCH" "archive holds $extraFiles unexpected file(s)" }
Invoke-Smoke $extracted "extracted"
Remove-Item -LiteralPath $extractRoot -Recurse -Force

# ------------------------------------------------------------------- manifest
$qtVersion = (Get-Item -LiteralPath (Join-Path $stage "Qt6Core.dll")).VersionInfo.ProductVersion
$manifest = [ordered]@{
    product        = "ProTrail"
    version        = $version
    mode           = $mode
    commit         = $head
    commit_time    = $commitTime
    tree_dirty     = ($dirty.Count -gt 0)
    tag            = $tag
    tag_exists     = [bool]$tagCommit
    product_icon   = $iconStatus
    signing        = "UNSIGNED"
    # Opt-in only: a rehearsal that staged the operator-cleared catalog records
    # the asset count, the official package records the fact it stayed out.
    sound_assets   = if ($IncludeSounds) { $soundCount } else { "not staged (opt-in)" }
    # T-82: explicit acceptance evidence. Every audio claim here comes from a
    # captured test EVIDENCE line or a counted package fact; a skipped check
    # reads as SKIP, never as verified.
    verification   = [ordered]@{
        sound_catalog                = $soundCatalogStatus
        portable_package_asset_count = $packageFiles.Count
        ctest_exit_code              = $ctestRc
        audio                        = $audioEvidence
        audio_statuses               = $audioStatuses
        human_audible                = $humanAudibleStatus
        note                         = "QSoundEffect reaching Playing is device-reported playback, never a human hearing claim"
    }
    toolchain      = [ordered]@{ msvc = $msvcVer; qt = $qtVersion; cmake = ((cmake --version) | Select-Object -First 1) }
    tests_total    = $testTotal
    tests_passed   = $testTotal
    archive        = [ordered]@{ name = $zipName; sha256 = $zipHash; bytes = (Get-Item -LiteralPath $zipPath).Length }
    files          = @($packageFiles | ForEach-Object {
                        [ordered]@{ path = $_.FullName.Substring($stagePrefix.Length).Replace('\', '/'); sha256 = (Sha256 $_.FullName); bytes = $_.Length } })
}
$manifestPath = Join-Path $OutDir "$packageName.manifest.json"
[System.IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 6), (New-Object System.Text.UTF8Encoding($false)))

Step "RELEASE_PACKAGE_PASS ($mode)"
Write-Output "archive     $zipPath"
Write-Output "checksums   $sumsPath"
Write-Output "manifest    $manifestPath"
foreach ($s in $audioStatuses) {
    if ($s -notlike "HUMAN_AUDIBLE*") { Write-Output "STATUS      $s" }
}
Write-Output "STATUS      $humanAudibleStatus"
exit 0
