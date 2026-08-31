# Overstap naar de ESP32-S3-BOX-3

## 1. Doel

De klant vervangt de eenvoudige statusleds door een ESP32-S3-BOX-3. Dat bord heeft een
kleurenscherm, aanraakbediening, drie knoppen, een luidspreker en twee microfoons.

Het apparaat moet daarmee meer kunnen dan één kleur tonen:

1. Een opstartbeeld met het Energy Owl-logo.
2. De toestand van de energiegemeenschap in beeld.
3. Begeleiding tijdens de provisioning, op het apparaat zelf.
4. Knoppen op het scherm om tussen beelden te wisselen.
5. Later: tekst op het scherm, en geluid.

Dit document beschrijft de weg daarheen in fasen. Elke fase eindigt met een apparaat dat
werkt en dat je kunt aanzetten.

## 2. De hardware

| Onderdeel | ESP32-S3-BOX-3 | Huidig bord (XIAO ESP32-S3) |
| --- | --- | --- |
| Module | ESP32-S3-WROOM-1 | ESP32-S3 |
| Flash | 16 MB, quad | 8 MB, quad |
| PSRAM | **16 MB, octal** | 8 MB, niet gebruikt |
| Scherm | 2,4 inch, 320 × 240, SPI | geen |
| Schermdriver | ST7789 of ILI9341 | — |
| Aanraakscherm | GT911 (BOX-3) of TT21100 (oudere BOX) | geen |
| Knoppen | drie | één (reset via GPIO 17) |
| Geluid uit | ES8311 met luidspreker | geen |
| Geluid in | ES7210 met twee microfoons | geen |
| Sensoren | ICM42670 (beweging), AHT30 (temperatuur en vocht) | geen |

De ledringen en de losse statusleds verdwijnen. Het scherm neemt hun taak over.

## 3. Hoe we de code van Espressif gebruiken

Espressif levert twee dingen, en het verschil is belangrijk.

| Bron | Wat het is | Gebruiken wij het? |
| --- | --- | --- |
| `espressif/esp-box` | Een complete voorbeeldtoepassing met spraakherkenning | **Nee** — als voorbeeld, niet als basis |
| `espressif/esp-box-3` | Een BSP-component in het componentenregister | **Ja** — als afhankelijkheid |

De repository `esp-box` bouwt tegen ESP-IDF v5.1 en bevat een hele toepassing. Die
overnemen betekent dat wij hun code onderhouden.

De BSP is een component zoals `led_strip` en `cjson` die we al gebruiken. Die voegen we toe
met één regel in `main/idf_component.yml`. De component manager haalt hem op, en updates
zijn een versienummer.

De BSP versie 3.2.0 brengt deze afhankelijkheden mee:

| Afhankelijkheid | Versie | Waarvoor |
| --- | --- | --- |
| `esp_lvgl_port` | ^2 | LVGL koppelen aan het scherm |
| LVGL | 9.4 | Tekenen, knoppen, tekst |
| `esp_codec_dev` | ~1.5 | Luidspreker en microfoons |
| `button` | ^4 | De drie knoppen |
| `icm42670`, `aht30` | — | Sensoren |

## 4. Wat dit betekent voor het bestaande werk

| Onderdeel | Gevolg |
| --- | --- |
| `main/src/status_led.c` | Vervalt. Het scherm neemt de taak over |
| Pinnen in de README | Vervallen. De BOX-3 heeft een eigen indeling |
| Bevinding **H6** (led op GPIO 44) | Vervalt. Die pin bestaat niet meer als leduitgang |
| Bevinding **M7** ("ring uit" betekent twee dingen) | Verandert. Op een scherm is "geen gegevens" wél te tonen |
| `sdkconfig.defaults` | Flash naar 16 MB, PSRAM aanzetten, andere partitietabel |
| Hardwaretests in `docs/REVIEW.md` | Opnieuw doen. Ander bord is ander bewijs |
| Alles rond wifi, API, opslag en provisioning | Blijft ongewijzigd |

De negentien opgeloste bevindingen blijven geldig. Zij zitten in de netwerk- en
opslaglagen, en die raakt deze overstap niet.

## 5. De fasen

### Fase 0 — Bewijs dat de BSP bouwt op ESP-IDF v6.1 — **geslaagd**

De BSP eist ESP-IDF 5.3 of nieuwer. Dat is een ondergrens, geen garantie voor 6.x. ESP-IDF
v6 heeft componenten hernoemd en gesplitst. Deze fase was de poort naar alle andere.

