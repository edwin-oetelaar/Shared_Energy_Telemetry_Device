# Firmware review — Shared Energy Telemetry Device

**Commit:** `df51625` · **Datum:** 2026-08-27 · **Reviewer:** Edwin Oetelaar (met Claude)
**Scope:** alle 2.698 regels C in `main/`, plus `sdkconfig`, `CMakeLists.txt` en `idf_component.yml`.
**Methode:** handmatige review van de broncode. Sinds 2026-08-28 is er wél een buildomgeving
(ESP-IDF 5.5.5 uit de PlatformIO-installatie); fixes worden daarmee gecompileerd, maar nog
steeds niet op hardware getest.

Dit document is de werklijst. We werken het stap voor stap af; vink af wat af is en laat de
bevinding staan als historie.

---

## Oordeel

Als prototype is dit goed werk: de module-indeling is logisch, HTTPS gebruikt de certificate
bundle, credentials worden pas opgeslagen nadat ze bewezen werken, en de README is eerlijk over
wat er wel en niet af is.

Als product dat bij mensen thuis aan het stopcontact hangt haalt het de streep nog niet. De vijf
kritieke punten zijn geen stijlkwesties: een wifi-wachtwoord met een spatie erin wordt verkeerd
opgeslagen, het apparaat komt na vijf mislukte reconnects nooit meer online zonder stekker eruit,
en tijdens provisioning gaan het wifi-wachtwoord én het API-secret onversleuteld door de lucht
over een open access point.

| Norm | Oordeel | Waarom |
| --- | --- | --- |
| Correctheid | **Zakt** | Wachtwoorden niet URL-gedecodeerd; format string met UB; datarace op tokenstate |
| Betrouwbaarheid in het veld | **Zakt** | Na 5 mislukte reconnects geen herstelpad; geen backoff, geen reboot |
| Security | **Zwak** | Open AP + plain HTTP voor secrets; plaintext NVS; flash encryption en secure boot uit |
| Onderhoudbaarheid | Matig | Nette moduleopdeling, maar HTML als C-string, 7× copy-paste JSON, dode code |
| Updatebaarheid | **Zakt** | Twee OTA-partities, geen regel OTA-code |
| Testbaarheid | **Zakt** | Geen tests, geen CI, geen static analysis |
| Reproduceerbaarheid | Matig | `sdkconfig` en `sdkconfig.old` ingecheckt i.p.v. `sdkconfig.defaults` |
| Documentatie | Goed | README beschrijft pinout, LED-semantiek en provisioning accuraat |

---

## Voortgang

### Kritiek — vóór het eerste apparaat de deur uit gaat
- [x] **C1** Wifi-wachtwoord wordt niet URL-gedecodeerd — opgelost, `main/src/uri_decode.c`
- [x] **C2** Na vijf mislukte reconnects is het apparaat permanent dood — opgelost, backoff-tabel
- [x] **C3** Twee taken doen tegelijk een tokenrequest, zonder lock — opgelost, module-lock
- [x] **C4** API-setup pagina gaat door een kapotte format string — opgelost, chunked
- [ ] **C5** Secrets gaan open door de lucht tijdens provisioning

### Hoog
- [ ] **H1** POST-body wordt in één keer gelezen in een te kleine buffer
- [ ] **H2** Panic-op-alles plus wis-na-3-boots is een gevaarlijke combinatie
- [x] **H3** Een webrequest kan het apparaat laten crashen — opgelost, foutcode i.p.v. abort
- [ ] **H4** Geen OTA, terwijl de partitietabel er twee slots voor heeft
- [ ] **H5** Credentials liggen leesbaar in flash

### Middel
- [ ] **M1** SSID's worden ongeëscaped in JSON geplakt
- [ ] **M2** `%d` met een `size_t`
- [ ] **M3** Foutdetectie via `strstr` op de ruwe body
- [ ] **M4** Vaste retry van 10 seconden, oneindig lang
- [ ] **M5** NVS-schrijfactie in de event handler
- [ ] **M6** `app_main` kan oneindig blijven wachten
- [ ] **M7** "Ring uit" betekent twee verschillende dingen
- [ ] **M8** Twee responses op één request in het API-check pad

