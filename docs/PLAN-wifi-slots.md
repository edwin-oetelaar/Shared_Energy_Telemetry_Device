# Drie wifi-netwerken in plaats van één

## 1. Doel

Het apparaat onthoudt nu één wifi-netwerk. Wie het meeneemt — naar een andere locatie, naar
de buren, naar het bureau op het werk — moet het opnieuw instellen via het portaal.

Dit document beschrijft de weg naar **drie onthouden netwerken**, waarbij het apparaat bij
het opstarten zelf uitzoekt op welk van de drie het staat.

De aanleiding is het ontwikkelwerk: het apparaat verhuist tussen thuis, kantoor en een
telefoon-hotspot. Maar het is ook voor de bewoners nuttig. Wie zijn router vervangt, houdt
het oude netwerk in een tweede slot staan totdat het nieuwe bewezen werkt.

Elke fase hieronder eindigt met een apparaat dat werkt en dat u kunt aanzetten.

## 2. Wat er nu staat

| Onderdeel | Nu | Bestand |
| --- | --- | --- |
| Opslag | één paar, `ssid` en `password` in de NVS-ruimte `wifi_creds` | `main/src/wifi_storage.c` |
| Laden bij het opstarten | één keer laden, één keer verbinden | `main/main.c:475` |
| Verbinden | één `wifi_config_t`, daarna `esp_wifi_connect()` | `main/src/wifi_provisioning.c:592` |
| Opnieuw proberen | acht rijen, van 0,5 s tot 5 minuten, laatste rij herhaalt | `main/src/wifi_provisioning.c:222` |
| Scannen | bestaat al, voor de netwerklijst in het portaal | `main/src/wifi_provisioning.c:647` |
| Afwijsreden | wordt gelogd, maar er wordt niets mee gedaan | `main/src/wifi_provisioning.c:313` |

Die laatste regel is bevinding **M10** in `docs/REVIEW.md`, en die staat nog open. Ze is
hier geen bijzaak — zie besluit 4.

## 3. Het ontwerp in één gedachte

De verleiding is om een lus om het bestaande schema te leggen: probeer slot 0, mislukt,
probeer slot 1, mislukt, probeer slot 2. Dat wordt traag. Het schema loopt op tot 5 minuten
per stap, en elke mislukte poging kost een verbindingstime-out. Drie slots maal acht rijen
is een apparaat dat er twintig minuten over doet om te ontdekken dat het op uw bureau staat.

De uitweg is dat er **twee verschillende vragen** in het spel zijn, en dat die niet in
dezelfde tabel horen:

| Vraag | Wordt beantwoord door | Kost |
| --- | --- | --- |
| Welk van mijn netwerken is hier? | één scan | ongeveer twee seconden, één keer |
| Hoe geduldig ben ik als er geen enkel netwerk is? | de bestaande backoff-tabel, ongewijzigd | zoals nu |

Scannen is wat wifi al doet, en het apparaat kan het al. Een telefoon-hotspot die uit staat,
is dan simpelweg geen kandidaat — in plaats van een time-out die u elke start opnieuw betaalt.

De keuze wordt daarmee:

1. Staat het laatst geslaagde netwerk in de scan? Neem dat. Een apparaat dat blijft staan,
   verandert dus niets aan zijn gedrag.
2. Anders: van de netwerken die zichtbaar zijn **en** in een slot staan, de sterkste.
3. Staat er geen enkele in de scan? Loop de gevulde slots één keer langs — een verborgen
   netwerk zendt zijn naam niet uit en komt nooit in een scan. Daarna de backoff-tabel voor
   de hele ronde.

## 4. Beslissingen

#### Besluit 1 — Drie slots, niet meer en niet minder

Twee is te weinig voor thuis, kantoor en hotspot. Meer dan drie kost NVS-ruimte en maakt het
statusscherm onleesbaar, zonder dat iemand een vierde locatie noemt.

**Voorstel:** drie. Het aantal staat als één `#define` in `wifi_storage.h`, zodat het later
één cijfer is en geen verbouwing.

#### Besluit 2 — Wat er in NVS komt, en hoe bestaande apparaten meekomen

