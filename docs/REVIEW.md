# Firmware review — Shared Energy Telemetry Device

**Uitgangspunt:** commit `df51625` · **Review gestart:** 2026-08-27 · **Bijgewerkt:** 2026-08-28
**Reviewer:** Edwin Oetelaar (met Claude) · **Auteur van de firmware:** JobMeulenbeld
**Scope:** alle 2.698 regels C in `main/`, plus de buildconfiguratie.
**Status:** dit is een review vóór productie; er is nog niets uitgeleverd.

**Methode:** handmatige review van de broncode, daarna fixes op de branch
`hardening/review-findings`. Elke fix is gecompileerd voor de ESP32-S3 met ESP-IDF 5.5.5, en
sinds L1 draait er CI die dat bij elke push herhaalt, samen met `cppcheck` en de host-tests.
**Nog steeds niet op hardware getest** — zie "Wat niet is gecontroleerd" onderaan.

Dit document is de werklijst. We werken het stap voor stap af; opgeloste bevindingen blijven
staan als historie, met een blokcitaat waarin staat wat er precies is gebeurd.

---

## Oordeel

**Bij aanvang:** als prototype goed werk — logische module-indeling, HTTPS met de certificate
bundle, credentials pas opgeslagen nadat ze bewezen werken, en een eerlijke README. Als product
haalde het de streep niet: een wifi-wachtwoord met een spatie erin werd verkeerd opgeslagen, het
apparaat kwam na vijf mislukte reconnects nooit meer online zonder stekker eruit, en een
firmwarebug kon zelf de configuratie van de klant wissen.

**Nu:** 27 van de 39 bevindingen zijn opgelost. Van het kritieke blok resteert alleen C5, en van
de hoge punten alleen H5. Wat er nu ligt is firmware die een routerherstart overleeft, die geen
enkele reboot meer laat uitlokken door een webrequest, die zichzelf bijwerkt en bij een mislukte
update terugvalt op de vorige versie, en waarvan elke wijziging automatisch wordt gebouwd,
geanalyseerd en getest.

**Wat de doorslag geeft voor productie:** de twee openstaande beslissingen (C5, H5). Beide raken
het productieproces en niet alleen de firmware, en beide zijn na uitlevering niet meer goedkoop
te repareren.

| Norm | Bij aanvang | Nu | Toelichting |
| --- | --- | --- | --- |
| Correctheid | **Zakt** | Goed | C1–C4 opgelost: decoding, format string, datarace, body-lezen |
| Betrouwbaarheid in het veld | **Zakt** | Goed | Backoff zonder bovengrens, geen aborts meer op externe input |
| Security | **Zwak** | Zwak | C5, H5 open: open AP + plain HTTP, plaintext NVS, geen flash encryption |
| Onderhoudbaarheid | Matig | Matig | L5–L8 open: HTML als C-string, copy-paste JSON, dode code |
| Updatebaarheid | **Zakt** | Goed | H4 opgelost: OTA vanaf GitHub Releases, met versiecheck en terugval |
| Testbaarheid | **Zakt** | Redelijk | CI met host-tests, cppcheck en firmwarebuild; nog geen hardwaretest |
| Reproduceerbaarheid | Matig | Redelijk | `sdkconfig.defaults` op orde; M9 open: IDF-versie niet gepind |
| Documentatie | Goed | Goed | README loopt mee met elke gedragswijziging |

---

## Voortgang

### Kritiek — vóór het eerste apparaat de deur uit gaat
- [x] **C1** Wifi-wachtwoord wordt niet URL-gedecodeerd — opgelost, `main/src/uri_decode.c`
- [x] **C2** Na vijf mislukte reconnects is het apparaat permanent dood — opgelost, backoff-tabel
- [x] **C3** Twee taken doen tegelijk een tokenrequest, zonder lock — opgelost, module-lock
- [x] **C4** API-setup pagina gaat door een kapotte format string — opgelost, chunked
- [ ] **C5** Secrets gaan open door de lucht tijdens provisioning

### Hoog
- [x] **H1** POST-body wordt in één keer gelezen in een te kleine buffer — opgelost
- [x] **H2** Panic-op-alles plus wis-na-3-boots is een gevaarlijke combinatie — opgelost
- [x] **H3** Een webrequest kan het apparaat laten crashen — opgelost, foutcode i.p.v. abort
- [~] **H6** De wifi-status-led zit op GPIO 44, dat is U0RXD — vervallen met de bordwissel
- [x] **H7** `energyboxx_api_setup()` laat het oude token staan — opgelost
- [x] **H8** De DNS-server lekt zijn socket bij het stoppen — opgelost
- [x] **H9** `wifi_prov_start_ap()` aborteert bij een fout, en draait nu tijdens bedrijf — opgelost
- [x] **H10** Een afgekeurde sleutel laat een werkend apparaat zonder credentials achter — opgelost
- [x] **M12** Het scherm bleef "Instellen" met QR tonen nadat het portaal was gesloten — opgelost
- [x] **H4** Geen OTA, terwijl de partitietabel er twee slots voor heeft — opgelost, `esp_https_ota` met versiecheck en terugval
- [ ] **H5** Credentials liggen leesbaar in flash

### Middel
- [x] **M1** SSID's worden ongeëscaped in JSON geplakt — opgelost, cJSON
- [x] **M2** `%d` met een `size_t` — opgelost
- [x] **M3** Foutdetectie via `strstr` op de ruwe body — opgelost, structureel i.p.v. tekstueel
- [x] **M4** Vaste retry van 10 seconden, oneindig lang — opgelost, backoff met jitter
- [ ] **M5** NVS-schrijfactie in de event handler
- [x] **M6** `app_main` kan oneindig blijven wachten — opgelost, stilte-timeout
- [x] **M7** "Ring uit" betekent twee verschillende dingen — opgelost op het scherm van de BOX-3
- [ ] **M10** Tijdens provisioning blijft het schema afgewezen credentials proberen
- [x] **M11** Een gebladerd voorbeeld is niet te onderscheiden van een echte meting — opgelost
- [x] **M8** Twee responses op één request in het API-check pad — opgelost
- [x] **M9** De ESP-IDF-versie ligt nergens vast — opgelost, vastgelegd op v6.1

### Klein
- [x] **L1** Geen tests, geen CI, geen static analysis — opgezet
- [x] **L2** Compilerwaarschuwingen staan op de standaard — opgelost, twaalf meldingen opgeruimd
- [x] **L3** `sdkconfig` én `sdkconfig.old` ingecheckt — opgelost, `sdkconfig.defaults`
- [ ] **L4** Geen LICENSE-bestand
- [ ] **L5** HTML/CSS/JS als C-stringliteral
- [ ] **L6** Zeven keer hetzelfde JSON-parseerblok
- [ ] **L7** Dode code
- [ ] **L8** Ongebruikte macro naast een hardcoded URL
- [x] **L9** Headers zijn niet zelfstandig — opgelost, was een harde buildbreker op IDF 5.5.5
- [x] **L10** `is_valid_credentials()` zonder `void` — opgelost
- [ ] **L11** Client secret in een `type=text`-veld; `data` niet `static`
- [ ] **L12** Losse `printf` in de HTTP-eventhandler vervuilt de log

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

