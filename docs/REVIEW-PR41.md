# Review van PR #41 — dagprijzen en wisselend scherm

**Bijdrage:** [PR #41](https://github.com/edwin-oetelaar/Shared_Energy_Telemetry_Device/pull/41),
`JobMeulenbeld:feature/Day_Ahead_Pricing`, 7 commits op `main` `11e5b5d`
**Auteur van de bijdrage:** JobMeulenbeld · **Review:** Edwin Oetelaar (met Claude), 2026-11-14 en 2026-11-15
**Uitkomst:** de bijdrage is bruikbaar. 10 commits erbovenop lossen de bevindingen op.

## Wat de bijdrage doet

| Onderdeel | Inhoud |
| --- | --- |
| Stroomprijzen | Een grafiekscherm met de prijzen per kwartier van EnergyZero, voor vandaag en morgen |
| Wisselen | Het scherm wisselt om de 15 s tussen de energietoestand en de prijzen |
| TLS in PSRAM | `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`, zodat twee HTTPS-verbindingen tegelijk passen |
| Fix 1 | Een regeleinde in het sleutelformulier van het portaal |
| Fix 2 | Het token blijft bewaard na een netwerkfout |

## Wat goed is

- Het model en de parser zijn zuiver C en host-getest, ook voor zomer- en wintertijd.
- De ophaaltaak heeft een eigen taak, een mutex en een kopie voor de lezer.
- Invoer van buiten wordt gecontroleerd: tijden, prijzen, grootte van het antwoord.
- De taak wacht op SNTP voordat hij een datum kiest.
- De NVS blijft onaangeroerd. Een terugval naar een oudere versie is daardoor veilig.
- Het scherm blijft uit de strook van de pijlen.

## Bevindingen

| ID | Bevinding | Uitkomst |
| --- | --- | --- |
| PR41-A1 | "Morgen" vult mogelijk nooit | **Geen fout.** Na 13:00 geeft één aanvraag drie dagen terug |
| PR41-A2 | TLS in PSRAM verandert het updatepad | **Bevestigd.** Zie de beproeving hieronder |
| PR41-A3 | Het prijsscherm vraagt stack van de statustaak | **Bevestigd.** 1,7 KB; 4,5 KB marge blijft over |
| PR41-B1 | Het scherm toont de kale prijs, zonder btw en toeslagen | **Open.** Productkeuze |
| PR41-B2 | De energietoestand staat de helft van de tijd in beeld | **Open.** Productkeuze |
| PR41-B3 | Vaktaal op het scherm: "Day-ahead", "CET/CEST", "Update mislukt" | **Open.** Productkeuze |
| PR41-C1 | De modi van het scherm zijn vier vlaggen en drie deadlines | **Opgelost.** Eén toestandsmachine |
| PR41-C2 | Welke toestanden wisselen, staat in een if-keten | **Opgelost.** Kolom `.cycles` in `s_view` |
| PR41-C3 | De ophaaltaak stuurt op vlaggen | **Opgelost.** Eén toestandsmachine |
| PR41-C4 | De nieuwe bestanden volgen de huisstijl niet | **Opgelost.** Twee commits: stijl en asserts |
| PR41-D1 | Het apparaat neemt stuurtekens in API-sleutels aan | **Opgelost.** `form_text_clean ()` |
| PR41-E1 | 48 aanvragen per dag, hertekenen elke 500 ms | **Opgelost.** 1 à 3 aanvragen per dag |
| PR41-F1 | Een geslaagde aanvraag logt niets | **Opgelost.** Eén regel per aanvraag |

### PR41-B1 — Het scherm toont de kale prijs

Het scherm toont de reeks `base`: de prijs zonder btw, energiebelasting en opslag. Op
2026-11-14 om 08:00 was dat 24,7 ct/kWh, terwijl de prijs met alles erbij 41,0 ct/kWh was. De
README en de handleiding zeggen dat. Het scherm zegt het niet, en een bewoner leest het scherm.
Het antwoord van de API bevat de reeks `all_in_with_vat` al, dus die tonen kost geen extra
aanvraag.

### PR41-B2 — De energietoestand staat de helft van de tijd in beeld

De energietoestand is de hoofdfunctie van het apparaat. Door het wisselen staat die nog 15 van
de 30 seconden in beeld.

### PR41-B3 — Vaktaal op het scherm

| Tekst | Probleem |
| --- | --- |
| "Day-ahead - ct/kWh" | Engelse vakterm |
| "Laagste 03:15 CET" | "CET" en "CEST" zeggen een bewoner niets |
| "Update mislukt" | Leest als een mislukte firmware-update; het gaat om het ophalen van de prijzen |

### Opgelost — wat er veranderd is

| ID | Verandering |
| --- | --- |
| PR41-C1, C2 | Vijf modi (`hold`, `live`, `prices`, `browsing`, `announcing`), één deadline, een volledige overgangstabel. Een aanraking tijdens een melding wordt genegeerd; MAIN beëindigt een melding |
| PR41-C3 | Toestanden `wait-wifi`, `wait-time`, `loading`, `ready`, `error`, met zes gebeurtenissen. De schermteksten komen uit één tabel per toestand |
| PR41-C4 | Huisstijl in een aparte commit. De machinecode is vóór en na bit voor bit gelijk. Daarna asserts voor programmeerfouten, en de constante `DAY_AHEAD_EARLIEST_TIME` |
| PR41-D1 | Witruimte aan de uiteinden weg, een regeleinde of stuurteken in het midden geweigerd. Eén tabel met antwoorden voor het portaal. 14 host-tests |
| PR41-E1 | Een schema met drie regels in het model, met host-tests. Middernacht is geen aanvraagmoment meer. Elk apparaat heeft een eigen spreiding van 0 tot 10 min. De grafiek wordt alleen opnieuw opgebouwd bij nieuwe gegevens |
| PR41-F1 | `Prices for … today 96/96, tomorrow 0/96 quarters`, en één keer de stackmarge |

## Beproefd op hardware

**Bord:** ESP32-S3-BOX-3 · **Datum:** 2026-11-14 en 2026-11-15

| Beproeving | Uitkomst |
| --- | --- |
| Grafiek, kloksync, prijzen ophalen | Werkt. Eerste aanvraag 2 s na de kloksync |
| Vanzelf wisselen om de 15 s | Werkt |
| Bladeren, loslaten, MAIN, Morgen en Vandaag | Werkt. Elke overgang staat in het log |
| Stack van de statustaak | 4512 bytes vrij met het prijsscherm, was 6208 |
| Stack van de ophaaltaak | 6888 van 12288 bytes vrij |
| Bijwerken met TLS in PSRAM | Werkt. Een kandidaat als `v0.3.3-review` haalde zelf `v0.3.4` op, in 23 s, terwijl Energyboxx tegelijk een aanvraag deed |
| De weg terug | Werkt. `v0.3.4` start op met de NVS van de kandidaat en wordt definitief |
| Netwerkfout bij Energyboxx | Werkt. Het token blijft bewaard; de ronde erna gaat door met `Token still valid` |
| Drie kapotte sleutelverzoeken naar het portaal | Werkt. Drie verschillende antwoorden; de API is niet aangeroepen |
| Schema | Werkt. Om 00:23 en 00:25: volgende aanvraag om 13:05 plus de spreiding |

## Wat niet is beproefd

- **Wifi valt weg.** De router kon niet uit. Het scherm "Offline (bewaard)" en het ophalen na
  het terugkomen van de wifi zijn dus niet op hardware gezien. De overgangen staan wel in de
  tabel van de ophaaltaak.
- **De regels "morgen is compleet" en "wachten op morgen"** van het schema. Die zijn alleen
  host-getest; op het bord treden ze pas na 13:00 op.
- **`cppcheck`** draait hier niet. De CI van de PR doet dat.