**Uitgevoerd op 2026-08-29.** Een leeg proefproject dat alleen `bsp_i2c_init()`,
`bsp_display_start()` en `bsp_display_backlight_on()` aanroept, bouwt op ESP-IDF 6.1.0.
Zonder patches, zonder vastgepinde uitzonderingen, en zonder één compilerwaarschuwing in de
hele keten.

Wat de component manager oploste:

| Component | Versie |
| --- | --- |
| `espressif/esp-box-3` | 3.2.0 |
| `lvgl/lvgl` | 9.5.0 |
| `espressif/esp_lvgl_port`, `esp_codec_dev`, `button` | meegekomen als afhankelijkheid |
| `esp_lcd_ili9341`, `esp_lcd_touch_gt911`, `esp_lcd_touch_tt21100` | meegekomen als afhankelijkheid |
| `icm42670`, `aht30`, `sensor_hub`, `i2c_bus` | meegekomen als afhankelijkheid |

Het skelet met scherm en LVGL erin is 586 KB. Dat getal is de ondergrens voor wat de
OTA-slots straks moeten kunnen bevatten; de huidige firmware zonder scherm is 1,04 MB.

**Gevolg:** het besluit bij M9 om op v6.1 te bouwen blijft staan, en fase 1 kan beginnen.

### Voorstel voor de partitietabel

Het bestand `partitions-box3.csv` staat in de repository. Fase 1 koppelt het aan
`sdkconfig.defaults`. De tabel is nagerekend met `gen_esp32part.py` van ESP-IDF en vult de
16 MB flash precies, zonder een byte over.

| Partitie | Type | Offset | Grootte | Waarom |
| --- | --- | --- | --- | --- |
| *bootloader* | — | 0x000000 | tot 68 KB | Ruimte voor een bootloader mét secure boot |
| *partitietabel* | — | 0x011000 | 4 KB | Zie besluit 1 |
| `nvs` | data | 0x012000 | 24 KB | Wifi- en API-gegevens, ongewijzigd |
| `nvs_keys` | data | 0x018000 | 4 KB | Sleutels voor NVS-encryptie (H5) |
| `otadata` | data | 0x019000 | 8 KB | Welk app-slot mag starten |
| `phy_init` | data | 0x01B000 | 4 KB | Radiokalibratie |
| `coredump` | data | 0x01C000 | 80 KB | Zie besluit 2 |
| `ota_0` | app | 0x030000 | 3 MB | Eerste app-slot |
| `ota_1` | app | 0x330000 | 3 MB | Tweede app-slot voor OTA (H4) |
| `storage` | data | 0x630000 | 5,81 MB | Beelden, lettertypen |
| `model` | data | 0xC00000 | 4 MB | Zie besluit 3 |

#### Waarom 3 MB per app-slot

| Meting | Grootte |
| --- | --- |
| Huidige firmware, zonder scherm | 1,04 MB |
| Skelet met alleen BSP en LVGL | 0,59 MB |
| Verwacht met scherm, schatting | 1,5 tot 1,8 MB |

Drie megabyte geeft daarmee ruwweg het dubbele aan ruimte. Beelden en lettertypen horen in
`storage`, niet in de app: dan groeit de firmware er niet van, en kun je beelden vervangen
zonder een volledige OTA.

#### Besluit 1 — De partitietabel op 0x11000

ESP-IDF staat op de ESP32-S3 een bootloader van maximaal 64 KB toe, plus 4 KB handtekening.
Dat is precies 0x11000. De standaard van 0x8000 laat maar 32 KB over.

Onze bootloader is nu 21 KB. Secure boot en flash-encryptie maken hem groter. Past hij
straks niet, dan moet de tabel opschuiven — en dat kan niet meer op apparaten die al bij
mensen thuis staan.

**Voorstel:** nu op 0x11000 zetten. Het kost 68 KB flash en houdt H5 open.

#### Besluit 2 — Een coredump-partitie

Bij een crash schrijft ESP-IDF de toestand naar deze partitie. Bij de volgende start is die
uit te lezen, met een leesbare backtrace.

Zonder deze partitie is een crash bij een klant thuis alleen te onderzoeken als iemand er
met een USB-kabel bij kan en de fout zich herhaalt.

**Voorstel:** 80 KB reserveren. Dat is een half promille van de flash.

#### Besluit 3 — Vier megabyte reserveren voor spraakmodellen