> **Opgelost.** Eén helper `s_receive_body()` leest nu beide formulieren: hij weigert een body
> die niet past in plaats van hem stil af te kappen, en leest door tot `content_len` bytes
> binnen zijn. Een socket-timeout wordt hooguit drie keer verdragen — de server heeft één
> worker, dus een client die bytes zit te druppelen mag hem niet vasthouden.
>
> De veldbuffers zijn op de **gecodeerde** lengte gedimensioneerd (3× de waarde, want elke byte
> kan `%XX` worden). Dat was een tweede, verborgen versie van hetzelfde probleem: een SSID als
> `a b c d e f g h i j k l m n o p` is 31 tekens maar 61 gecodeerd, en werd daarvoor door
> `httpd_query_key_value()` afgekapt tot "Missing ssid" — een volstrekt legitiem netwerk dat
> niet te kiezen was. Na het decoderen wordt de echte lengte gecontroleerd (32 voor de SSID,
> 63 voor het wachtwoord) en te lang wordt geweigerd in plaats van later stilletjes
> ingekort. Ook de returncode van het wachtwoordveld wordt nu bekeken: ontbreken mag (open
> netwerk), afgekapt worden niet.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest.

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

> **Opgelost, beide helften.**
>
> `main.c` ging van 34 naar 5 `ESP_ERROR_CHECK`-aanroepen. Wat overblijft is echt
> onherstelbaar: NVS-init, het aanmaken van de API-lock, de GPIO-configuratie van de
> status-LEDs, de wifi-stack en het aanmaken van de eerste task. Al het andere gaat via
> `s_log_if_failed()` — een LED die niet brandt, een teller die niet wegschrijft, een
> LED-ring die niet initialiseert. Zo'n apparaat is nuttiger draaiend met één kapot onderdeel
> dan eeuwig herstartend. `status_led.c` had er ook nog drie, waaronder één in `led_ring_show()`
> dat vanuit de telemetrietaak loopt; die geven nu een foutcode terug.
>
> De bootteller kijkt nu naar `esp_reset_reason()` via een tabel: alleen `ESP_RST_POWERON` en
> `ESP_RST_EXT` tellen mee. Een panic, een watchdog-beet of een brownout is de firmware die
> faalt, niet de gebruiker die om een reset vraagt — die tellen dus niet meer mee. Daarmee is
> de gevaarlijke koppeling weg: een bug vroeg in `app_main` kan de configuratie van de klant
> niet langer zelf wissen. De laatste rij van de tabel is de catch-all, dus onbekende
> resetredenen tellen veilig níét mee.
>
> Het pad dat het provisioningportaal opstart is samengetrokken in
> `start_provisioning_portal()`, die `false` teruggeeft als het AP of de webserver niet start.
> De aanroeper wacht dan niet op een portaal dat er niet is, en het reconnect-schema van C2
> blijft ondertussen het opgeslagen netwerk proberen.
>
> README bijgewerkt: die beschreef "power-cycle or reset three times" zonder onderscheid.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest.

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

### H6 — De wifi-status-led zit op GPIO 44, dat is U0RXD
`main/inc/status_led.h:15`, `sdkconfig.defaults` (console)

`STATUS_LED_WIFI_GPIO` staat op `GPIO_NUM_44`. Op de ESP32-S3 is dat **U0RXD**, de
ontvangstlijn van UART0. De consoleconfiguratie zet UART0 als primaire uitvoer
(`CONFIG_ESP_CONSOLE_UART_DEFAULT`, met USB-Serial-JTAG als secundaire).

`status_leds_init()` configureert die pin als push-pull uitgang. Op elk board met een
USB-UART-bridge op GPIO 43/44 gaan er dan twee drivers tegen elkaar in op dezelfde lijn: de
bridge stuurt zijn TX naar de ESP-RX, terwijl de ESP diezelfde lijn hoog en laag trekt voor
de led. Gevolgen, oplopend in ernst:

1. Seriële invoer richting het apparaat werkt niet meer. Uitvoer (GPIO 43) blijft werken.
2. Twee tegen elkaar in werkende push-pull uitgangen betekent kortsluitstroom door beide
   pinnen zolang ze verschillen. Dat is geen theoretische zorg maar een pinbelasting.

De README noemt de pin al als "D7 / RX", dus het is gezien maar niet als conflict herkend.

**Fix:** kies een pin die niet aan UART0 vastzit, of zet de console op USB-Serial-JTAG als het
board via de USB-poort van de chip zelf loopt. Tot dat besluit genomen is: flash en monitor via
de **native USB-poort** van het board, niet via de UART-bridge.

**Nog te bevestigen op hardware.** Of dit conflict optreedt hangt af van het board: heeft het
een bridge op GPIO 43/44, of alleen de native USB-poort?

### H7 — `energyboxx_api_setup()` laat het oude token staan
`main/src/energyboxx_api.c`

`energyboxx_api_setup()` neemt nieuwe sleutels aan en zet `valid_credentials` op false, maar
laat het bestaande token en zijn vervaltijd ongemoeid. `fetch_token()` ziet daarna een token
dat nog geldig is, slaat de aanvraag over en geeft `ESP_OK` terug.

Het gevolg is dat de webpagina "Validation successful" meldt en de sleutels naar NVS schrijft,
**zonder dat ze ooit zijn beproefd**. Iemand kan volstrekt verkeerde sleutels invoeren, te horen
krijgen dat ze goed zijn, en er twee uur later achter komen als het oude token verloopt.

Waargenomen op hardware op 2026-08-31:

```
[113.22] Energyboxx API setup completed with Client ID and Client Secret
[113.22] Token still valid for 7090 more seconds, skipping fetch
[113.22] API credentials saved successfully
```

Daarna bleef `valid_credentials` op false staan, dus het statusscherm meldde "sleutels
afgekeurd" en het apparaat toonde "Geen gegevens" terwijl het met het oude token gewoon
metingen bleef ophalen.

Dit zat al in de oorspronkelijke firmware. Bij een eerste installatie is er geen eerder token,
dus de overslag treedt daar nooit op; de bevinding werd pas bereikbaar toen het portaal opnieuw
geopend kon worden om werkende sleutels te vervangen (fase 6a van `docs/PLAN-box3.md`).

**Fix:** nieuwe sleutels maken het oude token betekenisloos, dus `setup()` gooit het weg -
token leeg, vervaltijd nul, en `renew_token` aan.

> **Opgelost.**

### H8 — De DNS-server lekt zijn socket bij het stoppen
`main/src/dns_server.c`

`stop_dns_server()` schiet de taak dood met `vTaskDelete()` terwijl die in `recvfrom()` op de
socket hangt. De socket wordt nooit gesloten, dus poort 53 blijft bezet. Een volgende
`start_dns_server()` kan niet binden.

