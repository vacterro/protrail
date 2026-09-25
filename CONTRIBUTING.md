# Contributing

## Development setup

Use Windows 10/11 with Visual Studio 2022, CMake 3.24 or newer, and Qt 6.8 `msvc2022_64` with Widgets and Test modules. Configure from a VS Developer PowerShell:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

Build both configurations and run the complete suite before opening a pull request:

```powershell
cmake --build build --config Release --parallel
cmake --build build --config Debug --parallel
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Debug --output-on-failure
```

The project treats MSVC warnings as errors. Keep persisted configuration changes covered by serialization, migration, canonical-default, and sentinel coverage. Main and Settings must remain views over the single application configuration authority; programmatic population must stay silent.

## Pull requests

Keep changes focused, explain user-visible behavior, include validation results, and do not commit generated build directories, local evidence dumps, credentials, or machine-specific paths. Do not add a license file without an explicit project decision.

Verification binaries may be built locally. The only supported way to produce a distributable package is `tools\release\package.ps1` (see the README's Packaging section); use `-Rehearsal` to exercise it before the final product icon exists. The final product icon is required before any official Windows executable release.
