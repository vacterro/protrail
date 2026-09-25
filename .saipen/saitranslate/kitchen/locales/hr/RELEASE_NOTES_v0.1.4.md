# ProTrail v0.1.4

## Oporavak izdanja

`v0.1.3` je nepromjenjiv, ali nepotpun zapis izdanja: njegova oznaka pokazuje na zatvarajući commit prije ugradnje ikone. Ova zakrpa ne pomiče niti briše tu oznaku. Stvarno izdanje s simbolom je `v0.1.4`.

## Sadržaj

- `ProTrail-v0.1.4-win-x64-portable.zip`
- `SHA256SUMS.txt`

Paket sadrži `protrail.exe`, Qt 6.8 runtime i lokalno MSVC runtime. Nije potpisan jer certifikat za potpis nije konfiguriran.

## Branding

`resources/branding/protrail.ico` jedini je izvor simbola proizvoda. ICO sadrži RGBA stavke veličina 16, 20, 24, 32, 40, 48, 64, 128 i 256 px. Točan SHA-256 nalazi se u `resources/branding/APPROVAL.md`, a glavni izvor umjetničkog materijala čuva se u `protrail-master.svg` i `protrail-master.png`.

## Provjera

Objava zahtijeva čisto označeno stablo, `PROTRAIL_REQUIRE_FINAL_ICON=ON`, Release build bez upozorenja, potpuni CTest PASS, package smoke PASS i podudarne SHA-256 vrijednosti. Binarni se artefakt ne objavljuje ako bilo koja vrata padnu.