De tweede helft maakt het onzichtbaar: de mislukte `bind()` wordt gelogd en genegeerd, waarna
er onvoorwaardelijk "Socket bound, port 53" onder staat. De server draait door op een
niet-gebonden socket en ziet er in de log gezond uit.

Waargenomen op 2026-08-31: het tweede portaal gaf `Socket unable to bind: errno 112` en daarna
geen enkele DNS-vraag meer, bij drie verbindingspogingen. De gebruiker kreeg wel een IP-adres
maar geen captive portal.

Ook dit zat al in de code. De oorspronkelijke firmware startte de DNS-server één keer en stopte
hem één keer, dus het lek had geen gevolg.

**Fix:** de socket in de handle bewaren en bij het stoppen sluiten, en een mislukte bind de
server laten staken in plaats van doorgaan.

> **Opgelost.** Dit is de eerste wijziging in `main/src/dns_server.c`, dat tot nu toe letterlijk
> van Espressif was overgenomen. De alternatieven waren slechter: de DNS-server permanent laten
> draaien lost het lek op, maar hij bindt op alle interfaces en zou dan ook DNS-vragen vanaf het
> thuisnetwerk beantwoorden met het adres van het accesspoint. De drie wijzigingen staan
> gemerkt als `LOCAL CHANGE`, met een blok bovenaan het bestand dat uitlegt wat er afwijkt van
> de bron.

### H9 — `wifi_prov_start_ap()` aborteert bij een fout, en draait nu tijdens bedrijf
`main/src/wifi_provisioning.c`

`esp_wifi_set_mode()` en `esp_wifi_set_config()` stonden onder `ESP_ERROR_CHECK`. Bij **H2** is
die aanroep blijven staan omdat hij alleen bij het opstarten liep, waar een abort te overleven
is. Sinds het portaal vanaf het scherm geopend kan worden, loopt dit pad terwijl iemand het
apparaat staat te gebruiken — en dan herstart een mislukte moduswissel het hele apparaat.

**Fix:** de foutcode teruggeven. Hetzelfde geldt voor `esp_wifi_set_config(WIFI_IF_STA, ...)`
in `wifi_prov_connect()`.

> **Opgelost.**

### H10 — Een afgekeurde sleutel laat een werkend apparaat zonder credentials achter
`main/src/wifi_web.c`, `main/src/energyboxx_api.c`

`energyboxx_api_setup()` overschrijft de credentials in het geheugen en gooit sinds **H7** het
token weg. Wordt de nieuwe sleutel daarna afgekeurd, dan blijft het apparaat achter met
onbruikbare gegevens in het geheugen, terwijl in NVS nog de werkende sleutels staan. Het scherm
gaat op "Geen gegevens" en blijft daar tot iemand het apparaat herstart.

Eén verkeerd getypt teken maakt een werkend apparaat dus tijdelijk stuk. Dat is te veel straf
voor een vergissing die iedereen maakt.

**Fix:** `setup()` bewaart de vorige credentials, en de webhandler zet ze terug zodra de nieuwe
worden afgekeurd. Er wordt meteen een nieuw token gehaald, zodat het apparaat weer werkt voordat
de gebruiker de foutmelding op zijn telefoon heeft uitgelezen.

> **Opgelost.**

### H4 — Geen OTA, terwijl de partitietabel er twee slots voor heeft
`sdkconfig` (`CONFIG_PARTITION_TABLE_TWO_OTA_LARGE=y`), geen OTA-code in `main/`

De flashindeling is al ingericht op over-the-air updates, maar er staat geen regel updatecode in
het project. Elke fix hierboven vereist dus fysiek langs elk apparaat met een USB-kabel. Voor
apparaten die bij mensen thuis staan is dat het verschil tussen een bug en een terugroepactie.

**Fix:** `esp_https_ota` met versiecheck en rollback (`app_rollback`) toevoegen zolang de vloot
nog klein is.

> **Opgelost.** `main/src/updater.c` haalt firmware op bij GitHub Releases, via de vaste URL
> `releases/latest/download/energy-owl.bin`. Daardoor is er geen manifest nodig en is er één
> plek die bepaalt wat de apparaten krijgen: het publiceren van de release.
>
> De module heeft dezelfde vorm als de machines uit fase 6b: vijf toestanden, één tabel
> `s_next [state][event]`, en `updater_handle()` als enige schrijver. Elke overgang komt in het
> log te staan.
>
> Het versienummer komt uit de git-tag via `git describe`, dus niemand hoeft een constante bij
> te werken. De updater leest eerst de beschrijving van het aangeboden beeld en breekt af als
> die niet nieuwer is — er wordt dan geen enkele byte van de image binnengehaald. Een build uit
> een werkmap (`v0.1.0-dirty`) wordt herkend en telt niet als release.
>
> Terugval is aan (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). Nieuwe firmware staat in proeftijd
> en wordt pas definitief na een volledige ronde langs de API, of na tien minuten
> aaneengesloten netwerk. Dat tweede bewijs voorkomt dat een API-storing goede firmware
> terugdraait; het is nog steeds sterk genoeg, want firmware die het netwerk haalt kan altijd
> nog vervangen worden. Firmware die meteen vastloopt haalt geen van beide en verdwijnt bij de
> volgende start vanzelf.
>
> Er wordt vijf minuten na het opstarten voor het eerst gekeken en daarna elk uur, met een knop
> **Nu bijwerken** op de About-pagina voor wie niet wil wachten. Tijdens het ophalen neemt het
> scherm de toestand `updating` aan met het percentage erbij, want er komt een herstart aan.
>
> Uitgeven gaat via `.github/workflows/release.yml`: een tag `v*` bouwt de firmware, controleert
> of het versienummer in het beeld gelijk is aan de tag, en hangt het bestand aan een
> **concept**-release. Zolang die een concept is, haalt geen apparaat hem op. Zie
> [OTA.md](OTA.md) voor de procedure.
>
> Wat hier nog niet in zit: het apparaat controleert het certificaat van GitHub, maar geen
> handtekening onder het bestand zelf. Dat is H5.
>
> **De eerste keer moet met een kabel.** De firmware die nu op de vijf apparaten staat kan
> zichzelf niet bijwerken.
>
> Gebouwd voor esp32s3 op ESP-IDF 6.1. Nog niet op hardware getest.

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

> **Opgelost.** De netwerklijst wordt met `cJSON_CreateArray()` opgebouwd en in één keer
> verstuurd. Een SSID wordt gekozen door wie het access point bezit en is dus invoer van
> buiten; cJSON escapet hem in plaats van hem rauw in de structuur te plakken. De `item[128]`
> buffer die midden in een string kon afkappen is weg.
>
> Blijft staan: een SSID met bytes die geen geldige UTF-8 zijn komt als vervangingsteken in de
> browser terecht. Dat is cosmetisch — de structuur van het antwoord kan er niet meer door
> breken, en dat was het punt.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest.