### Klein
- [ ] **L1** Geen tests, geen CI, geen static analysis
- [ ] **L2** Compilerwaarschuwingen staan op de standaard
- [ ] **L3** `sdkconfig` én `sdkconfig.old` ingecheckt
- [ ] **L4** Geen LICENSE-bestand
- [ ] **L5** HTML/CSS/JS als C-stringliteral
- [ ] **L6** Zeven keer hetzelfde JSON-parseerblok
- [ ] **L7** Dode code
- [ ] **L8** Ongebruikte macro naast een hardcoded URL
- [x] **L9** Headers zijn niet zelfstandig — opgelost, was een harde buildbreker op IDF 5.5.5
- [x] **L10** `is_valid_credentials()` zonder `void` — opgelost
- [ ] **L11** Client secret in een `type=text`-veld; `data` niet `static`

---

## Kritiek

### C1 — Wifi-wachtwoord wordt niet URL-gedecodeerd
`main/src/wifi_web.c:220-227`, `main/src/wifi_web.c:300`

De browser stuurt de velden met `encodeURIComponent()`. Aan de ESP-kant haalt
`httpd_query_key_value()` de waarde uit de body maar doet géén percent-decoding — dat zit niet in
ESP-IDF, de voorbeelden leveren daar een eigen `uri_decode` voor. Een wachtwoord `mijn wifi 2024`
wordt dus opgeslagen als `mijn%20wifi%202024`.

Gevolg: iedereen met een spatie, `&`, `+`, `%` of een accent in zijn wachtwoord — dus een flink
deel van de klanten — krijgt het apparaat niet aan de praat, met als enige feedback
"Could not connect. Check password." Hetzelfde geldt voor het client secret in `/api-check`.

**Fix:** percent-decode (inclusief `+` → spatie) toepassen op ssid, password, client_id en
client_secret vóór gebruik. Kleine helper, één plek.

**Verificatie:** zet een spatie in het wifi-wachtwoord en kijk wat er in NVS terechtkomt.

> **Opgelost.** `main/src/uri_decode.c` + `main/inc/uri_decode.h` bevatten een
> percent-decoder als expliciete drie-statige state machine met een lookup-tabel voor de
> hexcijfers. `uri_decode()` assert op een NULL-pointer (contractschending van de eigen
> code) maar wijst onzin van de klant — een `%` zonder twee hexcijfers erachter — af met
> `false`, waarna de portal 400 antwoordt en het apparaat gewoon doordraait.
> Toegepast op ssid, password, client_id en client_secret in `main/src/wifi_web.c`.
> Op de host gecompileerd en getest met 17 gevallen (spaties, `+`, `%2B`, UTF-8, afgekapte
> escapes); niet op hardware getest.

### C2 — Na vijf mislukte reconnects is het apparaat permanent dood
`main/src/wifi_provisioning.c:62-73`

`s_retry_count` wordt alleen op nul gezet in `IP_EVENT_STA_GOT_IP` en in `wifi_prov_connect()`.
Zodra de teller op 5 staat gaat de state naar `CONNECT_FAILED` en roept niemand ooit nog
`esp_wifi_connect()` aan: geen timer, geen herstart, geen terugval naar het provisioning-AP.

Een router die vijf minuten opnieuw opstart, of een spanningsdip bij de buren, zet het apparaat
definitief stil. De `energyboxx_task` blijft daarna oneindig elke 10 seconden de ring wissen.
Alleen de stekker eruit trekken helpt — en dat drie keer doen wist de opgeslagen credentials
(zie H2).

**Fix:** retries met exponentiële backoff en zonder bovengrens, of een teller die na N
mislukkingen `esp_restart()` doet. Vijf pogingen en dan opgeven is voor firmware zonder
toetsenbord geen strategie.