| Sleutel | Inhoud |
| --- | --- |
| `ssid0`, `pw0` … `ssid2`, `pw2` | de drie paren; een leeg `ssidN` betekent een leeg slot |
| `last_ok` | het slot dat het laatst een IP-adres opleverde |
| `ok_seq0` … `ok_seq2` | een oplopend nummer per slot, gezet bij elke geslaagde verbinding |

`ok_seq` is er voor besluit 3: het zegt welk slot het langst niet is gelukt, zonder dat er
een klok bij hoeft te komen.

**Migratie.** Bij de eerste start met de nieuwe firmware leest `wifi_storage_init()` de oude
sleutels `ssid` en `password`, schrijft ze naar slot 0, en wist de oude sleutels pas ná een
geslaagde commit. Gaat dat mis, dan staan de oude sleutels er nog en probeert het de volgende
start opnieuw.

Er staat op dit moment geen apparaat bij een bewoner — alles ligt op de werkbank, en een bord
dat zijn credentials kwijtraakt is hier een minuut werk. De volgorde hierboven staat er dus
niet omdat het nu spannend is, maar omdat dezelfde code straks draait op apparaten waar
niemand bij kan. Fase 0 gaat er alleen over, zodat ze één keer goed staat voordat de rest
erop bouwt.

#### Besluit 3 — Welk slot het portaal overschrijft

Het portaal vraagt om een netwerk en een wachtwoord. Het vraagt **niet** om een slotnummer:
dit is een apparaat op een vensterbank, niet een router-configuratiescherm.

De regel, in volgorde:

| Situatie | Waar het naartoe gaat |
| --- | --- |
| Er is een slot met dezelfde SSID | dat slot, overschreven |
| Er is een leeg slot | het eerste lege slot |
| Alle drie gevuld, geen match | het slot met het laagste `ok_seq` — het langst niet gelukt |

**Voorstel:** deze drie regels, in deze volgorde, als tabel in de code.

#### Besluit 4 — M10 hoort hierbij, niet erna

M10 zegt: bij een 4-way handshake timeout weten we dat het wachtwoord fout is, en blijft het
schema het toch proberen. Met één slot is dat verspilling. Met drie slots is het erger — een
fout slot houdt de andere twee tegen.

De afwijsreden wordt daarmee de stuurknop voor de slotkeuze. Wat er nu alleen gelogd wordt,
gaat in een tabel:

| Reden | Betekenis | Wat het slot doet |
| --- | --- | --- |
| 4-way handshake timeout, auth expired, auth fail | het wachtwoord klopt niet | slot afkeuren voor deze ronde, meteen door naar het volgende |
| no AP found | het netwerk is hier niet | slot overslaan voor deze ronde |
| assoc fail, assoc leave, en de rest | tijdelijk of onbekend | zelfde slot, via de backoff-tabel |

De getallen achter deze redenen staan in `esp_wifi_types.h`. Ze worden bij het bouwen tegen
v6.1 gecontroleerd en niet uit het hoofd overgenomen.

**Voorstel:** M10 als fase 1 van dit plan afwerken, vóór er een tweede slot in gebruik komt.

#### Besluit 5 — Geen netwerk in de firmware

Overwogen en afgewezen: één netwerk hardcoded in de code, alleen voor ontwikkeling.

De repository is publiek en de updater haalt firmware van GitHub Releases
(`main/src/updater.c:30`). Elke release is dus een publieke download, en een SSID met
wachtwoord staat daar als leesbare tekst in. Het geheim wordt niet onderschept — het wordt
gepubliceerd. Dat TLS het verkeer versleutelt doet daar niets aan, want dit geheim reist niet.

Daar komt bij dat het niet om het netwerk van een klant zou gaan maar om dat van ons, en dat
een SSID via wardriving-databases aan een adres te koppelen is. En het is een sleutel die
niemand meer kan roteren: zodra hij in uitgeleverde firmware zit, betekent dat wachtwoord
veranderen dat die apparaten hun terugval kwijt zijn.

Met drie slots is het ook niet nodig. Een testbord wordt één keer door het portaal gehaald —
wat voor de API-sleutels toch al moet — en kent daarna thuis, kantoor en hotspot.

