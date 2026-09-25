# ProTrail v0.1.5

## Oporavak izdanja

Korijenska datoteka `VERSION` preimenovana je u `RELEASE_VERSION` kako Windows
više ne bi koristio datoteku repozitorija kao C++ zaglavlje `<version>`.
Nepromjenjive oznake `v0.1.3` i `v0.1.4` ostaju netaknute.

## Sadržaj

- `ProTrail-v0.1.5-win-x64-portable.zip`
- `SHA256SUMS.txt`

Paket je bez digitalnog potpisa jer certifikat za potpis nije konfiguriran.
Sadrži `protrail.exe`, Qt 6.8 runtime i lokalno MSVC runtime.

## Branding

`resources/branding/protrail.ico` jedini je izvor simbola proizvoda, sa RGBA
stavkama veličina 16, 20, 24, 32, 40, 48, 64, 128 i 256 px. Točan SHA-256
nalazi se u `resources/branding/APPROVAL.md`.

## Provjera

Službena objava zahtijeva čisto označeno stablo, `PROTRAIL_REQUIRE_FINAL_ICON=ON`,
Release build bez upozorenja, potpuni CTest PASS, package smoke PASS i podudarne
SHA-256 vrijednosti.
