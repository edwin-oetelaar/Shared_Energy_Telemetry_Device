# Shared Energy Telemetry Device

Firmware voor een ESP32-S3 die toont hoeveel energie de energiegemeenschap op dit moment
over heeft. Het apparaat haalt de meetwaarden op bij de Energyboxx-API en toont de
uitkomst op een ledring.

Dit apparaat hoort bij de Energiegemeenschap Wilhelminaweg in Wageningen. Dat is een
groep huishoudens die overtollige zonne-energie deelt. Wat dat project wil bereiken en
hoe, staat in [docs/energiegemeenschap-wilhelminaweg.md](docs/energiegemeenschap-wilhelminaweg.md).

De gebruiker stelt de wifi- en API-gegevens in via een webportaal op het apparaat zelf.
De gegevens staan dus niet in de firmware.

## Wat het apparaat doet

Het apparaat vraagt elke minuut de telemetrie op. Het stuurt de ledring aan op het veld
`community_power_result_kw`.

| Uitkomst | Betekenis | Ledring |
| --- | --- | --- |
| Meer dan `+0,05 kW` | De gemeenschap heeft energie over om te delen | Groen |
| Minder dan `-0,05 kW` | De gemeenschap moet energie inkopen | Geel |
| Tussen `-0,05` en `+0,05 kW` | Vraag en aanbod zijn gelijk | Uit |
| Geen wifi of geen antwoord van de API | De gegevens zijn niet actueel | Uit |

Alle acht leds branden tegelijk in dezelfde kleur.

Het apparaat start een tweede ledring op, maar gebruikt die nog niet. Die ring is bedoeld
voor latere uitbreiding.

## Vaste termen

| Term | Betekenis |
| --- | --- |
| Provisioning | Het instellen van de wifi- en API-gegevens via het webportaal |
| Portaal | De webpagina's die het apparaat zelf aanbiedt tijdens provisioning |
| Ledring | Een ring van acht WS2812-leds |
| Status-led | Een losse led die één toestand toont: wifi, voeding of data |
| Telemetrie | De meetwaarden die het apparaat bij de Energyboxx-API ophaalt |
| Credentials | De opgeslagen wifi- en API-gegevens |

## Hardware

De huidige opzet gebruikt een ESP32-S3 en twee ledringen van acht pixels.

### Aansluitingen

| Functie | Pin op het board | GPIO |
| --- | --- | --- |
| Ongebruikte ledring | D0 | GPIO 1 |
| Ledring voor de energiestatus | D1 | GPIO 2 |
| Status-led wifi (blauw) | D7 / RX | GPIO 44 |
| Status-led voeding (rood) | D8 | GPIO 7 |
| Status-led data (blauw) | D9 | GPIO 8 |
| Knop om credentials te wissen | — | GPIO 17 |

De drie status-leds zijn actief-hoog. Plaats een geschikte voorschakelweerstand bij elke
externe led. De firmware begrenst de helderheid van de ledringen op 10 procent. Die
grens staat in `main/main.c`.

### Betekenis van de status-leds

| Led | Uit | Knippert | Brandt |
| --- | --- | --- | --- |
| Wifi | De eerste verbinding loopt nog | Provisioning is actief, of de verbinding is mislukt | Het apparaat heeft verbinding |
| Voeding | De firmware is nog niet gestart | — | De firmware draait |
| Data | — | Geen geldig token, of geen geslaagd antwoord | Token en telemetrie werken |

Een knipperende led is 500 ms aan en 500 ms uit.

## Provisioning

Het apparaat probeert bij het opstarten eerst de opgeslagen credentials. Het start een
eigen accesspoint zodra één van deze drie situaties zich voordoet:

- Er zijn geen wifigegevens opgeslagen.
- Het opgeslagen netwerk antwoordt niet binnen 30 seconden.
- De opgeslagen API-gegevens zijn ongeldig.

Het accesspoint heeft deze gegevens:

```text
SSID: SETD_Provisioning
Wachtwoord: geen
```

Verbind een telefoon of computer met dit netwerk. Het portaal opent daarna meestal
vanzelf. Open anders het gateway-adres van het accesspoint in een browser.

Het portaal vraagt twee dingen, in deze volgorde:

1. Kies een wifinetwerk en vul het wachtwoord in.
2. Vul de Energyboxx client ID en client secret in.

Het apparaat slaat gegevens pas op als het ze heeft beproefd. De wifigegevens gaan naar
NVS zodra de verbinding lukt. De API-gegevens gaan naar NVS zodra de tokenaanvraag lukt.

Na de provisioning stopt het portaal. Het apparaat schakelt daarna over naar wifi in
alleen station-modus.