**Voorstel:** geen netwerk in de firmware. Moet het er ooit toch in, dan achter een
Kconfig-optie die standaard uit staat, plus een CI-stap die de gebouwde `.bin` op die SSID
doorzoekt en de build laat falen. Een controle, geen belofte.

## 5. De fasen

### Fase 0 — Drie slots in de opslag, gedrag ongewijzigd

Doel: de opslag kan drie paren aan, en een bestaand apparaat merkt er niets van.

1. `wifi_storage` krijgt een slotnummer in zijn interface, en `WIFI_SLOT_COUNT` in de header.
2. De migratie van besluit 2, met de oude sleutels die pas na een geslaagde commit weg gaan.
3. Alles daarboven blijft slot 0 gebruiken; er verandert niets aan het verbinden.
4. Host-tests voor de migratie en voor de keuzeregel van besluit 3.

**Klaar als:** een apparaat dat `v0.2.1` draaide en over de lucht bijwerkt, verbindt na de
update met hetzelfde netwerk, zonder dat iemand iets doet.

> **Uitgevoerd op 2026-09-02.** De opslag houdt drie slots, en een bord dat al was ingesteld
> merkt er niets van. De migratie is niet uit de log afgelezen maar uit de NVS-partitie zelf,
> die met `esptool read-flash 0x012000 0x6000` van het bord is gehaald en met
> `nvs_tool.py` uit ESP-IDF is uitgelezen:
>
> ```
> 093. Erased , ... | ssid:     Size=7,  CRC32=7892034c
> 095. Erased , ... | password: Size=11, CRC32=963caac7
> 047. Written, ... | ssid0:    Size=7,  CRC32=7892034c
> 049. Written, ... | pw0:      Size=11, CRC32=963caac7
> 051. Written, ... | seq0:     1
> 052. Written, ... | last_ok:  0
> ```
>
> De CRC's van `ssid0` en `pw0` zijn gelijk aan die van de oude `ssid` en `password`, dus de
> waarden zijn byte voor byte overgenomen; de oude sleutels staan op *Erased*. Dat is
> sterker bewijs dan de logregel, want het toont de inhoud en niet ons eigen verslag ervan.
>
> **De schrijfkant, dezelfde dag.** Het eerste bord kwam na de migratie niet online omdat het
> netwerk `OETELX` niet in de lucht was (`reason=201`, no AP found) — precies de klacht waar
> dit plan uit voortkomt. Na het instellen van `CreateLAB` via het portaal:
>
> ```
> [ 2.01] wifi_prov: Connecting to SSID: CreateLAB
> [ 3.82] wifi_prov: Got IP: 192.168.1.253
> [ 3.82] wifi_storage: Saved 'CreateLAB' in slot 0
> [ 6.23] energyboxx_api: Status = 200
> ```
>
> In de NVS staat de oude `ssid0` (`OETELX`, 7 bytes) op *Erased* en de nieuwe (`CreateLAB`,
> 10 bytes) op *Written*. `seq0` bleef 1 en `last_ok` bleef 0: slot 0 was al het laatst
> geslaagde slot, dus `wifi_storage_note_success()` schreef niets. Dat is de bedoeling —
> een apparaat dat de hele dag opnieuw verbindt, hoort niet de hele dag naar flash te
> schrijven.
>
> Tussen twee starts staat er ook maar één `ssid0` met deze waarde in de partitie, terwijl
> het opslaan bij elke `Got IP` gebeurt. NVS slaat een schrijfactie met dezelfde inhoud dus
> zelf over. Dat is gemeten, niet aangenomen.
>
> De regels van besluit 3 zijn met tien gevallen op de host getest (`test/test_wifi_slots.c`)
> en nog niet aangesloten; dat is fase 3.

### Fase 1 — De afwijsreden gaat iets betekenen (M10)

Doel: een fout wachtwoord wordt niet eindeloos herhaald.

1. De tabel van besluit 4 in `wifi_provisioning.c`.
2. Een afgekeurd slot stopt het backoff-schema in plaats van het te voeden.
3. Het statusscherm zegt dat het wachtwoord is afgewezen, zodat er iets te doen valt.

