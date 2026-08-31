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

### Fase 0 — Bewijs dat de BSP bouwt op ESP-IDF v6.1

De BSP eist ESP-IDF 5.3 of nieuwer. Dat is een ondergrens, geen garantie voor 6.x. ESP-IDF
v6 heeft componenten hernoemd en gesplitst. **Deze fase is de poort naar alle andere.**

1. Bouw een leeg proefproject dat alleen `bsp_display_start()` aanroept.
2. Slaagt de build: ga door naar fase 1.
3. Faalt de build: kies tussen twee wegen, en leg de keuze vast in `docs/REVIEW.md` bij M9.
   - De BSP-versie verhogen of een patch afwachten.
   - Terug naar ESP-IDF v5.5, wat het besluit bij M9 terugdraait.

**Klaar als:** het proefproject bouwt, of de keuze uit stap 3 is gemaakt.

### Fase 1 — Bord omzetten, nog zonder beeld

Doel: de huidige firmware draait op de BOX-3, met alles wat er nu al werkt.

1. Zet `sdkconfig.defaults` om: 16 MB flash, octal PSRAM aan.
2. Maak een eigen partitietabel: twee OTA-slots plus een partitie voor beelden.
3. Vervang de ledlaag door een presentatie-interface met vier toestanden: overschot,
   tekort, in balans, geen gegevens. De eerste uitvoering schrijft alleen naar de log.
4. Werk de controlelijst in de CI bij op de nieuwe instellingen.

**Klaar als:** de BOX-3 opstart, provisioning doorloopt, telemetrie ophaalt, en de toestand
in de log meldt.

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

### Fase 3 — Toestand op het scherm

1. Vervang de loguitvoer uit fase 1 door beeld.
2. Geef elk van de vier toestanden een eigen beeld of kleur.

**Klaar als:** het scherm volgt de telemetrie, en "geen gegevens" is te onderscheiden van
"in balans". Daarmee is bevinding **M7** opgelost.

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
| BSP bouwt niet op ESP-IDF v6.1 | Alle fasen blokkeren | Fase 0 doet niets anders dan dit uitzoeken |
| Partitietabel achteraf verkeerd | Niet te herstellen op uitgeleverde apparaten | Vaststellen in fase 1, samen met H4 en H5 |
| LVGL en PSRAM verbruiken geheugen | Minder ruimte voor TLS en de rest | Meten na fase 2, vóór fase 3 |
| Firmware wordt veel groter | OTA-slots moeten groot genoeg zijn | Meten in fase 2; de partitietabel volgt daaruit |
| Hardwarebewijs vervalt | Elf bevestigde bevindingen gelden niet meer | Testronde herhalen na fase 1 |

## 7. Volgorde van beslissen

1. **Fase 0**, want die bepaalt of de ESP-IDF-versie houdbaar is.
2. **De partitietabel**, samen met OTA en flash encryption.
3. Daarna de fasen 2 tot en met 7, in volgorde.