### M2 — `%d` met een `size_t`
`main/src/energyboxx_api.c:164`

`ESP_LOGI(TAG, "Stored token (%d chars)", strlen(access_token));` — formaatmismatch, undefined
behaviour. Zou met `-Wformat` moeten opvallen; dat het dat niet doet zegt iets over de
warning-instellingen (L2).

**Fix:** `%zu`. **Opgelost.**

### M3 — Foutdetectie via `strstr` op de ruwe body
`main/src/energyboxx_api.c:251`

`strstr(response_buffer, "\"AUTH-1000\"")` zoekt een foutcode in ongeparseerde tekst. Breekt
zodra Energyboxx zijn foutformaat aanpast, en kan vals-positief zijn als die string ergens in
legitieme data opduikt. De statuscodes 401/403 ernaast zijn de betrouwbare check.

**Fix:** de body parsen en het foutveld gericht uitlezen, of alleen op statuscode vertrouwen.

> **Opgelost — en er zat meer achter.** De `strstr` is weg; 401 en 403 blijven als
> authenticatiecontrole. Het gat dat die `strstr` afdekte is nu structureel gedicht in plaats
> van tekstueel: `community_power_result_kw` is een **verplicht** veld geworden.
>
> Dat bleek belangrijker dan de bevinding zelf. Alle velden werden bij afwezigheid op `0.0f`
> gezet, dus een foutantwoord van de API — dat óók geldige JSON is — werd stilletjes gelezen
> als "nul kilowatt" en gaf `ESP_OK` terug. De ring ging dan uit en toonde "gemeenschap in
> balans": een zelfverzekerd antwoord opgebouwd uit niets. Nu levert een antwoord zonder dat
> veld `ESP_ERR_INVALID_RESPONSE` op, waarna de aanroeper het token vernieuwt en het opnieuw
> probeert — ongeacht welke foutcode de API verzonnen heeft.
>
> Dit haalt ook een deel van de angel uit **M7**: "ring uit" kan nu niet meer per ongeluk uit
> een foutantwoord komen.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest.

### M4 — Vaste retry van 10 seconden, oneindig lang
`main/main.c:26`, `main/main.c:117`, `main/main.c:128`, `main/main.c:139`

Bij een API-storing blijft elk apparaat eeuwig elke 10 seconden aankloppen. Met een paar honderd
apparaten is dat 6× per minuut per apparaat tegen een endpoint dat al in de problemen zit.

**Fix:** exponentiële backoff 10s → 20s → 40s → ... tot 5 min, plus willekeurige jitter zodat de
vloot niet synchroon loopt.

> **Opgelost.** `s_api_retry_delay_ms` is een tabel — 10, 20, 40, 80, 160, 300 seconden — waarvan
> de laatste rij zich herhaalt. Een lange storing zakt daarmee naar één poging per vijf minuten
> per apparaat in plaats van zes per minuut.
>
> Alle wachttijden krijgen tot een vijfde extra als jitter, óók de normale poll van 60 seconden.
> Dat laatste was strikt genomen geen onderdeel van de bevinding, maar het is dezelfde zorg: een
> straat die na een stroomstoring tegelijk opstart blijft anders tot in de eeuwigheid tegelijk
> vragen. Een geslaagde ronde zet de backoff terug op rij nul.
>
> Wachten op wifi telt bewust niet mee in het schema — er is dan niets aan de API gevraagd — en
> houdt zijn eigen vaste poll van 10 seconden.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest.

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

> **Opgelost.** Na vijftien minuten **stilte** herstart het apparaat en probeert het de
> opgeslagen credentials opnieuw. Stilte, niet verstreken tijd: elke handler die een mens
> aanraakt — de twee pagina's, `/connect`, `/api-check`, `/scan` — meldt activiteit via
> `wifi_prov_note_portal_activity()` en zet de klok terug. Iemand die rustig zijn client secret
> zit over te typen wordt dus niet onder zijn handen vandaan herstart.
>
> `/status` telt bewust **niet** mee: die wordt door de pagina zelf elke seconde gepollt, en
> anders houdt één vergeten open tabblad het apparaat eeuwig in de portal.
>
> Dit componeert met H2: `esp_restart()` meldt `ESP_RST_SW`, en die reden telt niet mee voor de
> drie-power-cycles-wist-credentials regel. De herstart kan dus niet per ongeluk de configuratie
> van de klant wissen.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest.

### M7 — "Ring uit" betekent twee verschillende dingen
`main/main.c:116`, `:126`, `:137` versus `main/main.c:159`

De ring gaat uit bij "gemeenschap in balans" én bij "geen wifi / API onbereikbaar". De README
erkent dit. De losse data-LED knippert dan wel, maar de ring — het ding waar mensen naar kijken —
toont "alles in balans" terwijl er in werkelijkheid geen data is.

**Fix:** geef "geen data" een eigen taal op de ring: een langzame ademende puls, of één gedimde
pixel. Uit hoort "ik weet het en het is niets" te betekenen, niet "ik weet het niet".

> **Opgelost, maar anders dan hier bedacht.** De ring bestaat niet meer; de ESP32-S3-BOX-3 heeft
> een scherm. Sinds fase 3 van `docs/PLAN-box3.md` heeft elke toestand een eigen kleur én een
> eigen tekst: "In balans" op donkergroen tegenover "Geen gegevens" op grijs. Daarmee is de
> dubbelzinnigheid weg bij de wortel. Een scherm kan zeggen dat het het niet weet; een gedoofde
> led kan dat niet.
>
> **M3** haalde er eerder al een deel van de angel uit, door te voorkomen dat een foutantwoord
> van de API stilzwijgend als nul kilowatt werd gelezen. Die twee samen zijn de bevinding.

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

> **Opgelost.** `parse_api_credentials()` stuurt niets meer — het bevat geen enkele
> `httpd_resp`-aanroep. Het leest, decodeert, controleert de lengtes tegen de buffers van de
> aanroeper en geeft alleen een foutcode terug; `api_check_post_handler()` is de enige die
> antwoordt, in JSON, precies één keer. Daarmee is ook het aparte decodeerblok dat bij C1 in
> de aanroeper stond weer opgeruimd: dat zit nu waar het hoort.
>
> Gebouwd voor esp32s3 op ESP-IDF 5.5.5. Niet op hardware getest.

---

## Klein

Geen van deze breekt iets, samen bepalen ze wel hoe het project over een jaar aanvoelt.

- **L1 — Geen tests, geen CI, geen static analysis.** **Opgezet.**
  `.github/workflows/ci.yml` draait bij elke push drie jobs: de host-tests uit `test/`, een
  `cppcheck`-pass over `main/`, en een volledige ESP32-S3 build in de officiële
  `espressif/idf`-container. Alle drie zijn lokaal gedraaid vóór ze zijn ingecheckt.
  De cppcheck-pass staat op `warning,performance,style` met `--error-exitcode=1` en sluit
  `main/src/dns_server.c` uit: dat is Espressif's voorbeeldbestand, letterlijk overgenomen, en
  zijn stijlbevindingen zijn niet de onze om te repareren. Op de rest van `main/` is de check
  schoon, dus de lat kan blijven staan waar hij nu staat.
  Nog open: de containertag `release-v5.5` beweegt; die zou op een exacte patchrelease gepind
  moeten worden zodra het project er een kiest.
