# Blokatori izdanja

🇷🇺 [Русский](../ru/RELEASE_BLOCKERS.md) · 🇺🇸 [English](../en/RELEASE_BLOCKERS.md) · 🇪🇪 [Eesti](../et/RELEASE_BLOCKERS.md) · 🇯🇵 [日本語](../ja/RELEASE_BLOCKERS.md) · 🇷🇺 [👴 Дед](../ded/RELEASE_BLOCKERS.md) · 🇺🇦 [Українська](../uk/RELEASE_BLOCKERS.md) · 🇩🇪 [Deutsch](../de/RELEASE_BLOCKERS.md) · 🇫🇷 [Français](../fr/RELEASE_BLOCKERS.md) · 🇪🇸 [Español](../es/RELEASE_BLOCKERS.md) · 🇮🇹 [Italiano](../it/RELEASE_BLOCKERS.md) · 🇵🇹 [Português](../pt/RELEASE_BLOCKERS.md) · 🇳🇱 [Nederlands](../nl/RELEASE_BLOCKERS.md) · 🇵🇱 [Polski](../pl/RELEASE_BLOCKERS.md) · 🇸🇪 [Svenska](../sv/RELEASE_BLOCKERS.md) · 🇩🇰 [Dansk](../da/RELEASE_BLOCKERS.md) · 🇫🇮 [Suomi](../fi/RELEASE_BLOCKERS.md) · 🇳🇴 [Norsk](../no/RELEASE_BLOCKERS.md) · 🇨🇳 [中文](../zh/RELEASE_BLOCKERS.md) · 🇰🇷 [한국어](../ko/RELEASE_BLOCKERS.md) · 🇹🇭 [ไทย](../th/RELEASE_BLOCKERS.md) · 🇻🇳 [Tiếng Việt](../vi/RELEASE_BLOCKERS.md) · 🇸🇦 [العربية](../ar/RELEASE_BLOCKERS.md) · 🇮🇱 [עברית](../he/RELEASE_BLOCKERS.md) · 🇹🇷 [Türkçe](../tr/RELEASE_BLOCKERS.md) · 🇮🇳 [हिन्दी](../hi/RELEASE_BLOCKERS.md) · 🇮🇩 [Bahasa Indonesia](../id/RELEASE_BLOCKERS.md) · 🇬🇷 [Ελληνικά](../el/RELEASE_BLOCKERS.md) · 🇨🇿 [Čeština](../cs/RELEASE_BLOCKERS.md) · 🇷🇴 [Română](../ro/RELEASE_BLOCKERS.md) · 🇭🇺 [Magyar](../hu/RELEASE_BLOCKERS.md) · 🇧🇬 [Български](../bg/RELEASE_BLOCKERS.md) · 🇸🇰 [Slovenčina](../sk/RELEASE_BLOCKERS.md) · 🇭🇷 [Hrvatski](RELEASE_BLOCKERS.md)

## FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING

Status: **OTVORENO** (inženjering izdanja v0.1.1 dovršen je do ove točke).

Korisnik će izraditi ili odabrati konačnu ikonu proizvoda ProTrail. Dok taj resurs ne bude dostavljen i prihvaćen, nemojte distribuirati službenu EXE datoteku, instalacijski program, prijenosni binarni paket, potpisani izvršni program ni izdanje Windowsa s ProTrail brendiranjem.

Automatski generirana ikona kursora QPainter ostaje razvojna zamjena. Nije odobreno konačno brendiranje, a nijedan automatski generirani, zamjenski ili umjetnom inteligencijom generirani kandidat ne smije se smatrati odobrenim bez izričitog korisnikova prihvatanja.

### Što je već postavljeno

- `resources/windows/protrail.rc.in` gradi VERSIONINFO i, kad resurs postoji, povezuje ga kao izvor ikone `IDI_ICON1` (ikona Explorera/PE-a; zadana Qt ikona prozora i trake zadataka).
- `ptd::ui::branding` čita isti resurs za Qt površine i sistemsku traku: uključeno stanje sistemske trake ikona je proizvoda, a onemogućeno stanje njezin deterministički obrađen izgled (potpuno bez zasićenosti, neprozirnost 45 %).
  `protrail_tray_tests` dokazuje da resurs odgovara ulazu za izgradnju te da se dva stanja sistemske trake vidljivo razlikuju pri 16, 20, 24 i 32 px.
- `-DPROTRAIL_REQUIRE_FINAL_ICON=ON` (koji službeni cjevovod koristi) pada tijekom konfiguriranja bez resursa.
- `tools/release/package.ps1` odbija službeno pakiranje osim ako ikona postoji **i** `APPROVAL.md` zapisuje njezin SHA-256. Ispitna okolina zatim zahtijeva izvor ikone u zapakiranom izvršnom programu.

### Potrebni resurs

| Stavka | Zahtjev |
|------|-------------|
| Datoteka | `resources/branding/protrail.ico` |
| Format | Windows ICO, stavke RGBA od 32 bita (stavka od 256 px smije biti komprimirana u PNG) |
| Veličine | 16, 20, 24, 32, 40, 48, 64, 128, 256 px (najmanji prihvatljiv skup: 16, 24, 32, 48, 256) |
| Male veličine | Veličine 16/20/24/32 px treba ručno doraditi umjesto smanjivanja; to su veličine za sistemsku traku i moraju biti čitljive na svijetlim i tamnim trakama zadataka |
| Izvorni predložak (snažno preporučen) | `resources/branding/protrail-master.svg` ili PNG dimenzija najmanje 1024 x 1024, kako bi se svaka veličina mogla ponovno stvoriti bez povećavanja male rasterske slike |
| Onemogućeno stanje | izvodi se automatski; zasebna grafika za onemogućeno stanje nije obavezna i zahtijevala bi malu promjenu koda |
| Zapis odobrenja | `resources/branding/APPROVAL.md` s retkom `sha256: <lowercase SHA-256 of protrail.ico>`, imenom osobe koja ga je odobrila i datumom, napisan samo nakon izričitog korisnikova prihvatanja |

### Sljedeća radnja nakon dostave resursa

1. Dodajte navedene datoteke i napišite `APPROVAL.md` za točne bajtove datoteke `.ico`.
2. Provjerite ikonu u razvojnom izdanju (Explorer, taskbar, prozor, uključena/onemogućena sistemska traka).
3. Ažurirajte ovu datoteku kako biste zabilježili odluku, uklonite natpis „pripremljeno, još nije objavljeno” iz `RELEASE_NOTES_v0.1.1.md` i označite unos CHANGELOG-a kao objavljen.
4. Podnesite promjene, zatim pokrenite službeni cjevovod iz čistog radnog stabla:
   `powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\package.ps1`
5. Stvorite oznaku `v0.1.1` na toj reviziji (s opisom, nikada je nemojte premještati), pošaljite granu i oznaku, zahtijevajte zeleni CI za označenu reviziju, ponovno izgradite iz oznake i objavite `ProTrail-v0.1.1-win-x64-portable.zip` + `SHA256SUMS.txt` s `RELEASE_NOTES_v0.1.1.md`.

Ovaj blokator ne blokira čišćenje izvornog koda, automatske testove, CI, dokumentaciju, probe pakiranja ni izdanja samo s izvornim kodom.