**Klaar als:** provisioning met een fout wachtwoord één poging kost, geen serie van vijf
minuten, en het portaal open blijft staan voor een tweede kans. Vink **M10** af in
`docs/REVIEW.md`.

> **Uitgevoerd op 2026-09-02.** De afwijsreden gaat door een tabel met drie betekenissen, en
> de toestandsmachine van de verbinding heeft er een vijfde toestand `rejected` bij. Een fout
> wachtwoord kost nu één poging in plaats van een reeks die oploopt tot een uur, het portaal
> opent na 6,2 s in plaats van 32 s, en het scherm zegt "Wachtwoord klopt niet" in plaats van
> "Geen verbinding". Zie de zesde ronde in `docs/REVIEW.md`.
>
> `DISCONNECT_ABSENT` — het netwerk is er niet — deelt voorlopig de gewone backoff, want met
> één slot in gebruik is er niets anders om te proberen. Fase 2 geeft die betekenis zijn eigen
> antwoord.
>
> **Onderweg gevonden:** `wifi_prov_init()` zette de wifi-modus nooit, waardoor het apparaat
> opstartte in de modus die de driver in zijn eigen NVS had onthouden. Op elk bord waarvan het
> portaal ooit open had gestaan, kwam het open accesspoint dus bij elke start omhoog. Dat is
> bevinding **H11** en is in dezelfde beurt opgelost.

### Fase 2 — Kiezen op basis van een scan

Doel: het apparaat vindt zelf het netwerk waar het staat.

1. Een scan vóór de eerste verbindingspoging, en na elke ronde waarin geen slot werkte.
2. De keuzeregels uit hoofdstuk 3, als tabel.
3. `last_ok` en `ok_seq` bijhouden, en alleen schrijven als ze veranderen — NVS slijt.
4. Niet scannen terwijl er iemand in het portaal staat: een scan verbreekt hun verbinding
   met het accesspoint van het apparaat.

**Klaar als:** een apparaat met drie gevulde slots op elk van de drie locaties binnen een
halve minuut online is, en een uitgezette hotspot geen vertraging kost.

> **Uitgevoerd op 2026-09-02.** Een ronde is één gang langs de opgeslagen netwerken, op
> volgorde van een scan. Binnen een ronde wacht niets: een netwerk dat er niet is of dat het
> wachtwoord weigert, geeft meteen door aan het volgende. Wachten gebeurt tússen rondes, en
> dat is de bestaande backoff-tabel die zijn oude werk doet op de ronde als geheel.
>
> Beproefd met drie slots, waarvan alleen slot 2 in de lucht was:
>
> ```
> [ 2.01] wifi_prov: 3 stored networks, last success in slot -1
> [ 4.42] wifi_prov: Scan saw 31 networks, 1 of them ours
> [ 4.42] wifi_prov: Round: slot 2, network 1 of 3      <-- de scan zet slot 2 vooraan
> [ 8.63] wifi_prov: STA disconnected, reason=15: the password is wrong
> [ 8.63] wifi_prov: Round: slot 0, network 2 of 3
> [11.03] wifi_prov: STA disconnected, reason=201: the network is not here
> [11.03] wifi_prov: Round: slot 1, network 3 of 3
> [13.44] wifi_prov: STA disconnected, reason=201: the network is not here
> [13.44] wifi_prov: 1 of 3 stored networks refused our key; the rest were not here
> [13.44] wifi_prov: Starting provisioning AP: SETD_Provisioning
> ```
>
> Slot 2 stond vooraan omdat de scan het zag, niet omdat het slot 2 was. De hele ronde kostte
> negen seconden.
>
> **Twee dingen anders dan het plan zei.**
>
> Ten eerste: met één gevuld slot wordt er **niet** gescand. Er valt dan niets te kiezen, en de
> scan was twee seconden vertraging voor een antwoord dat we al hadden — gemeten: 6,2 s tot
> online mét scan, 3,8 s zonder. Dat is het gewone geval: de meeste apparaten staan op één plek
> en kennen één netwerk.
>
> Ten tweede: het oordeel aan het eind van een ronde weegt de mengeling. Een netwerk dat er
> niet is, zegt niets over onze wachtwoorden en telt dus niet mee — niet vóór en niet tegen.
> Zonder die nuance leverde één geweigerd wachtwoord tussen twee afwezige netwerken "Geen
> verbinding" op, terwijl er wél iets te doen viel. Nu zegt het scherm "Wachtwoord klopt niet"
> zodra er iets geweigerd heeft en niets op een manier faalde die we niet kunnen lezen.
>
> Van de scan worden alleen de netwerken bewaard die van ons zijn: op de werkbank leverde één
> scan er 31 op, waarvan 1 de onze. Zo kan een druk flatgebouw ons netwerk niet voorbij het
> einde van een buffer duwen.
>
> **Nog niet aangesloten:** het portaal schrijft nog altijd naar slot 0. De keuzeregels van
> besluit 3 wachten op fase 3, en tot dan is slot 2 alleen met een gegenereerde NVS te vullen.
> De README beschrijft daarom nog één netwerk, want meer kan een gebruiker nog niet instellen.