- **L2 — Compilerwaarschuwingen staan op de standaard.** **Opgelost.** `main/CMakeLists.txt`
  zet nu een strengere set vlaggen op **alleen dit component**: `-Wall -Wextra -Wshadow`
  `-Wformat=2 -Wpointer-arith -Wvla -Wstrict-prototypes -Wmissing-prototypes`
  `-Wunused-parameter`. Projectbreed zou onze eigen uitvoer verdrinken in meldingen uit code
  die we niet onderhouden. ESP-IDF heeft zelf al `-Werror` aan, dus deze meldingen zijn meteen
  fataal.
  Dat leverde direct **twaalf** punten op in bestaande code: vier keer een variabele
  `nvs_handle` die de gelijknamige verouderde typedef uit `nvs.h` overschaduwt, vijf ongebruikte
  parameters, en drie functies zonder prototype — `task_reset_boot_count` en
  `http_404_error_handler` hadden `static` moeten zijn, en `app_main` had een declaratie nodig.
  Allemaal opgeruimd; de build is nu volledig schoon.
  Twee vlaggen zijn bewust **niet** aangezet: `-Wundef` en `-Wredundant-decls` gaan alleen af
  binnen ESP-IDF's eigen headers (`assert.h` test een niet-gedefinieerde configmacro, `stdio.h`
  declareert `flockfile` dubbel). Een waarschuwing die je alleen kunt stilzetten door andermans
  header te patchen is ruis, en ruis is waarom mensen waarschuwingen gaan negeren.
  `main/src/dns_server.c` houdt de mildere set: dat is Espressif's voorbeeldbestand, letterlijk
  overgenomen, en het patchen ervan maakt de volgende update lastiger.
- **L3 — `sdkconfig` én `sdkconfig.old` zijn ingecheckt.** **Opgelost.** Van 4032 regels bleven
  er vier instellingen over die dit project echt kiest: de ESP32-S3, 8 MB flash, de
  twee-OTA-partitietabel en `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384`. `sdkconfig` en
  `sdkconfig.old` staan nu in `.gitignore`.
  Twee dingen die dit boven water haalde: `idf.py save-defconfig` **miste de main-task stack**
  (16384 tegen een default van 3584 — `app_main` doet zelf een TLS-request met een 4 KB
  buffer op die stack, dus dat was direct fataal geweest), en de oude config droeg
  `CONFIG_MBEDTLS_THREADING_C` mee, wat op IDF 5.5 niet eens compileert. Beide gevonden door
  de tool niet te vertrouwen en een volledige diff tegen een verse defaultconfig te draaien,
  en daarna vanaf nul te bouwen.
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
- **L12 — Losse `printf` in de HTTP-eventhandler vervuilt de log.**
  `main/src/energyboxx_api.c:57` doet een `printf("\n")` bij elk ontvangen datastuk, buiten de
  `if/else` om. In de seriële log staan daardoor drie lege regels vóór elk antwoord. Gezien op
  hardware op 2026-08-28. Het is bovendien een `printf` per chunk, buiten het logsysteem om,
  dus zonder tag en zonder niveau. **Fix:** weghalen, of vervangen door één `ESP_LOGD` met het
  aantal ontvangen bytes.

- **L11 — Client secret in een `type=text`-veld** (`main/src/wifi_web.c:143`), terwijl het
  wifi-wachtwoord wél op `password` staat. En `energyboxx_data_t data` in `main/main.c:19` is niet
  `static`.

### M10 — Tijdens provisioning blijft het schema afgewezen credentials proberen
`main/src/wifi_provisioning.c` (`s_schedule_retry`, ingevoerd bij C2)

Waargenomen op hardware op 2026-08-28, tijdens een mislukte provisioning:

```
[wifi_prov]: STA disconnected, reason=15      (4-way handshake timeout)
[wifi_prov]: Reconnecting in 60000 ms
[wifi_prov]: STA disconnected, reason=2       (auth expired)
[wifi_prov]: Reconnecting in 300000 ms
```

Het reconnect-schema van C2 loopt door zolang `current_ssid` gevuld is. Bij een 4-way
handshake timeout weten we echter dat het wachtwoord niet klopt: opnieuw proberen met
diezelfde credentials kan per definitie niet slagen. Alleen nieuwe credentials helpen, en
die komen via het portaal.

Twee gevolgen:

1. Het apparaat blijft het accesspoint van de gebruiker benaderen met een wachtwoord dat al
   is afgewezen. Sommige routers gaan een client daarna tijdelijk blokkeren — de tweede
   poging hierboven strandde al in de authenticatiefase (reason 2) in plaats van bij de
   handshake, wat op zoiets kan wijzen.
2. De radio is bezet met die pogingen terwijl de gebruiker in het portaal staat.

**Fix:** zolang het provisioningportaal open staat, het schema begrenzen of stoppen bij een
reden die op verkeerde credentials wijst (`WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT`,
`WIFI_REASON_AUTH_FAIL`). Doorproberen slaat pas aan als er credentials zijn die ooit hebben
gewerkt.

### M12 — Het scherm bleef "Instellen" met QR tonen nadat het portaal was gesloten
`main/src/wifi_provisioning.c`

Er waren twee manieren om het portaal af te breken. `wifi_prov_close_portal()` deed het netjes
en zette de gemelde toestand terug. `wifi_prov_wait_until_completed()`, het pad dat bij het
opstarten loopt, deed hetzelfde met de hand — maar vergat die toestand.

Gevolg: na een geslaagde provisioning bleef `wifi_prov_get_state()` op `AP_ACTIVE` staan. Het
scherm toonde "Instellen" met een QR-code voor een accesspoint dat niet meer bestond, terwijl
het apparaat gewoon telemetrie ophaalde. Er was geen weg terug behalve herstarten.

`wifi_prov_close_portal()` weigerde bovendien als het portaal niet vanaf het scherm was
geopend, dus de knop "Klaar" op de sleutelpagina deed op dit pad helemaal niets.

Waargenomen op hardware op 2026-09-01, tijdens een netwerkwissel naar een telefoonhotspot.

**Fix:** één afbraakpad. `wifi_prov_close_portal()` is idempotent gemaakt en herstelt altijd de
gemelde toestand; `wait_until_completed()` roept het aan in plaats van het over te doen. Daarmee
verdween ook de laatste `ESP_ERROR_CHECK` op dit pad, dezelfde soort als **H9**.

> **Opgelost.**