> **Opgelost.** De teller met harde grens is vervangen door `s_retry_schedule`, een tabel in
> `main/src/wifi_provisioning.c` waarin elke rij zegt hoe lang er gewacht wordt en wat het
> apparaat ondertussen over zichzelf meldt: 0,5 s → 1 s → 2 s → 5 s → 10 s → 30 s → 60 s →
> 5 min, waarna de laatste rij zich herhaalt zolang het netwerk wegblijft. Het hele
> herstelbeleid staat daarmee in acht regels die je in één oogopslag overziet.
>
> Het wachten gebeurt met een `esp_timer`, niet met een blokkerende delay, zodat de
> event-loop vrij blijft. Vanaf de tiende seconde meldt het apparaat `CONNECT_FAILED` — de
> wifi-LED knippert dan — maar het blijft doorproberen. Bij een gelukte verbinding of nieuwe
> credentials springt het schema terug naar de eerste rij.
>
> Bijeffect om te weten: er kan nu een reconnect-poging lopen terwijl het provisioningportaal
> open staat. Dat is gewenst (het apparaat redt zichzelf als de router terugkomt) maar het
> maakt **H3** makkelijker te raken, want een gelijktijdige scan uit de portal kan
> `ESP_ERR_WIFI_STATE` opleveren en dat is daar nog een `ESP_ERROR_CHECK`. H3 is nu de
> logische volgende stap.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5, geen waarschuwingen. Niet op hardware getest.

### C3 — Twee taken doen tegelijk een tokenrequest, zonder lock
`main/src/wifi_provisioning.c:257-268` ↔ `main/src/wifi_web.c:320-332`

`wifi_prov_wait_until_completed()` pollt vanuit `app_main` elke seconde
`energyboxx_api_fetch_token()` zodra `has_credentials()` waar is. Op precies datzelfde moment
roept de httpd-handler `energyboxx_api_setup()` aan en dáárna óók `fetch_token()`.

Alle state is globaal en ongelockt: `client_id`, `client_secret`, `access_token`,
`valid_credentials`, `renew_token`. De pollende taak kan een `snprintf` van de post-body doen
terwijl de webtaak halverwege `strncpy` in `client_secret` zit — half oud, half nieuw secret.
Uitkomst: "Invalid Client ID or Client Secret" terwijl de gebruiker de juiste sleutels intypte,
plus twee gelijktijdige requests naar de tokenendpoint.

**Fix:** één mutex om de credential- en tokenstate. Beter nog: de provisioning-lus laten wachten
op een event dat de webhandler zet, in plaats van te pollen op state die die handler schrijft.

> **Opgelost, in twee delen.**
>
> 1. `energyboxx_api.c` heeft nu één statisch aangemaakte mutex over de credentials, het token
>    en de bijbehorende vlaggen. `fetch_token` en `get_data` zijn interne `_locked`-functies
>    geworden met een publieke wrapper eromheen, zodat geen enkel vroegtijdig `return` de lock
>    kan laten staan. `energyboxx_api_init()` maakt hem aan en wordt als eerste in `app_main`
>    aangeroepen; de andere functies asserten dat dat gebeurd is.
>    `is_valid_credentials()` en `has_credentials()` lezen bewust zonder lock — de status-LED
>    task vraagt ze twee keer per seconde en mag niet tien seconden achter een HTTP-request
>    blijven hangen. Die twee vlaggen zijn daarom `volatile bool`.
> 2. De poll-lus in `wifi_prov_wait_until_completed()` haalde elke seconde een token op. Dat
>    was niet alleen de helft van de race, het beukte ook eens per seconde met verlopen
>    credentials op de tokenendpoint. Die retry staat er nog — hij redt het geval waarin de
>    opgeslagen sleutels goed zijn maar de API bij het opstarten onbereikbaar was — maar nu op
>    een tick van 30 seconden, en via dezelfde lock als de portal.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest.

### C4 — De API-setup pagina wordt door een kapotte format string gehaald
`main/src/wifi_web.c:129` + `main/src/wifi_web.c:175`

De HTML in `api_setup_get_handler()` gaat als *format string* door
`snprintf(page, sizeof(page), html, ...)`. Die string bevat CSS:

```c
"input,button{width:100%;box-sizing:border-box; ..."
```

`%;` is geen geldige conversie. Dit is undefined behaviour, en het staat vóór de twee bedoelde
`%s`. Dat het er nu goed uitziet is een eigenschap van newlib, geen garantie: een IDF-upgrade of
andere printf-implementatie kan de pagina stukmaken of de argumenten laten verschuiven.

**Fix:** splits de pagina in een statisch deel en de twee dynamische stukjes
(`httpd_resp_send_chunk`), of escape als `100%%`. Beter nog: HTML uit de C-code halen (L5).