### Fase 3 — Zichtbaar maken wat het apparaat weet

Doel: "waarom zit hij op het verkeerde netwerk?" is te beantwoorden zonder seriële kabel.

1. Het statusscherm toont welk slot in gebruik is: `Wifi  <ssid>  (2 van 3)`.
2. Het portaal toont de drie slots en welk slot een nieuwe invoer zou overschrijven.
3. De log zegt bij elke keuze waarom: gekozen slot, reden, sterkte.

**Klaar als:** iemand voor het apparaat kan zien welk van de drie netwerken het gebruikt.

> **Uitgevoerd op 2026-09-02.** Het portaal schrijft nu naar het slot dat besluit 3 aanwijst in
> plaats van altijd naar slot 0; daarmee zijn drie netwerken voor het eerst met de hand in te
> stellen. Het statusscherm zet er `(2 van 3)` achter, en laat dat weg als er maar één netwerk
> is, want "1 van 1" zegt niemand iets.
>
> Het portaal toont wat het apparaat onthoudt en waar een nieuwe invoer terechtkomt. Dat komt
> van een nieuw eindpunt `/networks`, dat dezelfde `wifi_slots_choose_for ()` gebruikt als de
> firmware: de regel staat niet ook nog eens in de pagina, want een regel op twee plekken gaat
> ooit met zichzelf in tegenspraak.
>
> De log zegt nu waarom, en onderscheidt drie dingen die makkelijk door elkaar lopen:
>
> ```
> Plan 1: slot 2 'CreateLAB', -47 dBm
> Plan 2: slot 0 'ZZ-NietAanwezig-A', not in the scan
> Plan 3: slot 1 'ZZ-NietAanwezig-B', not in the scan
> ```
>
> en met één netwerk, waar niet voor gescand wordt:
>
> ```
> Plan 1: slot 0 'CreateLAB', no scan was needed
> ```
>
> **Een fout van fase 1 hersteld.** Het portaal kijkt of de toestand `failed` is om te weten
> dat het misging. De nieuwe toestand `rejected` viel in `default: "unknown"`, waardoor de
> pagina na een fout wachtwoord bleef pollen op iets wat ze niet kende — precies het geval
> waarvoor fase 1 was bedoeld. `rejected` levert nu ook `failed`, en dat is de tak die zegt
> "Could not connect. Check password."
>
> **Op hardware bevestigd op 2026-09-02**, nadat de storing van die avond was verholpen. Edwin
> voerde `OETELX` in als tweede netwerk; het ging naar slot 1 — het eerste lege — precies zoals
> het portaal vooraf zei. Zie de achtste ronde in `docs/REVIEW.md`. Het apparaat onthoudt nu
> twee netwerken, en het statusscherm toont `Wifi OETELX (2 van 2)` — door Edwin gelezen van
> het scherm zelf. Daarmee is voldaan aan "klaar als": iemand voor het apparaat kan zien welk
> van de netwerken het gebruikt.

### Fase 4 — Een slot wissen

Doel: een netwerk waar u nooit meer komt, kan weg zonder alles te wissen.

