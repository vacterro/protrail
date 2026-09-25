# ProTrail

🇷🇺 [Русский](../ru/README.md) · 🇺🇸 [English](../en/README.md) · 🇪🇪 [Eesti](../et/README.md) · 🇯🇵 [日本語](../ja/README.md) · 🇷🇺 [👴 Дед](../ded/README.md) · 🇺🇦 [Українська](../uk/README.md) · 🇩🇪 [Deutsch](../de/README.md) · 🇫🇷 [Français](../fr/README.md) · 🇪🇸 [Español](../es/README.md) · 🇮🇹 [Italiano](../it/README.md) · 🇵🇹 [Português](../pt/README.md) · 🇳🇱 [Nederlands](../nl/README.md) · 🇵🇱 [Polski](../pl/README.md) · 🇸🇪 [Svenska](../sv/README.md) · 🇩🇰 [Dansk](../da/README.md) · 🇫🇮 [Suomi](../fi/README.md) · 🇳🇴 [Norsk](../no/README.md) · 🇨🇳 [中文](../zh/README.md) · 🇰🇷 [한국어](../ko/README.md) · 🇹🇭 [ไทย](../th/README.md) · 🇻🇳 [Tiếng Việt](../vi/README.md) · 🇸🇦 [العربية](../ar/README.md) · 🇮🇱 [עברית](../he/README.md) · 🇹🇷 [Türkçe](../tr/README.md) · 🇮🇳 [हिन्दी](../hi/README.md) · 🇮🇩 [Bahasa Indonesia](../id/README.md) · 🇬🇷 [Ελληνικά](../el/README.md) · 🇨🇿 [Čeština](../cs/README.md) · 🇷🇴 [Română](../ro/README.md) · 🇭🇺 [Magyar](../hu/README.md) · 🇧🇬 [Български](../bg/README.md) · 🇸🇰 [Slovenčina](../sk/README.md) · 🇭🇷 [Hrvatski](README.md)

**v0.1.2**

ProTrail je izvorna Windows aplikacija za radnu površinu koja prikazuje prilagodljive tragove kursora, učinke klika i pritiska i držanja gumba te geometriju buđenja pri kretanju, a pritom nikada ne presreće, ne odgađa niti guta klikove miša. Riječ je o aplikaciji Qt 6 / C++20 koja putem Direct2D i DirectComposition prikazuje sadržaj u slojevima koji propuštaju klikove za svaki monitor.

## Trenutačni opseg

Samo Windows 10/11. Podržani skup alata je MSVC (Visual Studio 2022) s Qt 6.8.

- **Jedan ProTrail prozor** — korisničko sučelje sadrži kartice General, Trail i Click, a karticu Developer samo u razvojnim izdanjima. Svaka postavka uređuje se na jednom vidljivom mjestu; Trail i Click koriste vidljive mreže gumba za odabir umjesto padajućih kontrola za odabir stila. Kartica General zadana je pri pokretanju te sadrži prekidače za omogućavanje, pokretanje pri prijavi, brze sheme boja i gumb Restore All Defaults.
- **Kartice Trail i Click** — potpuni uređivači za Trail/Sparkle i Click/Hold/Motion Wake nalaze se u karticama s mogućnošću pomicanja, svaki s vlastitom radnjom za vraćanje postavki svojeg područja. Nema drugog prozora postavki ni dvostrukog sučelja za uređivanje.

## Učinci

- **Trail** — 8 stilova (Classic, Soft Glow, Comet, Neon, Dotted, Pulse, Ribbon i Spark) s početnom i završnom bojom, četiri načina rada boja, paletama od 14 uzoraka, odabirom prilagođene boje te kontrolama sužavanja, zaglađivanja i sjaja.
- **Sparkles** — 5 načina rada (Stardust, Twinkle, Glitter, Firefly i Shards) za determinističko raspoređivanje iskri duž vidljive putanje traga, uz kontrole količine, veličine i raspršenosti.
- **Click** — 11 stilova (Ring, Double Ring, Ripple, Burst, Spark Burst, Soft Flash, Dot + Ring te elementarni Air, Fire, Water i Earth), randomizacija po kliku koja ostaje stabilna neovisno o brzini iscrtavanja kadrova, okidači po pojedinom gumbu te kontrole čestica, boja i ublažavanja.
- **Hold** — pritisak i držanje zasebna je gesta u odnosu na klik: kontinuirana aura punjenja dok je gumb pritisnut te završni učinak nakon otpuštanja gumba, razmjeran trajanju držanja. Propušteni događaji otpuštanja gumba usklađuju se sa stvarnim stanjem gumba, pa izgubljeni događaj otpuštanja ne može ostaviti efekt držanja aktivnim.
- **Motion Wake** — dok se kursor pomiče uz aktivno držanje gumba, geometrija specifična za stil emitira se u svjetski koordinatni sustav. Svaki emitirani element trajno zadržava početnu sidrenu točku i animira se neovisno o kursoru. Napredne kontrole obuhvaćaju snagu i veličinu buđenja, raspršenost, odziv na brzinu, najmanju dopuštenu brzinu kretanja te naglaske pri skretanju i zaustavljanju.