> **Opgelost.** De pagina is opgesplitst in drie vaste stukken met de twee variabele waarden
> ertussen, en gaat er als array doorheen met `httpd_resp_send_chunk()`. Er komt geen
> printf meer aan te pas, dus `width:100%` is weer gewoon tekst. De buffer van 4 KB op de
> httpd-stack is daarmee ook verdwenen.
>
> Eén valkuil die het commentaar in de code benoemt: een chunk van lengte nul is in chunked
> encoding het einde-signaal, dus het lege stuk (de niet-gezette `disabled`-attribuut) wordt
> overgeslagen in plaats van verstuurd.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest — de zichtbare verificatie is
> de API-setup pagina openen en kijken of hij compleet is, met een werkende knop in beide
> toestanden.

### C5 — Secrets gaan open door de lucht tijdens provisioning
`main/src/wifi_provisioning.c:24-25`, `main/src/wifi_provisioning.c:171`, hele portal in `wifi_web.c`

Het provisioning-AP is `WIFI_AUTH_OPEN` zonder wachtwoord, en de portal draait op plain HTTP.
Het wifi-wachtwoord van de klant én het Energyboxx client secret worden dus onversleuteld
verzonden en zijn passief af te luisteren door iedereen binnen radiobereik.

Er zit ook geen enkele authenticatie op de endpoints. Een buitenstaander die op het open AP
inlogt kan `/scan` draaien, via `POST /connect` het apparaat aan zíjn access point koppelen, of
via `/api-check` eigen credentials laten opslaan.

**Fix:** WPA2 op het provisioning-AP met een per-apparaat wachtwoord op de behuizing of een
QR-sticker. Dat lost afluisteren én ongeautoriseerde toegang in één keer op. ESP-IDF's eigen
`wifi_provisioning`-component doet dit met een versleuteld kanaal (X25519 + AES) — het overwegen
waard.

---

## Hoog

### H1 — POST-body wordt in één keer gelezen in een te kleine buffer
`main/src/wifi_web.c:207-215`, `main/src/wifi_web.c:290-298`

Twee problemen in dezelfde vier regels. Ten eerste is `char body[160]` te klein: een SSID van 32
tekens plus een wachtwoord van 63, beide percent-encoded, plus de veldnamen komt ruim boven de
160 uit. Ten tweede mag `httpd_req_recv()` minder teruggeven dan er komt; er wordt niet
doorgelezen tot `req->content_len`. In beide gevallen wordt de body stil afgekapt en krijgt de
gebruiker een onverklaarbare verbindingsfout.

**Fix:** buffer op `req->content_len` baseren (met bovengrens) en in een lus lezen tot alles
binnen is.

### H2 — Panic-op-alles plus wis-na-3-boots is een gevaarlijke combinatie
`main/main.c` (34× `ESP_ERROR_CHECK`), `main/main.c:206-214`

`ESP_ERROR_CHECK` is een abort: elke onverwachte returncode herstart het apparaat. Dat staat in
`main.c` 34 keer, ook om dingen die prima te overleven zijn — een NVS-schrijffout, het wissen van
een LED-ring, een mislukte statuspin.

Tegelijk wist de bootteller de credentials na drie herstarts binnen tien seconden. Een
firmwarebug die vroeg in `app_main` paniekt veroorzaakt dus zelf de drie herstarts die de
configuratie van de klant wissen. Het apparaat "vergeet" zichzelf door een bug, en de klant moet
opnieuw provisionen.

**Fix:** `ESP_ERROR_CHECK` reserveren voor echt onherstelbare init-fouten; de rest loggen en
doorgaan. En de bootteller pas ophogen ná een minimale uptime, of alleen tellen bij een schone
power-on reset (`esp_reset_reason()`), niet bij een panic-reset.

### H3 — Een webrequest kan het apparaat laten crashen
`main/src/wifi_provisioning.c:233`

`wifi_prov_scan()` doet `ESP_ERROR_CHECK(esp_wifi_scan_start(...))`. Twee browsers die tegelijk
"Refresh networks" indrukken, of één die dubbelklikt terwijl de vorige scan nog loopt, levert
`ESP_ERR_WIFI_STATE` op — en dus een panic-reboot, midden in het provisioningproces. Input van
buiten mag nooit tot een abort leiden.

**Fix:** de returncode teruggeven aan de handler, die netjes 503 doet. De handler heeft de
foutafhandeling al klaarstaan.