## Credentials wissen

Er zijn twee manieren om de opgeslagen wifi- en Energyboxx-gegevens te wissen:

- Houd de knop op GPIO 17 tijdens het opstarten minstens drie seconden laag.
- Schakel de voeding drie keer binnen tien seconden uit en weer in.

De teller voor die tweede manier gaat na tien seconden draaien terug naar nul. Alleen een
echte in- en uitschakeling telt mee. Een herstart door een firmwarefout, een watchdog of
een onderspanning telt niet mee. Een softwarefout kan de credentials dus niet zelf wissen.

## Gedrag bij storingen

Het apparaat geeft nooit op. Het probeert het opnieuw volgens vaste schema's.

**Wifi weg.** Het apparaat blijft opnieuw verbinden, met een oplopende wachttijd:
0,5 s, 1 s, 2 s, 5 s, 10 s, 30 s, 60 s en daarna elke 5 minuten. De wifi-led knippert
zodra het schema bij de stap van tien seconden komt. Het schema is de tabel
`s_retry_schedule` in `main/src/wifi_provisioning.c`.

**API onbereikbaar.** Een mislukte token- of telemetrieaanvraag stopt de firmware niet.
Het apparaat wist de energiering en probeert het opnieuw. De wachttijd loopt op:
10 s, 20 s, 40 s, 80 s, 160 s en daarna elke 5 minuten. Het schema is de tabel
`s_api_retry_delay_ms` in `main/main.c`.

**Spreiding.** Elke wachttijd krijgt tot een vijfde extra. Dat geldt ook voor de gewone
tussentijd van één minuut. Zo vragen apparaten die tegelijk zijn opgestart niet tegelijk
opnieuw.

**Portaal blijft ongebruikt.** Het apparaat herstart na vijftien minuten stilte en
probeert de opgeslagen credentials opnieuw. Elke pagina en elke handeling in het portaal
zet die klok terug. De teller meet dus stilte, geen verstreken tijd.

## Bouwen

Dit project bouwt tegen **ESP-IDF v6.1**. Die versie staat vast: de CI bouwt tegen de
vaste containertag `espressif/idf:v6.1`.

Installeer ESP-IDF v6.1 volgens de handleiding van Espressif. Activeer de omgeving en
bouw:

```bash
. $IDF_PATH/export.sh
```

```bash
idf.py build
```

Het doelchip, de flashgrootte, de partitie-indeling en de stackgrootte van de hoofdtaak
komen uit `sdkconfig.defaults`. Dat bestand bevat alleen de instellingen die dit project
bewust kiest.

De gegenereerde `sdkconfig` staat niet in Git. Dat bestand herschrijft zichzelf bij elke
configure en zou die paar beslissingen verbergen tussen vierduizend regels.

Draai geen `idf.py set-target`. Dat commando gooit `sdkconfig.defaults` weg.

Het project haalt twee afhankelijkheden op met de ESP-IDF Component Manager:

- `espressif/led_strip`
- `espressif/cjson`

`sdkconfig.defaults` kiest een ESP32-S3 met 8 MB flash en een partitie-indeling met twee
OTA-slots.

Kies bewust wanneer je naar een nieuwere ESP-IDF gaat. Bouw en flash daarna een board, en
loop de tests uit [docs/REVIEW.md](docs/REVIEW.md) opnieuw na. Een nieuwe hoofdversie kan
onderliggende onderdelen vervangen: v6 gebruikt bijvoorbeeld picolibc waar v5 newlib
gebruikte.

## Tests en controles

De modules die geen ESP-IDF nodig hebben, draaien op de ontwikkelmachine:

```bash
make -C test check
```

Elke push start drie taken in GitHub Actions:

1. De host-tests hierboven.
2. Een `cppcheck`-controle over `main/`.
3. Een volledige firmwarebuild voor de ESP32-S3.

De opzet staat in `.github/workflows/ci.yml`.

## Flashen en meekijken

Sluit het board aan met de **native USB-poort** van de ESP32-S3. Op een XIAO
ESP32-S3 is dat de enige poort. Het board meldt zich dan als `/dev/ttyACM0`.

Met een gewone ESP-IDF-installatie gaat flashen zo:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Sluit de seriële monitor af met `Ctrl+]`.

### Terugvaloptie: de ESP-IDF van PlatformIO

Gebruik deze weg alleen als je geen eigen ESP-IDF hebt staan en snel iets wilt bouwen.

**Let op: PlatformIO levert ESP-IDF 5.5.x, niet de v6.1 waar dit project op mikt.** Een
build langs deze weg bewijst dus niet dat de firmware op de doelversie werkt.