### M11 — Een gebladerd voorbeeld is niet te onderscheiden van een echte meting
`main/src/status_view.c` (bladermodus, ingevoerd bij fase 5 van `docs/PLAN-box3.md`)

Gevonden op 2026-08-31, doordat Edwin het zelf niet kon zien. Fase 5 gaf het apparaat een
bladerfunctie waarmee je door de energietoestanden kunt lopen. Zo'n voorbeeld zag er precies zo
uit als een echte meting: "Energie inkopen" op geel, zonder enig verschil.

Twee gevolgen. Wie bladert weet niet meer of wat er staat gemeten of gekozen is. En wie
wegloopt terwijl er een voorbeeld staat, laat tot de terugval een verzonnen waarde aan de muur
hangen.

Dit was geen fout in de oorspronkelijke firmware maar een die bij het bouwen van fase 5 is
ingevoerd. Dat maakt hem niet minder echt: het apparaat toonde iets dat niet gemeten was.

> **Opgelost.** Elk beeld dat niet de actuele toestand is, krijgt linksboven een merkteken
> "voorbeeld". Het statusscherm is uitgezonderd, want dat gaat over het apparaat zelf en is
> nooit een meting.

### M9 — De ESP-IDF-versie ligt nergens vast
`sdkconfig` (t/m commit 08908ec), `.github/workflows/ci.yml`

De `sdkconfig` die tot nu toe was ingecheckt is gegenereerd door **ESP-IDF 6.1.0**. De CI bouwt
tegen `espressif/idf:release-v5.5`, en de lokale ontwikkelomgeving heeft 5.5.5. Niemand legt
ergens vast welke versie de juiste is.

