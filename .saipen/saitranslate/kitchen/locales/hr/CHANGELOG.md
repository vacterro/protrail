# Dnevnik promjena

🇷🇺 [Русский](../ru/CHANGELOG.md) · 🇺🇸 [English](../en/CHANGELOG.md) · 🇪🇪 [Eesti](../et/CHANGELOG.md) · 🇯🇵 [日本語](../ja/CHANGELOG.md) · 🇷🇺 [👴 Дед](../ded/CHANGELOG.md) · 🇺🇦 [Українська](../uk/CHANGELOG.md) · 🇩🇪 [Deutsch](../de/CHANGELOG.md) · 🇫🇷 [Français](../fr/CHANGELOG.md) · 🇪🇸 [Español](../es/CHANGELOG.md) · 🇮🇹 [Italiano](../it/CHANGELOG.md) · 🇵🇹 [Português](../pt/CHANGELOG.md) · 🇳🇱 [Nederlands](../nl/CHANGELOG.md) · 🇵🇱 [Polski](../pl/CHANGELOG.md) · 🇸🇪 [Svenska](../sv/CHANGELOG.md) · 🇩🇰 [Dansk](../da/CHANGELOG.md) · 🇫🇮 [Suomi](../fi/CHANGELOG.md) · 🇳🇴 [Norsk](../no/CHANGELOG.md) · 🇨🇳 [中文](../zh/CHANGELOG.md) · 🇰🇷 [한국어](../ko/CHANGELOG.md) · 🇹🇭 [ไทย](../th/CHANGELOG.md) · 🇻🇳 [Tiếng Việt](../vi/CHANGELOG.md) · 🇸🇦 [العربية](../ar/CHANGELOG.md) · 🇮🇱 [עברית](../he/CHANGELOG.md) · 🇹🇷 [Türkçe](../tr/CHANGELOG.md) · 🇮🇳 [हिन्दी](../hi/CHANGELOG.md) · 🇮🇩 [Bahasa Indonesia](../id/CHANGELOG.md) · 🇬🇷 [Ελληνικά](../el/CHANGELOG.md) · 🇨🇿 [Čeština](../cs/CHANGELOG.md) · 🇷🇴 [Română](../ro/CHANGELOG.md) · 🇭🇺 [Magyar](../hu/CHANGELOG.md) · 🇧🇬 [Български](../bg/CHANGELOG.md) · 🇸🇰 [Slovenčina](../sk/CHANGELOG.md) · 🇭🇷 [Hrvatski](CHANGELOG.md)

## 0.1.9 - Obnova kanonskog izvora verzije izdanja

- Korijenska datoteka `VERSION` ponovno je jedini izvor broja verzije izdanja; vraćeno je preimenovanje iz `RELEASE_VERSION` uvedeno u v0.1.5.
- To preimenovanje bilo je zaobilazno rješenje za zasjenjivanje MSVC zaglavlja `<version>`. Izdanje v0.1.8 ispravilo je putove uključivanja testova koji su ga uzrokovali, pa čista izgradnja s korijenskom datotekom `VERSION` sada daje izvršnu datoteku bez pogrešaka i upozorenja te bez C2059.
- Preimenovanje je također onemogućavalo kanonsku putanju objave `saipen ship`, koja zahtijeva korijensku datoteku `VERSION`. Ovo je prvo izdanje objavljeno tom putanjom pa nosi predanu potvrdu izdanja.
- Ponašanje proizvoda nije se promijenilo; prijenosni paket sadržajno je jednak v0.1.8 osim resursa VERSIONINFO koji sada navodi 0.1.9.

## 0.1.8 - Službeno prijenosno izdanje Windows x64 — potpuno zatvaranje

- Predano je potpuno izvorno zatvaranje proizvoda koje zahtijeva kanonski CMake popis ciljeva: izvori objedinjenog prozora, uklanjanje povučenog `MainWindow`-a, T-59 popravci izvršavanja, ispravljeni putovi uključivanja testova te ojačane provjere dima i postavljanja.
- Nepromjenjive oznake `v0.1.3` do `v0.1.7` su sačuvane; svaka međuzimska oznaka ostaje nepotpuni zapis izgradnje paketa i nijedna nije pomaknuta niti obrisana.
- Pripremljen je cjevovod za nepotpisani prijenosni paket; provjere pakiranja, dima, kontrolne sume i objave ostaju autoritativne.