PlatformIO gebruikt `idf.py` zelf niet. De Python-omgeving die PlatformIO meelevert, mist
daarom drie modules die `idf.py` nodig heeft: `esp_idf_monitor`, `pyyaml` en `esptool`. Er
is geen omgeving die je kunt activeren waarmee `idf.py` het toch doet.

`tools/idfenv.sh` stuurt in dat geval CMake en ninja rechtstreeks aan:

```bash
. tools/idfenv.sh
```

Configureer de build één keer:

```bash
cmake -S . -B build -G Ninja -DPYTHON="$IDF_PYTHON_ENV_PATH/bin/python" -DPYTHON_DEPS_CHECKED=1
```

Bouwen en flashen:

```bash
ninja -C build
```

```bash
ESPPORT=/dev/ttyACM0 ninja -C build flash
```

Meekijken met de logs:

```bash
python tools/monitor.py /dev/ttyACM0
```

`tools/monitor.py` toont dezelfde regels als `idf.py monitor`, met een
tijdstempel per regel. Het script vertaalt geen backtrace-adressen naar
functienamen. Installeer daarvoor ESP-IDF op de gewone manier.

Geef een aantal seconden mee om vanzelf te stoppen. Voeg `reset` toe om het
board eerst te herstarten:

```bash
python tools/monitor.py /dev/ttyACM0 30 reset
```

## Configuratie

De belangrijkste instellingen staan bovenin `main/main.c`:

| Instelling | Waarde | Doel |
| --- | --- | --- |
| `BRIGHTNESS_PERCENTAGE` | `10.0` | Helderheid van de ledringen |
| `POWER_BALANCE_DEADBAND_KW` | `0.05` | Grens waarbinnen de gemeenschap in balans is |
| `TELEMETRY_INTERVAL_MS` | `60000` | Tijd tussen twee telemetrieaanvragen |
| `WIFI_WAIT_POLL_MS` | `10000` | Hoe vaak het apparaat kijkt of wifi terug is |
| `RESET_HOLD_MS` | `3000` | Hoe lang de knop laag moet blijven |

Drie schema's staan in een tabel in plaats van in losse waarden. Zo is het hele beleid in
één oogopslag te zien:

| Tabel | Bestand | Bepaalt |
| --- | --- | --- |
| `s_api_retry_delay_ms` | `main/main.c` | De wachttijd na een mislukte API-ronde |
| `s_retry_schedule` | `main/src/wifi_provisioning.c` | De pogingen om wifi te herstellen |
| `s_reset_reason` | `main/main.c` | Welke herstarts meetellen voor het wissen |

`main/src/energyboxx_api.c` bevat drie andere instellingen: de URL's van de endpoints,
de marge voor het vernieuwen van het token en de grootte van de antwoordbuffer. De
stilte-timeout van het portaal staat als `PROVISIONING_SILENCE_TIMEOUT_MS` in
`main/src/wifi_provisioning.c`.

Git negeert `main/inc/secrets.h`. De huidige firmware gebruikt dat bestand niet. Zet
nooit echte credentials in de repository.

## Opbouw van het project

```text
main/
├── main.c                    Opstarten, herstel en de telemetrietaak
├── inc/                      Publieke headers
└── src/
    ├── api_storage.c         Energyboxx-gegevens in NVS
    ├── dns_server.c          DNS-omleiding voor het portaal
    ├── energyboxx_api.c      Tokenaanvraag en telemetrie
    ├── status_led.c          Ledringen en losse status-leds
    ├── uri_decode.c          Decoderen van formulierwaarden
    ├── wifi_provisioning.c   Wifitoestand en het accesspoint
    ├── wifi_storage.c        Wifigegevens in NVS
    └── wifi_web.c            Het webportaal

docs/REVIEW.md                Review vóór productie, met werklijst
docs/energiegemeenschap-wilhelminaweg.md   Het project waar dit apparaat bij hoort
test/                         Host-tests voor de modules zonder ESP-IDF
tools/idfenv.sh               Terugvaloptie: bouwen met de ESP-IDF van PlatformIO (5.5.x)
tools/monitor.py              Seriële monitor met tijdstempels
```

## Verloop na het opstarten

```text
Opstarten
  -> NVS en leds klaarzetten
  -> opgeslagen wifigegevens laden en verbinden
     -> provisioning starten als dat niet lukt
  -> API-gegevens laden en beproeven
     -> provisioning starten als dat niet lukt
  -> telemetrie opvragen
  -> de energiering bijwerken
  -> 60 seconden wachten en opnieuw beginnen
```