> **Opgelost.** `wifi_prov_scan()` geeft de fout van `esp_wifi_scan_start()` nu terug in plaats
> van te aborten, en assert alleen nog op zijn eigen parameters. De handler vertaalt de
> foutcode via een tabel `s_scan_error` naar een HTTP-status: `ESP_ERR_WIFI_STATE` en
> `ESP_ERR_WIFI_NOT_STARTED` worden 503 met "Scan busy, try again", een timeout wordt 504, en
> de laatste rij vangt al het overige af als 500 — daardoor kan die tabel niet onvolledig zijn.
>
> Dit was extra dringend geworden door C2: de reconnect-timer kan nu een verbindingspoging doen
> terwijl iemand in de portal op "Refresh networks" drukt, en dat is precies het geval dat
> `ESP_ERR_WIFI_STATE` oplevert.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest.

### H4 — Geen OTA, terwijl de partitietabel er twee slots voor heeft
`sdkconfig` (`CONFIG_PARTITION_TABLE_TWO_OTA_LARGE=y`), geen OTA-code in `main/`

De flashindeling is al ingericht op over-the-air updates, maar er staat geen regel updatecode in
het project. Elke fix hierboven vereist dus fysiek langs elk apparaat met een USB-kabel. Voor
apparaten die bij mensen thuis staan is dat het verschil tussen een bug en een terugroepactie.

**Fix:** `esp_https_ota` met versiecheck en rollback (`app_rollback`) toevoegen zolang de vloot
nog klein is.

### H5 — Credentials liggen leesbaar in flash
`sdkconfig`: geen `SECURE_FLASH_ENC_ENABLED`, geen `NVS_ENCRYPTION`, geen `SECURE_BOOT`

Wifi-wachtwoord en Energyboxx client secret staan als plain string in NVS. Wie het apparaat vijf
minuten in handen heeft leest ze met `esptool read_flash` uit. De ESP32-S3 ondersteunt flash
encryption én secure boot v2 — de SOC-vlaggen staan aan in `sdkconfig`, de features zelf niet.

**Fix:** voor productie flash encryption in release mode + NVS encryption, en secure boot v2 als
je de firmware wilt afschermen. **Beslis dit vóór de eerste serie** — achteraf inschakelen op
reeds uitgeleverde apparaten kan niet meer.

---

## Middel

### M1 — SSID's worden ongeëscaped in JSON geplakt
`main/src/wifi_web.c:252-262`

De netwerklijst wordt met de hand als JSON aan elkaar geplakt. Een buurman die zijn access point
`Net","rssi":0},{"ssid":"X` noemt breekt de structuur; een SSID met een aanhalingsteken of
backslash maakt `JSON.parse` stuk en de netwerklijst blijft leeg. De `item[128]`-buffer kan
bovendien afkappen midden in een string. Geen XSS (de pagina gebruikt `textContent`), wel een
setup die het niet doet door toedoen van een derde.

**Fix:** cJSON is al een dependency — gebruik `cJSON_CreateArray()` in plaats van `snprintf`.

### M2 — `%d` met een `size_t`
`main/src/energyboxx_api.c:164`

`ESP_LOGI(TAG, "Stored token (%d chars)", strlen(access_token));` — formaatmismatch, undefined
behaviour. Zou met `-Wformat` moeten opvallen; dat het dat niet doet zegt iets over de
warning-instellingen (L2).

**Fix:** `%zu`.

### M3 — Foutdetectie via `strstr` op de ruwe body
`main/src/energyboxx_api.c:251`

`strstr(response_buffer, "\"AUTH-1000\"")` zoekt een foutcode in ongeparseerde tekst. Breekt
zodra Energyboxx zijn foutformaat aanpast, en kan vals-positief zijn als die string ergens in
legitieme data opduikt. De statuscodes 401/403 ernaast zijn de betrouwbare check.

**Fix:** de body parsen en het foutveld gericht uitlezen, of alleen op statuscode vertrouwen.

### M4 — Vaste retry van 10 seconden, oneindig lang
`main/main.c:26`, `main/main.c:117`, `main/main.c:128`, `main/main.c:139`

Bij een API-storing blijft elk apparaat eeuwig elke 10 seconden aankloppen. Met een paar honderd
apparaten is dat 6× per minuut per apparaat tegen een endpoint dat al in de problemen zit.

**Fix:** exponentiële backoff 10s → 20s → 40s → ... tot 5 min, plus willekeurige jitter zodat de
vloot niet synchroon loopt.

