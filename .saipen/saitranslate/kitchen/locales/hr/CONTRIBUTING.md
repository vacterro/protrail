# Doprinos

🇷🇺 [Русский](../ru/CONTRIBUTING.md) · 🇺🇸 [English](../en/CONTRIBUTING.md) · 🇪🇪 [Eesti](../et/CONTRIBUTING.md) · 🇯🇵 [日本語](../ja/CONTRIBUTING.md) · 🇷🇺 [👴 Дед](../ded/CONTRIBUTING.md) · 🇺🇦 [Українська](../uk/CONTRIBUTING.md) · 🇩🇪 [Deutsch](../de/CONTRIBUTING.md) · 🇫🇷 [Français](../fr/CONTRIBUTING.md) · 🇪🇸 [Español](../es/CONTRIBUTING.md) · 🇮🇹 [Italiano](../it/CONTRIBUTING.md) · 🇵🇹 [Português](../pt/CONTRIBUTING.md) · 🇳🇱 [Nederlands](../nl/CONTRIBUTING.md) · 🇵🇱 [Polski](../pl/CONTRIBUTING.md) · 🇸🇪 [Svenska](../sv/CONTRIBUTING.md) · 🇩🇰 [Dansk](../da/CONTRIBUTING.md) · 🇫🇮 [Suomi](../fi/CONTRIBUTING.md) · 🇳🇴 [Norsk](../no/CONTRIBUTING.md) · 🇨🇳 [中文](../zh/CONTRIBUTING.md) · 🇰🇷 [한국어](../ko/CONTRIBUTING.md) · 🇹🇭 [ไทย](../th/CONTRIBUTING.md) · 🇻🇳 [Tiếng Việt](../vi/CONTRIBUTING.md) · 🇸🇦 [العربية](../ar/CONTRIBUTING.md) · 🇮🇱 [עברית](../he/CONTRIBUTING.md) · 🇹🇷 [Türkçe](../tr/CONTRIBUTING.md) · 🇮🇳 [हिन्दी](../hi/CONTRIBUTING.md) · 🇮🇩 [Bahasa Indonesia](../id/CONTRIBUTING.md) · 🇬🇷 [Ελληνικά](../el/CONTRIBUTING.md) · 🇨🇿 [Čeština](../cs/CONTRIBUTING.md) · 🇷🇴 [Română](../ro/CONTRIBUTING.md) · 🇭🇺 [Magyar](../hu/CONTRIBUTING.md) · 🇧🇬 [Български](../bg/CONTRIBUTING.md) · 🇸🇰 [Slovenčina](../sk/CONTRIBUTING.md) · 🇭🇷 [Hrvatski](CONTRIBUTING.md)

## Razvojno okruženje

Koristite Windows 10/11 s Visual Studio 2022, CMake 3.24 ili novijom verzijom te Qt 6.8 `msvc2022_64` s modulima Widgets i Test. Konfigurirajte projekt u PowerShellu za razvojne programere u Visual Studiju:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

Prije otvaranja pull requesta izgradite obje konfiguracije i pokrenite cijeli skup testova:

```powershell
cmake --build build --config Release --parallel
cmake --build build --config Debug --parallel
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Debug --output-on-failure
```

Projekt upozorenja MSVC-a tretira kao pogreške. Promjene trajno pohranjenih postavki moraju biti pokrivene testovima serializacije, migracija, kanonskih zadanih vrijednosti i sentinel-vrijednosti. Prozori Main i Settings moraju ostati prikazi nad jednim izvorom konfiguracije aplikacije; programatsko popunjavanje mora ostati tiho.

## Pull requestovi

Ograničite promjene, objasnite ponašanje vidljivo korisniku, navedite rezultate provjere i nemojte predavati generirane build direktorije, lokalne zapise dokaza, vjerodajnice ni putove specifične za računalo. Nemojte dodavati datoteku licenci bez izričite odluke projekta.

Testne binarne datoteke mogu se izgraditi lokalno. Jedini podržani način stvaranja distribucijskog paketa je `tools\release\package.ps1` (poglavlje Packaging u README-u); koristite `-Rehearsal` za probni postupak dok konačna ikona proizvoda još nije dostupna. Konačna ikona proizvoda potrebna je prije objave službenog Windows izvršnog programa.