## Windows integracija

- **Više monitora i DPI** — jedan preklopni sloj koji propušta klikove i ne preuzima fokus po monitoru prema ugovoru procesa osjetljivog na DPI svakog monitora, uz odgođeno usklađivanje topologije nakon promjena zaslona, priključivanja ili isključivanja te mješovitih DPI postavki.
- **Ponašanje u mirovanju** — iscrtavanje se odvija samo dok je sadržaj aktivan. ProTrail u mirovanju nema stalni mjerač vremena za iscrtavanje.
- **Pokretanje sa sustavom Windows** — unos `Run` u korisničkom profilu kojim upravlja ProTrail, čija naredba sadrži trenutačnu putanju izvršne datoteke u cijelosti pod navodnicima i izričiti argument za automatsko pokretanje. Piše se ili uklanja samo vrijednost kojom upravlja ProTrail, a registracija se pri pokretanju usklađuje sa stvarnim putem izvršne datoteke.
- **Načini pokretanja** — ručno pokretanje otvara jedini ProTrailov prozor na kartici General. Pri automatskom pokretanju sa sustavom Windows aplikacija se tiho prikazuje samo u sistemskoj traci: nema korisničkog prozora, gumba na traci zadataka ni preuzimanja fokusa. Drugo ručno pokretanje postojećoj instanci predaje zahtjev za aktivaciju, koja vraća isti prozor.
- **Sistemska traka i životni ciklus** — `Open ProTrail`, `Enable`/`Disable` i `Exit`. Zatvaranje prozora skriva ga u sistemsku traku i ostavlja ProTrail pokrenut; izlaz je izričita radnja u sistemskoj traci. Dvoklik na ikonu u sistemskoj traci koristi istu radnju Open.

## Konfiguracija

Korisnička konfiguracija nalazi se u `%LOCALAPPDATA%\ProTrail\config.json`, zapisuje se atomski te se pri učitavanju provjerava prema shemi (trenutačno shema 11). Datoteke starijih shema migriraju se prema naprijed; vrijednost uvedena novijom shemom popravlja se na njezinu dokumentiranu sigurnu zadanu vrijednost umjesto drukčije interpretacije.

**Restore Defaults** primjenjuje kanonske zadane vrijednosti ugrađene pri izgradnji iz `resources/release_defaults.json` — za novu instalaciju i izričito vraćanje postoji točno jedan izvor zadanih vrijednosti.

Razvojna izdanja dodatno nude **Set Current as Release Defaults**. Ta funkcija preuzima potpunu kanonsku konfiguraciju aplikacije, atomski zapisuje izvor zadanih vrijednosti, ponovno ga čita i raščlanjuje kako bi dokazala semantičku jednakost te prikazuje uspjeh i promijenjene postavke. U produkcijskom izdanju Release funkcija se u potpunosti odbija i nikada se ne isporučuje običnim korisnicima.

## Zahtjevi za izgradnju

- Windows 10 ili noviji
- Visual Studio 2022 / MSVC v143
- CMake 3.24+
- Qt 6.8.x `msvc2022_64` s modulima Widgets i Test