Dit is bewust de laatste fase. Zonder deze knop is de ergste uitkomst dat een oud slot een
keer wordt overschreven volgens besluit 3, en dat is geen ramp.

1. Het portaal toont per onthouden netwerk een knop **Forget**, naast de lijst die fase 3 er al
   zette.
2. Een netwerk wordt aangewezen met zijn naam, niet met een slotnummer. Een nummer op een
   pagina die een minuut geleden is getekend, kan intussen naar iets anders wijzen — en een
   verkeerd nummer wist het verkeerde netwerk.
3. Het netwerk dat op dit moment in gebruik is, mag ook weg. Het portaal zegt eerst wat dat
   betekent: de verbinding blijft staan tot het apparaat herstart, en daarna komt het hier niet
   op terug. Weigeren zou betekenen dat iemand die er speciaal voor is gaan staan het niet kan
   opruimen.
4. Wissen gaat door `wifi_prov_forget ()` en niet rechtstreeks naar de opslag, want
   `wifi_provisioning` houdt een kopie van de slots plus twee antwoorden die eruit volgen: welk
   slot in gebruik is en welk slot het laatst werkte. Een slot dat achter zijn rug om leeggaat,
   laat het scherm een netwerk noemen dat er niet meer is.

**Klaar als:** een netwerk uit het portaal verdwijnt, het apparaat het daarna niet meer
probeert, en de rest blijft staan.

> **Uitgevoerd op 2026-09-02.** Eindpunt `POST /forget`, een knop per netwerk in het portaal, en
> `wifi_slots_find ()` met vijf host-tests — waaronder de lege naam, die anders een leeg slot
> zou aanwijzen en dus het verkeerde netwerk zou wissen. `wifi_storage_clear_slot ()` wist nu
> ook `last_ok` als dat naar het gewiste slot wees.
>
> De netwerknaam gaat in de pagina via `textContent` en nooit via `innerHTML`: een SSID wordt
> gekozen door wie het accesspoint bezit, en één met een tag erin mag deze pagina niet kunnen
> schrijven. Dat is dezelfde zorg als bij **M1**, nu aan de kant van de browser.
>
> **Nog niet op hardware bevestigd:** het indrukken van de knop. Daarvoor is een browser op het
> portaal-AP nodig en dat kan alleen een mens; WSL2 zit achter NAT en komt niet op dat netwerk.

## 6. Risico's

| Risico | Weging |
| --- | --- |
| De migratie in fase 0 verliest de bestaande credentials | Nu goedkoop — alles ligt op de werkbank — maar dezelfde code draait straks buiten bereik. De oude sleutels blijven staan tot de nieuwe zijn vastgelegd, en fase 0 is op hardware beproefd met een apparaat dat al ingesteld was |
| Een scan kost tijd bij elke koude start | Ongeveer twee seconden, en alleen als het laatst geslaagde netwerk er niet is |
| Een verborgen netwerk komt niet in de scan | Daarom loopt stap 3 van de keuze de gevulde slots alsnog langs |
| Een scan tijdens provisioning verbreekt het portaal | Fase 2, punt 4: niet scannen zolang het portaal open is |
| Drie netwerken in platte tekst in NVS in plaats van één | Verzwaart **H5** een beetje. Geen reden om het niet te doen, wel iets om te weten zolang H5 uitstaat |

## 7. Samenhang met de review

| Bevinding | Verband |
| --- | --- |
| **M10** | Wordt in fase 1 opgelost; dit plan bouwt erop verder |
| **H10** | Een afgekeurde sleutel mag geen werkend apparaat zonder credentials achterlaten. Met drie slots geldt dat per slot, en dat is meegenomen in besluit 3 |
| **H5** | Drie netwerken in plaats van één maken flash-encryptie iets waardevoller |
| **C5** | Niet geraakt. Het portaal blijft wat het is |

## 8. Volgorde van beslissen

Besluit 1 tot en met 5 kunnen nu. Alleen besluit 2 is later moeilijk terug te draaien: de
sleutelnamen in NVS staan straks op apparaten die bij mensen thuis hangen, en een tweede
migratie is een tweede kans om credentials kwijt te raken.