De BOX-3 is gebouwd voor spraakbediening. De modellen van `esp-sr` staan in een eigen
partitie en zijn enkele megabytes groot.

Dit is de enige keuze in de tabel die volledig afhangt van wat de klant later wil:

| Variant | `storage` | `model` | Gevolg |
| --- | --- | --- | --- |
| **A — reserveren** | 5,81 MB | 4 MB | Spraakbediening blijft mogelijk |
| **B — niet reserveren** | 9,81 MB | — | Meer ruimte voor beelden; spraak vervalt definitief |

Reserveren kost weinig: `model` is een gewone spiffs-partitie. Gebruikt de klant nooit
spraak, dan kun je er alsnog beelden in zetten. Het is alleen een tweede
bestandssysteem in plaats van één groot.

**Voorstel:** variant A, tenzij je zeker weet dat spraak nooit komt.

### Fase 1 — Bord omzetten, nog zonder beeld

Doel: de huidige firmware draait op de BOX-3, met alles wat er nu al werkt.

1. Zet `sdkconfig.defaults` om: 16 MB flash, octal PSRAM aan, partitietabel op 0x11000.
2. Koppel `partitions-box3.csv` aan de build.
3. Vervang de ledlaag door een presentatie-interface met vier toestanden: overschot,
   tekort, in balans, geen gegevens. De eerste uitvoering schrijft alleen naar de log.
4. Werk de controlelijst in de CI bij op de nieuwe instellingen.

**Klaar als:** de BOX-3 opstart, provisioning doorloopt, telemetrie ophaalt, en de toestand
in de log meldt.

> **Uitgevoerd op 2026-08-31.** De firmware draait op de BOX-3. De 16 MB octal PSRAM wordt
> herkend en getest, de app start uit `ota_0` op 0x30000, en `status_view` meldt de toestand:
> `starting` bij het opstarten, `provisioning` zodra het portaal openstaat. De bootteller en de
> resetreden werken ongewijzigd.
>
> | Meting | Waarde |
> | --- | --- |
> | Bootloader | 21 KB van de 68 KB beschikbaar — ruimte genoeg voor secure boot |
> | Firmware | 1,07 MB van de 3 MB per slot — 66 procent vrij |
> | Vrije heap bij opstarten | 273 KB intern, plus 16 MB PSRAM |
>
> `status_led.c` en de `led_strip`-afhankelijkheid zijn verwijderd. De vier toestanden uit dit
> plan zijn er zes geworden: `starting` en `provisioning` zijn erbij gekomen, omdat fase 2 het
> opstartbeeld daaraan hangt en fase 4 het provisioningscherm.

> De partitietabel is een beslissing die je later niet meer kunt terugdraaien op
> uitgeleverde apparaten. Neem hem samen met bevinding **H4** (OTA) en **H5** (flash
> encryption). Dit is het moment.

### Fase 2 — Scherm aan met het opstartbeeld

1. Start het scherm en de achtergrondverlichting via de BSP.
2. Start LVGL via `esp_lvgl_port`.
3. Toon het Energy Owl-logo zolang het apparaat opstart.

Het beeld gaat als C-array mee in de firmware. Dat is de eenvoudigste weg voor één beeld.
Vanaf drie of vier beelden verhuist het naar de beeldpartitie uit fase 1.

**Klaar als:** het logo staat op het scherm binnen een seconde na inschakelen.

> **Uitgevoerd op 2026-08-31.** Het scherm komt op in 1,7 seconde: LVGL-taak gestart,
> ILI9341-paneel aangemaakt, beeld getekend, en daarna pas de achtergrondverlichting aan — in
> die volgorde, zodat niemand een verlicht leeg paneel ziet.
>
> Het beeld staat er nu nog als tijdelijke plaatsvervanger. De keten eromheen is wel de
> definitieve:
>
> | Stap | Bestand |
> | --- | --- |
> | Bronbeeld | `assets/energy-owl-bringup.png` |
> | Omzetten | `tools/png_to_lvgl.py` |
> | Ruwe RGB565 | `assets/energy-owl-bringup.bin` |
> | Inbouwen | `EMBED_FILES` in `main/CMakeLists.txt` |
>
> Het echte uilenbeeld vervangen is daarmee: de PNG overschrijven, het script draaien, bouwen.
> Er zit geen PNG-decoder in de firmware; het beeld staat er in het formaat waarin het scherm
> het tekent.
>
> De firmware groeide van 1,07 MB naar 1,61 MB. Daarvan is 150 KB het beeld zelf en de rest
> LVGL met de BSP. In het slot van 3 MB is nog 49 procent vrij, wat de keuze voor 3 MB
> bevestigt.
>
> **Nog te bevestigen:** of het beeld er ook goed uitziet. De log zegt dat het getekend is;
> alleen iemand die naar het scherm kijkt kan zeggen of dat klopt.

