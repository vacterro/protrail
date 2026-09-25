# ProTrail v0.1.8

## Zatvaranje izdanja

Nepromjenjive oznake `v0.1.5`, `v0.1.6` i `v0.1.7` zadržane su kao nepotpuni
zapisi izgradnje paketa. Ovo izdanje predaje potpuno izvorno zatvaranje
proizvoda koje zahtijeva kanonski CMake popis ciljeva: objedinjeni ProTrail
prozor, uklanjanje povučene implementacije `MainWindow`, T-59 popravke
izvršavanja, ispravljene putove uključivanja testova te ojačanu provjeru
dima pri postavljanju.

Preimenovanje izvora izdanja iz `VERSION` u `RELEASE_VERSION` iz v0.1.5 je
zadržano, pa korijenski direktorij repozitorija više ne može zasjeniti C++
zaglavlje `<version>` tijekom Windows izgradnje.

## Sadržaj

- `ProTrail-v0.1.8-win-x64-portable.zip`
- `SHA256SUMS.txt`

Paket je nepotpisan jer nije konfiguriran vjerodajstveni certifikat. Sadrži
`protrail.exe`, Qt 6.8 runtime i MSVC runtime lokalno uz aplikaciju.

## Brendiranje

`resources/branding/protrail.ico` jedini je izvor ikone proizvoda, s RGBA
unaprijed definiranim veličinama 16, 20, 24, 32, 40, 48, 64, 128 i 256 px.
Njezin točan SHA-256 zapis odobrenja nalazi se u
`resources/branding/APPROVAL.md`.

## Provjera

Službena objava zahtijeva čisto radno stablo s oznakom,
`PROTRAIL_REQUIRE_FINAL_ICON=ON`, Release izgradnju `/W4 /WX` bez upozorenja,
potpuni CTest PASS, PASS provjere dima paketa i podudarajuće SHA-256
vrijednosti.
