# Wat het apparaat in NVS bewaart

NVS is het stukje flash waar het apparaat dingen bewaart die een herstart moeten overleven:
wifigegevens, API-sleutels, en een handvol tellers. Dit document beschrijft wat er staat, wie het
schrijft, en welke regels gelden als de indeling verandert.

De aanleiding is bevinding **H15**. Bij het uitbrengen van `v0.3.0` veranderde de indeling van de
wifigegevens, en de migratie wiste de oude sleutels. Vooruit ging dat goed. Terug niet.

---

## 1. De indeling

Vier namespaces. Drie ervan zijn van ons.

### `wifi_creds` — de netwerken die het apparaat onthoudt

| Sleutel | Type | Wat het is | Sinds |
| --- | --- | --- | --- |
| `layout` | `u8` | Het schemanummer van deze namespace | v0.3.1 |
| `ssid0`, `ssid1`, `ssid2` | `str` | De naam van het netwerk in dat slot; leeg is een leeg slot | v0.3.0 |
| `pw0`, `pw1`, `pw2` | `str` | Het wachtwoord bij dat slot | v0.3.0 |
| `seq0`, `seq1`, `seq2` | `u32` | Hoe recent dat slot werkte; hoger is recenter, 0 is nooit | v0.3.0 |
| `last_ok` | `i32` | Welk slot het laatst een adres kreeg, of afwezig voor geen | v0.3.0 |
| `ssid`, `password` | `str` | **De brug voor oudere firmware.** Zie hoofdstuk 3 | v0.1, blijft |

### `api_credentials` — de sleutels van Energyboxx

| Sleutel | Type | Wat het is |
| --- | --- | --- |
| `client_id` | `str` | Het client ID |
| `client_secret` | `str` | Het client secret |

Geen schemanummer. Zolang deze twee sleutels blijven wat ze zijn is dat geen probleem; verandert
er ooit iets, dan hoort er eerst een nummer bij. Zie hoofdstuk 4.

### `boot_count` — de teller achter de fabrieksreset

| Sleutel | Type | Wat het is |
| --- | --- | --- |
| `boot_count` | `u32` | Hoeveel stroomonderbrekingen er kort na elkaar waren |

Drie binnen tien seconden wist de wifigegevens en de API-sleutels. Geen schemanummer, en dat is
hier ook niet nodig: één getal met één betekenis.

### `nvs.net80211` — niet van ons

De wifi-driver van Espressif bewaart hier zijn eigen instellingen, waaronder de modus en de
configuratie van het accesspoint. Wij schrijven er niets in en lezen er niets uit. Zie **H11**:
het apparaat hoort zijn modus zelf te zetten en niet uit deze ruimte te erven.

---

## 2. Wat een fabrieksreset wist

Drie stroomonderbrekingen binnen tien seconden, of de resetknop bij het opstarten, wissen:

- alles in `wifi_creds`, inclusief de brug
- alles in `api_credentials`

Wat **niet** wordt gewist: `boot_count` zelf, en alles wat later in een eigen namespace wordt
gezet. Dat is opzet en geen toeval — zie hoofdstuk 5.

---

## 3. De brug voor oudere firmware

`ssid` en `password` zijn de sleutels van `v0.2.x` en eerder, die één netwerk bewaarde. Zij
blijven bestaan, en het netwerk dat het laatst werkte staat er altijd onder.

Dit is er omdat een apparaat dat over de lucht bijwerkt de oude versie in zijn andere slot
houdt. Zakt de nieuwe versie door haar proeftijd, dan start die oude weer op — en die kent
alleen deze twee sleutels.

De brug mag weg zodra geen enkel apparaat nog een `v0.2.x`-image in zijn andere slot heeft.
Eerder niet.

---

## 4. Regels voor wie de indeling verandert

Een apparaat kan lang offline staan en daarna in één sprong van `v0.3.1` naar `v0.4.7` gaan,
zonder ooit iets ertussenin te draaien. Elke migratie moet daar tegen kunnen.

**Regel 1 — nummer per namespace.** Elke namespace waarvan de indeling ooit verandert krijgt een
`layout`-sleutel. Het nummer zegt welke indeling er staat, niet welke firmware hem schreef.

**Regel 2 — een keten van stappen, geen sprong.** Elke stap gaat van *n* naar *n+1* en niets
anders. Bij het opstarten leest de firmware het nummer en loopt alle stappen daarboven af, op
volgorde. Een sprong van 1 naar 4 is dan hetzelfde als drie keer één stap, en er is geen enkel
pad dat nooit gelopen is.

**Regel 3 — elke stap zet zijn eigen nummer, en commit dat.** Valt de stroom uit halverwege stap
drie, dan staat er nog steeds 2 en begint stap drie opnieuw. Een stap moet daarom twee keer
uitgevoerd kunnen worden zonder schade.

**Regel 4 — voeg toe, verwijder nooit.** Een sleutel die oudere firmware leest blijft staan, ook
als de nieuwe firmware hem niet meer gebruikt. Verwijderen mag pas als geen terugvalslot die
firmware meer kan bevatten. Dit is H15, als regel.

**Regel 5 — hergebruik nooit een naam.** Een sleutel die ooit iets anders betekende krijgt een
nieuwe naam. Oudere firmware leest die naam nog, met de oude betekenis, en een `u32` die
plotseling iets anders telt is erger dan een sleutel die ontbreekt.

**Regel 6 — een hoger nummer dan wij kennen is een terugval.** Ziet de firmware `layout = 4`
terwijl zij tot 2 kan, dan draait zij op een apparaat dat nieuwere firmware heeft gehad. Zij
migreert dan **niet** naar beneden. Zij logt het luid en gebruikt de sleutels die zij kent — wat
werkt, dankzij regel 4.

---

## 5. Wat een migratie nooit mag doen

- **Een apparaat onbereikbaar maken.** Na elke migratie moet het apparaat een netwerk kunnen
  vinden, want zonder netwerk is er geen weg meer naar binnen behalve een kabel.
- **Van de fabrieksreset afhangen.** Een gebruiker die iets probeert te repareren door alles te
  wissen, mag daarmee nooit in dezelfde toestand terugkomen. Zie `docs/OTA.md`, "De lus die
  nooit mag ontstaan".
- **Stilzwijgend falen.** Een migratie die niet lukt hoort in de log te staan, en de oude
  toestand hoort te blijven staan zodat de volgende start het opnieuw kan proberen.

---

## 6. Recept voor de volgende migratie

1. Verhoog `LAYOUT_*` in de betreffende module met één.
2. Schrijf een stap die van het vorige nummer naar het nieuwe gaat, en niets anders aanneemt
   over waar het apparaat vandaan komt.
3. Zet het nieuwe nummer in dezelfde commit als de wijziging zelf.
4. Beproef de stap **twee keer achter elkaar** op hardware: de tweede keer hoort niets te doen.
5. Beproef de weg **terug**: flash de vorige uitgegeven versie over het apparaat heen en kijk of
   die nog een netwerk vindt. Dat is wat een terugval met NVS doet — de app wisselt, de opslag
   blijft.
6. Beschrijf de nieuwe sleutels in hoofdstuk 1 van dit document.

Stap 5 is de stap die bij H15 ontbrak.