### Fase 3 — Toestand op het scherm

1. Vervang de loguitvoer uit fase 1 door beeld.
2. Geef elk van de vier toestanden een eigen beeld of kleur.

**Klaar als:** het scherm volgt de telemetrie, en "geen gegevens" is te onderscheiden van
"in balans". Daarmee is bevinding **M7** opgelost.

> **Uitgevoerd op 2026-08-31.** De tabel in `status_view.c` heeft er drie kolommen bij: een
> Nederlandse schermtekst, hoe de toestand geschilderd wordt, en de kleur. De logregels blijven
> Engels, net als de rest van de code; het scherm spreekt de taal van de bewoners.
>
> | Toestand | Scherm | Kleur |
> | --- | --- | --- |
> | `starting` | het opstartbeeld | — |
> | `provisioning` | "Instellen" | blauw |
> | `connecting` | "Verbinden" | grijsblauw |
> | `surplus` | "Energie over" | groen |
> | `deficit` | "Energie inkopen" | geel |
> | `balanced` | "In balans" | donkergroen |
> | `no-data` | "Geen gegevens" | grijs |
>
> De tekstkleur wordt niet in de tabel gekozen maar berekend uit de helderheid van de
> achtergrond. Zo kan niemand per ongeluk een combinatie kiezen die onleesbaar is.
>
> Twee paden zijn op hardware gezien: het beeld bij `starting` en het kleurscherm met tekst bij
> `provisioning`. De vier energietoestanden zijn pas te zien met een apparaat dat telemetrie
> ophaalt.
>
> Firmware 1,64 MB, nog 47 procent vrij in het slot.

### Fase 4 — Provisioning op het scherm

Vandaag ziet de gebruiker tijdens de provisioning niets op het apparaat zelf.

1. Toon de naam van het accesspoint en de voortgang.
2. Toon fouten die nu alleen in de weblog staan.
3. Overweeg een QR-code voor het accesspoint.

**Klaar als:** iemand het apparaat kan instellen zonder de handleiding.

### Fase 5 — Knoppen en aanraakbediening

1. Neem de `button`-component in gebruik voor de drie fysieke knoppen.
2. Koppel het aanraakscherm aan LVGL via de BSP.
3. Maak knoppen op het scherm om tussen beelden te wisselen.

**Klaar als:** een aanraking en een knopdruk allebei een beeldwissel geven.

### Fase 6 — Tekst op het scherm

1. Kies een lettertype met de tekens die het Nederlands nodig heeft.
2. Toon de meetwaarden: vermogen in kW en de prijzen.
3. Leg een indeling vast voor titel, waarde en eenheid.

**Klaar als:** de waarden uit de telemetrie leesbaar op het scherm staan.

### Fase 7 — Geluid

Pas oppakken als fase 1 tot en met 6 draaien. Geluid vraagt keuzes over wanneer een
apparaat in een woonkamer geluid màg maken.

## 6. Risico's

| Risico | Gevolg | Wat we eraan doen |
| --- | --- | --- |
| ~~BSP bouwt niet op ESP-IDF v6.1~~ | — | **Vervallen.** Fase 0 heeft dit uitgesloten |
| Partitietabel achteraf verkeerd | Niet te herstellen op uitgeleverde apparaten | Vaststellen in fase 1, samen met H4 en H5 |
| LVGL en PSRAM verbruiken geheugen | Minder ruimte voor TLS en de rest | Meten na fase 2, vóór fase 3 |
| Firmware wordt veel groter | OTA-slots moeten groot genoeg zijn | Meten in fase 2; de partitietabel volgt daaruit |
| Hardwarebewijs vervalt | Elf bevestigde bevindingen gelden niet meer | Testronde herhalen na fase 1 |

## 7. Volgorde van beslissen

1. ~~Fase 0~~ — afgerond, de ESP-IDF-versie is houdbaar.
2. **De partitietabel**, samen met OTA en flash encryption. Dit is nu het eerste
   openstaande besluit, en het enige dat achteraf niet te herstellen is.
3. Daarna de fasen 1 tot en met 7, in volgorde.