### M5 — NVS-schrijfactie in de event handler
`main/src/wifi_provisioning.c:99`

`wifi_storage_save_credentials()` wordt aangeroepen vanuit `IP_EVENT_STA_GOT_IP`, dus in de
event-loop-taak, bij élke (her)verbinding. Flashoperaties horen daar niet: de event loop
blokkeert zolang de schrijfactie duurt. Functioneel valt het mee doordat NVS ongewijzigde
waardes overslaat, maar het is de verkeerde plek — en de returncode wordt genegeerd.

**Fix:** alleen opslaan op het pad waar nieuwe credentials binnenkomen, vanuit de taak die ze
ontving.

### M6 — `app_main` kan oneindig blijven wachten
`main/src/wifi_provisioning.c:262-268`

De `while (energyboxx_api_is_valid_credentials() == false)`-lus heeft geen uitweg. Als niemand
het portaal opent blijft het apparaat daar staan — geen timeout, geen herstart, geen "geef het op
en probeer de opgeslagen wifi opnieuw".

**Fix:** een provisioning-timeout (bv. 15 minuten) met daarna een herstart.

### M7 — "Ring uit" betekent twee verschillende dingen
`main/main.c:116`, `:126`, `:137` versus `main/main.c:159`

De ring gaat uit bij "gemeenschap in balans" én bij "geen wifi / API onbereikbaar". De README
erkent dit. De losse data-LED knippert dan wel, maar de ring — het ding waar mensen naar kijken —
toont "alles in balans" terwijl er in werkelijkheid geen data is.

**Fix:** geef "geen data" een eigen taal op de ring: een langzame ademende puls, of één gedimde
pixel. Uit hoort "ik weet het en het is niets" te betekenen, niet "ik weet het niet".

### M8 — Twee responses op één request in het API-check pad
`main/src/wifi_web.c:284-307` en `main/src/wifi_web.c:309-318`

`parse_api_credentials()` stuurt bij een fout zelf al een response met
`httpd_resp_send_err()` en geeft daarna `ESP_FAIL` terug. De aanroeper
`api_check_post_handler()` stuurt vervolgens nóg een response, het JSON-object
`{"ok":false,"message":"Invalid request"}`. Er gaan dus twee volledige antwoorden over
dezelfde verbinding.

Gevonden tijdens het werk aan C1. Het nieuwe decodeerpad omzeilt dit door de controle in de
aanroeper te doen, waar het antwoord thuishoort; de twee bestaande paden doen het nog wel.

**Fix:** `parse_api_credentials()` alleen laten valideren en een foutcode teruggeven; het
antwoord aan de aanroeper laten, die dat toch al doet.

---

## Klein

Geen van deze breekt iets, samen bepalen ze wel hoe het project over een jaar aanvoelt.

- **L1 — Geen tests, geen CI, geen static analysis.** Minstens een GitHub Action die
  `idf.py build` draait, plus `clang-tidy` of `cppcheck`. Bij deze omvang een middag werk.
- **L2 — Compilerwaarschuwingen staan op de standaard.** Geen `CONFIG_COMPILER_WARN_*` in
  `sdkconfig`. `-Wall -Wextra` had M2 en waarschijnlijk C4 gevonden.
- **L3 — `sdkconfig` én `sdkconfig.old` zijn ingecheckt.** De conventie is `sdkconfig.defaults`
  committen en `sdkconfig` negeren; `sdkconfig.old` (3.000 regels ruis, enige verschil:
  flashsize 2MB↔8MB) hoort er sowieso niet in.
- **L4 — Geen LICENSE-bestand.** `main/src/dns_server.c` is keurig overgenomen mét Espressif's
  CC0-header, maar het project zelf heeft geen licentie — juridisch "alle rechten voorbehouden".
- **L5 — HTML, CSS en JavaScript als C-stringliteral.** Onhandig te bewerken, geen syntax
  highlighting, en de directe oorzaak van C4. ESP-IDF's `EMBED_FILES` lost dit netjes op.
- **L6 — Zeven keer hetzelfde JSON-parseerblok.** `main/src/energyboxx_api.c:281-348` — een tabel
  van `{naam, offset}` plus één lus doet hetzelfde in tien regels.
- **L7 — Dode code.** `led_ring_set_fill()`, `led_ring_start_loop_async()`,
  `energyboxx_api_get_token()` en `led_ring_1` worden nergens gebruikt. Prima als het bewuste
  voorbereiding is, maar zeg dat dan in een comment.