Dat is niet academisch. Bij het opruimen van L3 bleek de 6.1.0-configuratie instellingen mee te
dragen die op 5.5 **niet compileren** (`CONFIG_MBEDTLS_THREADING_C` → "MBEDTLS_THREADING_ALT
defined, but not all prerequisites"), en 6.1 kiest picolibc waar 5.5 newlib kiest — een andere
C-bibliotheek onder dezelfde broncode. Het binaire bestand veranderde ~40 KB van formaat alleen
door deze opruiming.

**Fix:** kies een versie, zet hem in de README én in de CI-containertag (een exacte patchrelease,
niet `release-v5.5`, want die tag beweegt). Zolang dat niet gebeurt, bouwt iedereen net iets
anders en is "het werkt bij mij" geen uitspraak over iets.

> **Opgelost: het project mikt op ESP-IDF v6.1.**
>
> Espressif geeft elke release 30 maanden ondersteuning: 12 maanden service, daarna 18 maanden
> maintenance waarin alleen ernstige en security-problemen nog een fix krijgen. Sinds juli 2020
> is er geen apart LTS-begrip meer; alle releases krijgen dezelfde termijn.
>
> Dat maakt de keuze eenduidig. v5.5 kwam uit in juli 2025 en is **sinds ~21 juli 2026 uit de
> service-periode**; ondersteuning loopt tot ~januari 2028, maar alleen nog voor ernstige zaken.
> Espressif raadt voor een nieuw project expliciet een release "in service" aan. v6.1 is de
> huidige stable en zit tot medio 2027 in service, met ondersteuning tot begin 2029.
>
> Er zat één afweging tegenin: v6.1 is weken oud en dus een `.0`. Die weegt hier minder zwaar,
> omdat er nog niets is uitgeleverd, omdat de oorspronkelijke firmware al op 6.1 is ontwikkeld,
> en omdat er inmiddels CI en een testboard zijn om een regressie op te vangen.
>
> Vastgelegd op drie plekken:
>
> - `README.md` noemt v6.1 als doelversie, met de normale ESP-IDF-installatie als bouwweg.
> - `.github/workflows/ci.yml` gebruikt de **vaste** tag `espressif/idf:v6.1`. Niet
>   `release-v6.1`: die beweegt mee met de branch en verandert de toolchain onder je handen.
> - `tools/idfenv.sh` is gedegradeerd tot terugvaloptie en waarschuwt als de gevonden IDF niet
>   v6.1 is. PlatformIO levert 5.5.x, dus een build langs die weg bewijst niets over de
>   doelversie.
>
> **De CI controleert nu ook of de configuratie echt is toegepast.** `sdkconfig.defaults` wordt
> stilzwijgend genegeerd voor elk symbool dat de gebruikte ESP-IDF niet kent. Precies zo raakte
> `CONFIG_ESP_MAIN_TASK_STACK_SIZE` bij L3 al een keer zoek, en de default van 3584 bytes is
> veel te klein voor een `app_main` die een TLS-request doet. De buildjob vergelijkt daarom de
> gegenereerde `sdkconfig` met vier verwachte waarden en faalt als er één ontbreekt.
>
> **Gevolg voor de hardwaretests:** v6 gebruikt picolibc waar v5 newlib gebruikte. De zeven
> bevindingen die op 2026-08-28 op hardware zijn bevestigd, zijn bevestigd op een build met
> ESP-IDF 5.5.5. Die tests moeten opnieuw op een v6.1-build.

---

## Bevestigd op hardware

> **Let op: het bord is gewisseld.** Sinds 2026-08-29 gaat het project naar de
> ESP32-S3-BOX-3; zie `docs/PLAN-box3.md`. De twee testrondes hieronder zijn gedaan op de
> Seeed XIAO ESP32-S3 en gelden dus niet meer als bewijs voor het product. Zij blijven staan
> als bewijs dat de fixes zelf werken. Wat opnieuw moet: alles wat hardware raakt.
>
> Bevinding **H6** vervalt met de wissel — de wifi-led op GPIO 44 bestaat niet meer.
> Bevinding **M7** wordt oplosbaar in plaats van een afweging, want een scherm kan
> "geen gegevens" wél tonen; dat is fase 3 van het plan.

**Board:** Seeed Studio XIAO ESP32-S3 Sense · ESP32-S3 rev v0.2 · 8 MB flash · 8 MB PSRAM
**Datum:** 2026-08-28 · **Firmware:** deze branch, gebouwd met **ESP-IDF 5.5.5**

> Deze ronde is gedaan vóór het besluit bij M9 om op v6.1 te bouwen. Zie de tweede ronde
> hieronder voor de doelversie.
**Uitgevoerd:** eerste boot, provisioning via het portaal, wifi verbinden, API-sleutels
invoeren, token ophalen en twee telemetrierondes.

De kolom zegt wat de test **bewijst**, niet wat er is opgelost.

| Bevinding | Status | Grondslag |
| --- | --- | --- |
| **C4** API-setup pagina | **Bevestigd** | De pagina rendert en is bruikbaar; sleutels ingevoerd en geaccepteerd |
| **M1** Netwerklijst als JSON | **Bevestigd** | De browser parseert de lijst en een netwerk is gekozen |
| **C3** Lock op de tokenstate | **Bevestigd** | `setup` en `fetch_token` vanuit de httpd-taak, daarna `skipping fetch` vanuit de wachtlus. Geen deadlock, geen corruptie |
| **M4** Jitter op het interval | **Bevestigd** | Gemeten interval 71,8 s. Zonder jitter zou dat exact 60,0 s zijn |
| **M8** Eén antwoord per request | **Bevestigd** | Het portaal verwerkt het JSON-antwoord van `/api-check` correct |
| **L9/L10** Zelfstandige headers | **Bevestigd** | De firmware bouwt en draait |
| **H2** Resetreden | **Deels** | `ESP_RST_USB` correct herkend en niet meegeteld. Power-on nog niet getest |
| **H1** Body volledig lezen | **Deels** | Beide formulieren verwerkt, en de gecodeerde vorm van het wachtwoord was langer dan de waarde zelf. Een body die de buffer overschrijdt is niet beproefd |
| **C1** URL-decoding | **Bevestigd** | Wifiwachtwoord met spaties én procenten. Zonder de fix waren die als `%20` en `%25` in NVS beland en had de verbinding gefaald |
| **M3** Verplicht telemetrieveld | **Deels** | Geldige telemetrie wordt niet ten onrechte afgewezen. Het foutpad is niet beproefd |
| **C2** Reconnect-backoff | **Niet getest** | Er is geen verbinding weggevallen |
| **H3** Geweigerde scan | **Niet getest** | Er is niet twee keer snel gescand |
| **M6** Stilte-timeout | **Niet getest** | De provisioning was binnen vijftien minuten klaar |
| **M4** Backoff bij storing | **Niet getest** | Er is geen API-aanvraag mislukt |

Wat verder werkte zonder dat het een bevinding was: de captive portal-detectie van Windows
(`/connecttest.txt` krijgt een redirect), de DNS-omleiding, het valideren van het
servercertificaat via de bundel, het opslaan in NVS, en het omschakelen naar station-modus
nadat de client het accesspoint verliet.

De eerste meting was `community_power_result_kw = -3,171`, ruim onder de drempel van
-0,05 kW. Het apparaat meldde "Community is importing power" en zette de ring op geel. De
drempellogica klopt dus ook met echte waarden. Latere metingen lopen op tot -9,114 kW, met
telkens hetzelfde oordeel.

**C1 verdient een aparte vermelding.** Het wachtwoord bevatte spaties én procenttekens. Dat
is het strengste geval: de browser codeert een spatie als `%20` en een procentteken als
`%25`. Zonder decoder komt de letterlijke tekst `%20` en `%25` in NVS terecht en mislukt de
verbinding, met als enige melding "Could not connect. Check password". Het apparaat verbindt
en haalt telemetrie op, dus de decoder werkt op het lastigste teken dat er is.

### Tweede ronde: op de doelversie

**Datum:** 2026-08-28, later op de dag · **Firmware:** deze branch op commit `79405b3`,
gebouwd met **ESP-IDF v6.1** · zelfde board.

Deze ronde bestond uit twee delen. Eerst een boot met de credentials uit de eerste ronde nog
in NVS, waarbij de provisioning werd overgeslagen. Daarna is NVS gewist en is de provisioning
volledig opnieuw doorlopen via het portaal.

Voor C4, M1 en M8 is de grondslag dat de provisioning van begin tot eind is voltooid: zonder
een leesbare pagina, een bruikbare netwerklijst en één verwerkbaar antwoord op `/api-check`
komt een gebruiker niet aan de andere kant uit. Dat is een gevolgtrekking uit het resultaat,
geen regel uit de log.

| Bevinding | Status op v6.1 | Grondslag |
| --- | --- | --- |
| **C2** Reconnect na een wegval | **Bevestigd** | `STA disconnected, reason=4` → `Reconnecting in 500 ms` → `Got IP` na 5,6 s. De eerste rij van de tabel vuurde en het herstel slaagde |
| **H2** Resetreden | **Bevestigd** | `Reset reason 11, counts as user request: no` |
| **M3** Verplicht telemetrieveld | **Bevestigd** | Geldige telemetrie doorgelaten, alle velden geparseerd |
| **L9/L10** Zelfstandige headers | **Bevestigd** | Bouwt en draait op v6.1, geen waarschuwingen uit `main/` |
| **C1** URL-decoding | **Bevestigd** | Provisioning met een wachtwoord met spaties én procenttekens, en het apparaat verbindt |
| **C4** API-setup pagina | **Bevestigd** | De pagina rendert en accepteert de sleutels; daarna haalt het apparaat telemetrie op |
| **M1** Netwerklijst als JSON | **Bevestigd** | Het netwerk is uit de lijst gekozen, dus de browser parseerde hem |
| **M8** Eén antwoord per request | **Bevestigd** | Het portaal verwerkt het antwoord van `/api-check` en gaat door naar de volgende stap |
| **C3** Lock op de tokenstate | **Niet herhaald** | Provisioning verliep binnen dertig seconden, dus de tweede taak vroeg geen token op. Er was geen overlap om te beproeven |
| **M4** Jitter | **Niet herhaald** | Er is maar één telemetrieronde waargenomen |

Wat de overstap naar picolibc **niet** brak: de TLS-handshake vanuit `app_main`, het ophalen
en opslaan van het token, het parseren van de JSON, en de drempellogica. Die laatste liet
zich voor het eerst van de andere kant zien: `community_power_result_kw = +0,869` gaf
"Community is exporting power", dus de groene tak. Beide takken zijn nu op hardware gezien.

Verschillen tussen de twee versies, ter vergelijking:

| | v5.5.5 | v6.1 |
| --- | --- | --- |
| C-bibliotheek | newlib | picolibc |
| Vrije heap bij opstarten | 271 KiB | 279 KiB |
| Binariegrootte | 1.061.040 bytes | 1.068.416 bytes |

**H6 kreeg bevestiging uit onverwachte hoek.** ESP-IDF meldt bij het opstarten zelf:
`cpu_start: GPIO 44 and 43 are used as console UART I/O pins`. De firmware zet daarna diezelfde
GPIO 44 als uitgang voor de wifi-led. Op dit board hangt er geen bridge aan, dus er is geen
strijd om de lijn — maar het conflict is nu zwart-op-wit uit de IDF zelf.

**L12 blijft ongewijzigd zichtbaar:** drie lege regels vóór elk antwoord, ook op picolibc.

**Een valse start om te onthouden.** De eerste poging tot herprovisioning mislukte met
`reason=15`, een 4-way handshake timeout. Omdat dezelfde code op v5.5.5 wél werkte en er
procenttekens in het wachtwoord zaten, lag een decodeerfout door de versiesprong voor de hand.
Dat bleek onjuist: er was een typefout in het wachtwoord. `reason=15` betekent letterlijk
"de sleutel klopt niet", en dat is precies wat een typefout ook oplevert.

Twee dingen zijn daar toch uit gekomen. `httpd_query_key_value` is in v6.1 nagelezen in de
broncode en decodeert nog steeds niet, dus de dubbele-decodering waar de verdenking op viel
bestaat niet. En het gedrag van het reconnect-schema tijdens die mislukte pogingen leverde
**M10** op.

### Derde ronde: op de ESP32-S3-BOX-3

**Board:** ESP32-S3-BOX-3 · 16 MB flash · 16 MB octal PSRAM
**Datum:** 2026-08-31 · **Firmware:** branch `box3/plan`, ESP-IDF v6.1, fase 3 van het plan
**Uitgevoerd:** NVS leeg, provisioning volledig doorlopen via het portaal, daarna twee
telemetrierondes.

| Bevinding | Status | Grondslag |
| --- | --- | --- |
| **C4** API-setup pagina | **Bevestigd** | Pagina rendert, sleutels geaccepteerd, token opgehaald |
| **M1** Netwerklijst als JSON | **Bevestigd** | Netwerk uit de lijst gekozen, verbinding gelegd |
| **M8** Eén antwoord per request | **Bevestigd** | Het portaal verwerkt `/api-check` en gaat door |
| **M3** Verplicht telemetrieveld | **Bevestigd** | Twee geldige antwoorden doorgelaten en verwerkt |
| **M4** Jitter op het interval | **Bevestigd** | 73,16 s tussen twee rondes; zonder jitter zou dat steeds 60 s plus verzoektijd zijn |
| **M7** Twee betekenissen | **Bevestigd** | Zie hieronder |
| **C1** URL-decoding | **Bevestigd** | Zie hieronder |
| **C2** Reconnect-backoff | **Niet getest** | Er is geen verbinding weggevallen |

**M7 liet zich in het wild zien.** Tussen het krijgen van een IP-adres en het invoeren van de
API-sleutels zat 216 seconden. Al die tijd stond er **"Geen gegevens"** op het scherm: het
apparaat was verbonden, maar wist niets over de gemeenschap. Op de oude hardware was de ring in
diezelfde situatie gedoofd geweest — niet te onderscheiden van "in balans". Dit is precies het
geval waar de bevinding over ging, en het duurde ruim drie minuten.

**C1 is afgerond.** De grondslag is drieledig, en het is eerlijker die te noemen dan te doen
alsof één test alles bewees. Op de XIAO is op 2026-08-28 met spaties én procenttekens
aangetoond dat de decoder werkt. De code van `uri_decode` is sindsdien niet gewijzigd. Op de
BOX-3 heeft Edwin de afhandeling van wachtwoorden met spaties op 2026-08-31 in de code
nagelopen en akkoord bevonden, en het decodeerpad heeft hier gedraaid. Wat hier níét is
gebeurd, is een provisioning op dit bord met een wachtwoord dat decodering nodig had.

De toestanden liepen in de juiste volgorde over het scherm: `provisioning` → `connecting` →
`no-data` → `deficit`. Vier van de zeven toestanden zijn daarmee op hardware gezien.

In een tweede ronde later die dag, na fase 4 van het plan, is het provisioningscherm met de
QR-code beproefd: een telefoon die de code scant krijgt het netwerk aangeboden en verbindt
meteen. Daarmee is ook de zichtbare kant van het provisioningprobleem uit **C5** aangepakt —
al blijft C5 zelf open, want die gaat over de beveiliging van dat accesspoint, niet over de
vindbaarheid.

Verder in de log, en allebei ongevaarlijk:

- `httpd_uri: URI '/generate_204' not found` — Android controleert zo of er internet achter het
  accesspoint zit. De 404-handler stuurt hem door naar het portaal; zo hoort het te werken.
- `wifi: Password length matches WPA2 standards, authmode threshold changes from OPEN to WPA2` —
  de wifi-stack scherpt zijn eigen drempel aan op grond van de wachtwoordlengte.

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

Stappen 1 en 2 zijn afgerond (C1–C4, H1–H3, M1–M4, M6, M8, L1, L3, L9, L10). Wat resteert:

1. **Eerst op hardware.** Zeventien fixes zijn gecompileerd maar nooit uitgevoerd. Een board
   flashen en de vijf scenario's hieronder doorlopen weegt zwaarder dan welke volgende bevinding
   ook — zie "Wat niet is gecontroleerd".
2. **Beslissen vóór de eerste serie.** C5 (WPA2 met per-apparaat wachtwoord op het
   provisioning-AP) en H5 (flash encryption, secure boot, NVS-encryptie). Beide raken het
   productieproces, niet alleen de firmware, en H5 is na uitlevering niet meer aan te zetten.
   H5 weegt zwaarder nu H4 er is: de apparaten installeren voortaan zelf wat GitHub aanbiedt,
   en alleen secure boot maakt van "wat GitHub aanbiedt" ook "wat wij ondertekend hebben".
3. **Gedrag afmaken.** M7 (LED-semantiek: "uit" betekent nog twee dingen) en M5 (NVS-schrijfactie
   uit de event handler).
4. **Opruimen.** L2 en L4–L8, L11. Geen daarvan verandert gedrag; ze bepalen hoe het project over
   een jaar aanvoelt. L2 (compilerwaarschuwingen aanzetten) heeft de beste verhouding tussen
   moeite en opbrengst — dat had M2 en waarschijnlijk C4 vanzelf gevonden.

---

## Wat niet is gecontroleerd

De oorspronkelijke review is gedaan zonder buildomgeving. Sinds 2026-08-28 is die er wel
(ESP-IDF 5.5.5 uit de PlatformIO-installatie, plus een CI-build in de officiële
`espressif/idf`-container), en **elke fix op deze branch is gecompileerd** — meerdere keren met
resultaat: L9 bleek een harde buildbreker, en zowel de mbedTLS-instelling bij L3 als een
ontbrekende main-task stack werden alleen door een echte build gevonden.

Wat nog steeds ontbreekt: **geen enkele fix is op hardware uitgevoerd.** Compileren bewijst dat
de code klopt volgens de compiler, niet dat het apparaat doet wat we denken. Met zeventien fixes
op de teller is dit het zwaarste openstaande punt van deze lijst.

De vijf scenario's die het meeste opleveren, in volgorde:

1. **Wifi-wachtwoord met een spatie erin** (C1). De fix waar de meeste klanten last van hadden.
2. **Een SSID met een spatie of leesteken kiezen uit de lijst** (M1, plus de verborgen tweede
   helft van H1: veldbuffers die op de gecodeerde lengte moesten worden gedimensioneerd).
3. **De API-setup pagina openen**, knop in beide toestanden (C4).
4. **Router vijf minuten uitzetten** (C2). In de seriële log loopt het backoff-schema zichtbaar op.
5. **Twee keer snel op "Refresh networks"** tijdens provisioning (H3). Moet een nette melding
   geven, geen reboot.

Let op bij het flashen: door L3 is de configuratie nu die van de gebruikte IDF-versie in plaats
van een gemigreerd bestand van ESP-IDF 6.1.0. Het binaire bestand is daardoor ~40 KB van formaat
veranderd. Bouw met de versie waarmee je in productie gaat — en leg die vast (M9).