Konfigurirajte i izgradite projekt iz PowerShella za razvojne programere u Visual Studiju:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
cmake --build build --config Debug --parallel
```

Korak konfiguriranja provjerava kanonski izvor zadanih vrijednosti izdanja i jasno otkazuje ako nedostaje ili je neispravan. Izgradnje MSVC-a koriste `/W4 /WX`. Konfigurirajte s `-DPROTRAIL_DEV_BUILD=ON` kako biste u izdanju Release omogućili razvojno sučelje za uređivanje zadanih vrijednosti izdanja (u izdanjima Debug ono je uvijek omogućeno).

## Testovi

Pokrenite cijeli skup za konfiguraciju:

```powershell
ctest --test-dir build -C Release --output-on-failure
ctest --test-dir build -C Debug --output-on-failure
```

Skup testova registrira cijelu CTest matricu za trag, iskrice, klik, držanje/buđenje, više monitora, raspoređivač, konfiguraciju, zadane release vrijednosti, automatsko pokretanje, jedinstvenu instancu, životni ciklus sistemske trake te objedinjeno grafičko sučelje, uz dvije testne skripte za kontrole zadanih vrijednosti izdanja; jedan namjerno dokazuje da kontrola može otkazati. Grafički testovi rade bez zaslona putem Qt-ove platforme `offscreen`.

## Struktura repozitorija

| Putanja | Sadržaj |
|------|----------|
| `src/` | Izvorni kod aplikacije (app, config, core, effects, platform, render, ui) |
| `tests/` | Izvršne datoteke testova registrirane u CTestu |
| `resources/` | Kanonski izvor zadanih vrijednosti i pripadajući Qt resurs |
| `resources/windows/` | Predložak resursa VERSIONINFO / ikone za Windows |
| `resources/branding/` | Odobrena ikona proizvoda (`protrail.ico`) i zapis odobrenja |
| `cmake/` | Validator zadanih release vrijednosti i kontrola identiteta izdanja pri konfiguriranju |
| `tools/release/` | Cjevovod za pakiranje i provjeru prijenosne Windows x64 verzije |
| `ROADMAP/` | Planovi, reference i povijesne bilješke o dizajnu |
| `ROADMAP/evidence/` | Zadržani zapisi o dizajnu i mjerenjima po pojedinim zadacima |

## Status izdanja

**v0.1.2 objavljeno je izdanje samo s izvornim kodom i hrvatskim prijevodom
dokumenacije. v0.1.1 ostaje prvo binarno izdanje za Windows x64, ali nije
objavljeno kao službena binarna distribucija.** Dosad nije objavljena službena
Windows EXE datoteka, instalacijski program, prijenosni arhiv, binarna datoteka
za upravitelj paketa ni potpisana binarna datoteka, a CI namjerno ne objavljuje
binarne datoteke.

Službena binarna distribucija za Windows ovisi o dostavi i prihvaćanju konačne ikone proizvoda ProTrail
(`FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING`, vidi
[RELEASE_BLOCKERS.md](RELEASE_BLOCKERS.md)). Stvorena ikona kursora u sistemskoj traci razvojna je zamjena, a ne odobreno konačno brendiranje.

### Pakiranje

Planirano binarno izdanje jest prijenosni arhiv
`ProTrail-v<VERSION>-win-x64-portable.zip` i `SHA256SUMS.txt`. Jednom naredbom iz čistog radnog stabla izdanje se izgradi, testira, pripremi, provjeri i zapakira:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\package.ps1
```

Cjevovod izvodi novu izgradnju Release s `/W4 /WX` i cijeli skup CTest, postavlja Qt runtime putem `windeployqt` i MSVC runtime lokalno uz aplikaciju te dokazuje da paket sadrži sve zavisnosti. Zatim provodi probnu provjeru pripremljenog paketa pod sistemskim `PATH` iz običnih direktorija, direktorija s razmacima, ugnijeđenih direktorija i direktorija s Unicode znakovima, zapisuje deterministički ZIP i njegov SHA-256 te provodi probnu provjeru svježeg izvlačenja tog ZIP-a. Službeni način odbacuje prljavo radno stablo te nedostajuću ili neodobrenu ikonu; `-Rehearsal` pokreće iste kontrole bez ta dva uvjeta i svaki izlaz naziva `...-REHEARSAL` kako se ne bi moglo zamijeniti s izlaznim artefaktom izdanja.

## Doprinos i sigurnost

Vidi [CONTRIBUTING.md](CONTRIBUTING.md) za tijek izgradnje i testiranja te
[SECURITY.md](SECURITY.md) za privatne prijave ranjivosti. Licenciranje je namjerno neodređeno dok se ne odabere licenca; ovaj repozitorij ne podrazumijeva nijednu licencu.
