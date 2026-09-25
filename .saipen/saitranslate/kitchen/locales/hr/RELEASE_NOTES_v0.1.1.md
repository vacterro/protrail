# ProTrail v0.1.1

🇷🇺 [Русский](../ru/RELEASE_NOTES_v0.1.1.md) · 🇺🇸 [English](../en/RELEASE_NOTES_v0.1.1.md) · 🇪🇪 [Eesti](../et/RELEASE_NOTES_v0.1.1.md) · 🇯🇵 [日本語](../ja/RELEASE_NOTES_v0.1.1.md) · 🇷🇺 [👴 Дед](../ded/RELEASE_NOTES_v0.1.1.md) · 🇺🇦 [Українська](../uk/RELEASE_NOTES_v0.1.1.md) · 🇩🇪 [Deutsch](../de/RELEASE_NOTES_v0.1.1.md) · 🇫🇷 [Français](../fr/RELEASE_NOTES_v0.1.1.md) · 🇪🇸 [Español](../es/RELEASE_NOTES_v0.1.1.md) · 🇮🇹 [Italiano](../it/RELEASE_NOTES_v0.1.1.md) · 🇵🇹 [Português](../pt/RELEASE_NOTES_v0.1.1.md) · 🇳🇱 [Nederlands](../nl/RELEASE_NOTES_v0.1.1.md) · 🇵🇱 [Polski](../pl/RELEASE_NOTES_v0.1.1.md) · 🇸🇪 [Svenska](../sv/RELEASE_NOTES_v0.1.1.md) · 🇩🇰 [Dansk](../da/RELEASE_NOTES_v0.1.1.md) · 🇫🇮 [Suomi](../fi/RELEASE_NOTES_v0.1.1.md) · 🇳🇴 [Norsk](../no/RELEASE_NOTES_v0.1.1.md) · 🇨🇳 [中文](../zh/RELEASE_NOTES_v0.1.1.md) · 🇰🇷 [한국어](../ko/RELEASE_NOTES_v0.1.1.md) · 🇹🇭 [ไทย](../th/RELEASE_NOTES_v0.1.1.md) · 🇻🇳 [Tiếng Việt](../vi/RELEASE_NOTES_v0.1.1.md) · 🇸🇦 [العربية](../ar/RELEASE_NOTES_v0.1.1.md) · 🇮🇱 [עברית](../he/RELEASE_NOTES_v0.1.1.md) · 🇹🇷 [Türkçe](../tr/RELEASE_NOTES_v0.1.1.md) · 🇮🇳 [हिन्दी](../hi/RELEASE_NOTES_v0.1.1.md) · 🇮🇩 [Bahasa Indonesia](../id/RELEASE_NOTES_v0.1.1.md) · 🇬🇷 [Ελληνικά](../el/RELEASE_NOTES_v0.1.1.md) · 🇨🇿 [Čeština](../cs/RELEASE_NOTES_v0.1.1.md) · 🇷🇴 [Română](../ro/RELEASE_NOTES_v0.1.1.md) · 🇭🇺 [Magyar](../hu/RELEASE_NOTES_v0.1.1.md) · 🇧🇬 [Български](../bg/RELEASE_NOTES_v0.1.1.md) · 🇸🇰 [Slovenčina](../sk/RELEASE_NOTES_v0.1.1.md) · 🇭🇷 [Hrvatski](RELEASE_NOTES_v0.1.1.md)

> **Status: pripremljeno, još nije objavljeno.** Ovo je tekst prvog
> službenog binarnog izdanja za Windows. Ne objavljuje se dok konačna ikona
> proizvoda ne bude dostavljena i prihvaćena
> (`FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING`, vidi
> [RELEASE_BLOCKERS.md](RELEASE_BLOCKERS.md)); ovaj natpis uklanja se u
> commitu izdanja.