## 0.1.7 - Nepotpuni zapis paketa (nadomješten izdanjem v0.1.8)

- Dodani su preostali izvori `src/ui/branding.h` i `src/ui/branding.cpp`, ali predani `Application` još je upotrebljavao povučenu implementaciju `MainWindow`, pa čisto povezivanje nije uspjelo.
- Nepromjenjive oznake `v0.1.3` do `v0.1.6` su sačuvane; nijedna oznaka nije pomaknuta niti obrisana.

## 0.1.6 - Nepotpuni zapis paketa (nadomješten izdanjem v0.1.8)

- Dodan je potrebni izvorni `src/app/topology_retry.h`, ali čisti popis ciljeva još je upotrebljavao nepredane izvore brendiranja i povučeni kod `MainWindow`.
- Nepromjenjive oznake `v0.1.3` do `v0.1.5` su sačuvane; nijedna oznaka nije pomaknuta niti obrisana.

## 0.1.5 - Službeno prijenosno izdanje Windows x64 — oporavak

- Korijenski izvor verzije preimenovan je iz `VERSION` u `RELEASE_VERSION`, čime je uklonjeno prekrivanje C++ zaglavlja `<version>` u Windows sustavu.
- Nepromjenjive oznake `v0.1.3` i `v0.1.4` su sačuvane; ovo izdanje sadrži odobreni simbol i ispravan raspored datoteke verzije.
- Pripremljen je cjevovod za nepotpisani prijenosni paket; provjere pakiranja, dima, kontrolne sume i objave ostaju autoritativne.

## 0.1.4 - Službeno prijenosno izdanje Windows x64 — oporavak

- Ispravljen je nepotpuni zapis izdanja v0.1.3 bez pomicanja ili brisanja njegove nepromjenjive oznake; stvarno izdanje sa simbolom je v0.1.4.
- Ugrađen je operatorijski odobreni deterministički simbol ProTrail u PE resurs, prozor, traku zadataka i sistemsku traku.
- Dodani su višerazlučivi `protrail.ico`, glavni SVG/PNG izvor i točan zapis SHA-256 odobrenja.
- Pripremljen je cjevovod za nepotpisani prijenosni paket; provjere pakiranja, dima, kontrolne sume i objave ostaju autoritativne.

## 0.1.3 - Priprema službenog prijenosnog izdanja za Windows x64

- Ugrađen je operatorijski odobreni deterministički simbol proizvoda ProTrail u PE resurs, prozor, traku zadataka i sistemsku traku.
- Dodani su višerazlučivi `protrail.ico`, glavni SVG/PNG izvor i točan zapis SHA-256 odobrenja.
- Pripremljen je cjevovod za nepotpisani prijenosni paket; provjere pakiranja, dima, kontrolne sume i objave ostaju autoritativne.

## 0.1.2 - Hrvatska dokumentacijska zakrpa (samo izvorni kod)

- Dodan je cjelovit hrvatski prijevod sedam održavanih dokumentacijskih površina pod lokalom `hr`.
- Ažurirane su zrcalne i kuhinjske metapodatke za identitet izdanja `v0.1.2` samo s izvornim kodom.
- U izdanje nisu uključeni službena Windows binarna datoteka, instalacijski program, prijenosni arhiv ni potpisani izvršni program; blokada završne ikone proizvoda ostaje otvorena.

## 0.1.1 - Priprema prijenosnog izdanja za Windows x64 (nepobjavljeno)

