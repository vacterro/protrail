# ProTrail v0.1.9

## Obnova kanonskog izvora izdanja

Korijenska datoteka `VERSION` ponovno je jedini izvor broja verzije izdanja;
vraćeno je preimenovanje u `RELEASE_VERSION` uvedeno u izdanju v0.1.5.

To preimenovanje bilo je zaobilazno rješenje za zasjenjivanje MSVC zaglavlja
`<version>`: na Windowsu koji ne razlikuje velika i mala slova korijenska
datoteka `VERSION` mogla bi se razriješiti umjesto standardnog C++ zaglavlja.
Izdanie v0.1.8 uklonilo je uzrok time što je uklonilo korijenski direktorij iz
putova uključivanja preostalih testnih ciljeva, pa čista izgradnja s
korijenskom datotekom `VERSION` sada daje izvršnu datoteku bez pogrešaka, bez
upozorenja i bez C2059.

Preimenovanje je imalo i drugu cijenu: kanonska putanja objave `saipen ship`
zahtijeva korijensku datoteku `VERSION` i odbijala je objavljivati dok je
nepostojala. Ovo je prvo izdanje objavljeno tom putanjom pa nosi predanu
potvrdu izdanja koja naziva objavljenu oznaku.

## Sadržaj

- `ProTrail-v0.1.9-win-x64-portable.zip`
- `SHA256SUMS.txt`

Paket je nepotpisan jer nije konfiguriran vjerodajstveni certifikat. Sadrži
`protrail.exe`, Qt 6.8 runtime i MSVC runtime lokalno uz aplikaciju.

## Nema promjene proizvoda

Ponašanje proizvoda u ovom izdanju nije se promijenilo. Prijenosni paket
odgovara v0.1.8 osim resursa VERSIONINFO koji sada navodi 0.1.9.

## Provjera

Službena objava zahtijeva čisto radno stablo s oznakom,
`PROTRAIL_REQUIRE_FINAL_ICON=ON`, Release izgradnju `/W4 /WX` bez upozorenja,
potpuni CTest PASS, PASS provjere dima paketa i podudarajuće SHA-256
vrijednosti.