ProTrail v0.1.1 prvo je binarno izdanje aplikacije ProTrail za Windows. ProTrail je izvorna aplikacija za radnu površinu koja prikazuje prilagodljive tragove kursora, učinke klika i pritiska i držanja gumba te geometriju buđenja pri kretanju u slojevima za svaki monitor koji propuštaju klikove, bez presretanja, odgađanja ili gutanja klikova miša.

## Preuzimanje

| Resurs | Sadržaj |
|-------|----------|
| `ProTrail-v0.1.1-win-x64-portable.zip` | Prijenosna aplikacija: `protrail.exe`, Qt 6.8 runtime i MSVC runtime |
| `SHA256SUMS.txt` | SHA-256 prijenosnog arhiva |

- **Platforma:** samo Windows 10 ili noviji, x64.
- **Vrsta paketa:** prijenosni arhiv. Nema instalacijskog programa.
- **Potpisivanje:** izvršna datoteka **nije potpisana digitalnim potpisom**. Windows SmartScreen može pri prvom pokretanju prikazati upozorenje; prvo provjerite arhiv prema `SHA256SUMS.txt`.

## Instalacija i korištenje

1. Provjerite preuzimanje:
   `(Get-FileHash .\ProTrail-v0.1.1-win-x64-portable.zip -Algorithm SHA256).Hash`
   mora odgovarati vrijednosti u `SHA256SUMS.txt`.
2. Izdvojite arhiv u bilo koji direktorij koji posjedujete, primjerice
   `%LOCALAPPDATA%\Programs\ProTrail`. Podržane su putanje s razmacima i znakovima koji nisu ASCII.
3. Pokrenite `protrail.exe`. ProTrail otvara svoj prozor i prikazuje ikonu u
   području obavijesti; zatvaranjem prozora aplikacija se skriva u sistemsku traku, a odabirom **Exit** u njezinu izborniku aplikacija se zatvara.
4. Postavke se po korisniku spremaju u `%LOCALAPPDATA%\ProTrail\config.json`,
   a zapisnik u isti direktorij. Tijekom uobičajenog rada ništa se ne zapisuje u
   direktorij aplikacije. Samo ako Windows ne može odrediti korisnički direktorij
   za podatke aplikacije, postavke i zapisnik pohranjuju se u direktorij aplikacije.
5. Postavka **Start with Windows** (kartica General) registrira izvršnu datoteku koju pokrenete. Ako premjestite direktorij, pokrenite ProTrail jednom s nove lokacije i registracija ga slijedi. Za uklanjanje ProTraila isključite Start with Windows, izađite i obrišite direktorij (te `%LOCALAPPDATA%\ProTrail` da biste izbrisali postavke).

## Što sadrži

- Jedan ProTrailov prozor s karticama (General, Trail, Click) i uživo uređivanjem
  učinaka Trail/Sparkle i Click/Hold/Motion Wake.
- Životni ciklus sistemske trake: `Open ProTrail`, Enable/Disable, Exit; ikona
  u sistemskoj traci prikazuje uključeno/onemogućeno stanje; pri automatskom
  pokretanju sa sustavom Windows aplikacija se tiho pokreće samo u sistemskoj traci.
- Crtanje Direct2D/DirectComposition osjetljivo na DPI svakog monitora na
  više monitora, uz raspored crtanja samo dok je sadržaj aktivan (nema
  buđenja u mirovanju).
- Konfiguracija s provjerom sheme i atomskim zapisom te sigurnim oporavkom od
  oštećene datoteke; jedna pokrenuta instanca, pri čemu drugo pokretanje vraća
  postojeći prozor.

## Promjene od v0.1.0

Pogledajte [CHANGELOG.md](CHANGELOG.md). Ukratko: dodani su resursi verzije i ikone
za Windows te provjereni cjevovod za izradu prijenosnog paketa. Ispravljeno je i
curenje razvojne putanje u binarnim datotekama Release te su popravljena probna
testiranja postavljanja koja su mogla izmijeniti stvarni korisnikov unos za
automatsko pokretanje.