- Korijenska datoteka `VERSION` postala je jedini izvor broja verzije izdanja: CMake `project()`, resurs VERSIONINFO za Windows i cjevovod za pakiranje izvode se iz nje, a nova CTest kontrola `protrail_release_identity` otkazuje kada se s njom ne podudaraju README oznaka, najnoviji unos CHANGELOG-a ili bilješke izdanja.
- Dodani su resursi proizvoda za Windows: VERSIONINFO (ProductName/FileDescription `ProTrail`, FileVersion/ProductVersion iz `VERSION`, OriginalFilename `protrail.exe`) te mjesto za ikonu aplikacije `IDI_ICON1`. Odobrena ikona proizvoda na `resources/branding/protrail.ico` postaje jedini izvor ikone izvršne datoteke, prozora, trake zadataka i sistemske trake. U onemogućenom stanju ikona sistemske trake prikazuje se kao deterministički desaturirana i prigušena inačica te ikone.
- Dodan je `tools/release/package.ps1`, cjevovod za prijenosno pakiranje: čista izgradnja Release i puni CTest, vrijeme izvođenja putem windeployqt i lokalno MSVC vrijeme izvođenja uz aplikaciju, provjere zatvorenosti zavisnosti i curenja puteva razvojnog okruženja, probna provjera uz PATH očišćen od dodatnih puteva, iz običnih puteva, puteva s razmacima, ugnijeđenih puteva i puteva s Unicode znakovima, deterministički ZIP sa SHA-256 te probna provjera neovisno izdvojenog arhiva.
- Ispravljeno: binarne datoteke Release sadržavale su putanju do razvojne kopije izvornog koda; ta se putanja sada definira samo u razvojnim izdanjima.
- Ispravljeno: probna provjera sa svježom izoliranom konfiguracijom uskladila bi unos za automatsko pokretanje stvarnog korisnika u `HKCU\...\Run` i uklonila njegovu stvarnu registraciju za pokretanje sa sustavom Windows; probni način sada koristi pozadinski mehanizam za automatsko pokretanje u memoriji, a ispitni sklop prijavljuje pogrešku ako se unos `Run` promijeni.
- Ispravljeno: `deploy.bat` prisilno je prekidao svaki pokrenuti `protrail.exe`, uključujući vlastitu instancu operatera; sada predaje posao ispitnoj okolini, koja uklanja samo procese koje je sama pokrenula.
- Ispravljeno: probna provjera postavljanja tražila je povučeni naslov prozora `ProTrail Settings` umjesto objedinjenog prozora `ProTrail`.
- Šest testnih ciljeva više ne dodaje korijenski direktorij repozitorija na popis direktorija za uključivanje, pa korijenska datoteka `VERSION` više ne može zasjeniti C++ zaglavlje `<version>`.

## 0.1.0 - Priprema izdanja samo s izvornim kodom

- Odvojena korisnička sučelja početnog i naprednog uređivača zamijenjena su jednim ProTrailovim prozorom: General, Trail, Click te Developer samo u razvojnim izdanjima.
- Potpuna konfiguracija Trail/Sparkle i Click/Hold/Motion Wake zadržana je u karticama odgovarajućih područja s mogućnošću pomicanja, uz vidljive mreže gumba za odabir umjesto kombiniranih okvira za uobičajeni odabir stila, načina rada i ublažavanja.
- Kretanje kroz sistemsku traku objedinjeno je oko `Open ProTrail`; zatvaranje skriva isti prozor u sistemsku traku, automatsko pokretanje ostaje samo u sistemskoj traci, a interaktivna aktivacija druge instance vraća postojeći prozor.
- `Application::app_config` zadržan je kao kanonski izvor postavki, uz jednu logičku transakciju za Restore All Defaults i mogućnost postavljanja zadanih vrijednosti izdanja samo u razvojnim izdanjima, na temelju potpune spremljene konfiguracije.
- Ispravljeno: dovršen trajni zapis konfiguracije ostavio je postavljenu zastavicu odgođenog spremanja, pa bi isteklo odgađanje ponovno zapisati već spremljeno stanje.
- Dodani su ciljani regresijski testovi za objedinjeni prozor: identitet kartica, stanje selektora, tiho popunjavanje, jedno emitiranje signala po radnji, zatvaranje u sistemsku traku, odabir načina pokretanja, ponovnu uporabu sistemske trake i kanonski Restore Defaults.
- Dodani su Windows CI, smjernice za doprinos i sigurnost, predlošci za issue i pull request, pravila higijene repozitorija i izuzetke za generirane artefakte.
- Dokumentirano je ponašanje DPI-a na više monitora, životni ciklus sistemske trake, ponašanje automatskog pokretanja, spremanje konfiguracije, struktura repozitorija i upute za izgradnju iz izvornog koda.

Uz izdanje nije priložen službeni Windows izvršni program, instalacijski program, prijenosni paket ni druga binarna datoteka. Konačno binarno izdanje blokira
`FINAL_BINARY_RELEASE_BLOCKED: USER_PRODUCT_ICON_PENDING`.