- **L8 — Ongebruikte macro naast een hardcoded URL.** `ENERGYBOXX_TOKEN_URL`
  (`main/src/energyboxx_api.c:15`) is gedefinieerd maar de config gebruikt dezelfde URL letterlijk
  op regel 100. Twee plekken om te vergeten.
- **L9 — Headers zijn niet zelfstandig.** `main/inc/api_storage.h` gebruikt `size_t` zonder
  `<stddef.h>`, `main/src/api_storage.c` gebruikt `strlen` zonder `<string.h>`, en
  `main/inc/energyboxx_api.h` gebruikt `bool` zonder `<stdbool.h>`.
  **Opgelost — en dit was geen theoretisch punt:** op ESP-IDF 5.5.5 levert het meteen
  `error: unknown type name 'bool'` op, waardoor het project daar helemaal niet bouwde. De
  transitieve include waar het op leunde zit in nieuwere IDF-versies niet meer in `esp_err.h`.
- **L10 — `bool energyboxx_api_is_valid_credentials();`** zonder `void` is geen prototype; de
  compiler controleert de argumenten niet. **Opgelost** — kwam samen met L9 naar boven als
  `conflicting types ... have '_Bool()'`, omdat de compiler zonder `bool` terugviel op `int`.
- **L11 — Client secret in een `type=text`-veld** (`main/src/wifi_web.c:143`), terwijl het
  wifi-wachtwoord wél op `password` staat. En `energyboxx_data_t data` in `main/main.c:19` is niet
  `static`.

---

## Wat er goed is

Niet als beleefdheid — dit zijn de dingen die niet veranderd moeten worden.

- **HTTPS met de certificate bundle.** `esp_crt_bundle_attach` op beide calls, met timeout. Geen
  `skip_cert_common_name_check`, geen uitgezette verificatie.
- **Credentials worden pas opgeslagen als ze bewezen werken.** Wifi pas na `GOT_IP`, API pas na
  een geslaagde tokenrequest. Dat is de juiste volgorde.
- **Geen secrets in de repo of in de history.** `main/inc/secrets.h` staat in `.gitignore` en is
  er ook nooit in beland — alleen de macronamen komen in oude commits voor.
- **Duidelijke moduleopdeling.** storage / provisioning / web / api / led zijn echt gescheiden,
  met headerguards en `static`-interne functies.
- **Herkende herkomst.** `dns_server.c` is overgenomen mét de originele SPDX-header.
- **Een eerlijke README.** Pinout, LED-tabel, resetprocedure en zelfs de bewust ongebruikte
  tweede ring staan erin.

---

## Voorgestelde volgorde

1. **Halve dag — de twee showstoppers.** C1 (URL-decoding) en C2 (onbegrensde reconnect met
   backoff). Hierna doet het apparaat het bij klanten met een spatie in hun wachtwoord, en
   overleeft het een routerherstart.
2. **Halve dag — crashes en races.** H2 (`ESP_ERROR_CHECK`-opruiming), H3 (scan-crash),
   C3 (tokenrace), C4 (format string).
3. **Eén dag — provisioning dichttimmeren.** C5 (WPA2 met per-apparaat wachtwoord) en H1
   (body-lezen).
4. **Beslismoment vóór de eerste serie.** H4 en H5 (OTA, flash encryption, secure boot). De enige
   punten die achteraf niet meer recht te zetten zijn op uitgeleverde apparaten.
5. **Daarna de rest.** M4, M7, M1, L1 en de opruimpunten — die kunnen mee met een OTA-update,
   zodra die er is.

---

## Wat niet is gecontroleerd

Er stond geen ESP-IDF op de reviewmachine, dus de code is **niet gecompileerd en niet op hardware
getest**. Alle bevindingen komen uit het lezen van de bron. Twee verdienen een expliciete
kanttekening:

- **C4** is undefined behaviour volgens de standaard, maar newlib gedraagt zich in de praktijk
  mild — mogelijk ziet de pagina er vandaag gewoon goed uit. Het punt blijft staan: het is geen
  gedrag waar je op mag bouwen.
- **C1** is in dertig seconden te bevestigen: zet een spatie in het wifi-wachtwoord en kijk wat
  er in NVS terechtkomt.
