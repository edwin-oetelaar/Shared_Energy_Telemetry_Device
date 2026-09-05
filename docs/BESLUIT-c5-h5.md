# Twee besluiten vóór de eerste serie

Dit stuk is bedoeld voor het gesprek met de klant. Het beschrijft twee openstaande punten uit de
firmware-review, wat zij betekenen, wat het kost om ze nu te doen, en wat het kost om ze later
alsnog te willen.

Beide gaan over beveiliging. Geen van beide is een fout in de code: het zijn keuzes die nog niet
gemaakt zijn.

---

## 1. In het kort

| | **C5** — het instelnetwerk staat open | **H5** — de flash ligt open |
| --- | --- | --- |
| Wat er gebeurt | Tijdens het instellen maakt het apparaat een **onbeveiligd** wifinetwerk. Het wifi-wachtwoord van de bewoner en de API-sleutels gaan er onversleuteld overheen | Wifi-wachtwoord en API-sleutels staan als leesbare tekst in de flash. Wie het apparaat vijf minuten in handen heeft, leest ze uit met een kabel |
| Wie er iets aan heeft | Iemand binnen radiobereik, op het moment van instellen | Iemand die het apparaat in handen krijgt |
| Wat er te halen valt | Het wifi-wachtwoord van het huis | Hetzelfde, plus de API-sleutels |
| Achteraf te repareren? | **Ja**, met een gewone update over de lucht | **Nee.** Dit moet in de fabriek gebeuren, vóór het apparaat de deur uit gaat |
| Kosten om het nu te doen | Ongeveer een dag, inclusief beproeven | Enkele dagen, plus een blijvende verplichting |

Dat verschil in de laatste twee regels is waar dit stuk over gaat. **C5 kan altijd nog. H5 niet.**

---

## 2. C5 — het instelnetwerk staat open

### Wat er nu gebeurt

Als iemand het apparaat instelt, zet het een eigen wifinetwerk aan met de naam
`SETD_Provisioning`. Dat netwerk heeft **geen wachtwoord**, en de pagina waarop de bewoner zijn
gegevens intypt gaat over gewoon HTTP.

Wie op dat moment binnen bereik is met een laptop, kan meelezen: het wifi-wachtwoord van het
huis, en de sleutels van Energyboxx. Diezelfde persoon kan ook zelf op het netwerk inloggen en
het apparaat aan zijn eigen accesspoint koppelen — er zit geen enkele drempel op de pagina.

### Hoe groot is dat

Klein, en kleiner dan het was. Sinds bevinding **H11** komt dat netwerk alleen nog omhoog als
iemand erom vraagt; daarvóór stond het bij elke start aan, de hele dag. Nu is het venster een
paar minuten, bij het instellen.

Maar het venster keert wél terug. Sinds het apparaat drie netwerken onthoudt, is het portaal ook
de plek waar iemand een netwerk toevoegt of vergeet. Dat is geen eenmalige handeling meer.

En wat er te halen valt is niet van ons maar van de bewoner: het wachtwoord van zijn eigen wifi.
Dat weegt zwaarder dan een apparaat dat verkeerd wordt ingesteld.

### Wat het kost om het te repareren

Weinig, en dat komt doordat het apparaat een scherm heeft.

Het instelnetwerk krijgt WPA2 met een wachtwoord dat per apparaat verschilt. Dat wachtwoord komt
op het scherm te staan, naast de QR-code die er al is. De QR-code kan het wachtwoord meedragen —
de vorm die telefoons begrijpen is `WIFI:S:naam;T:WPA;P:wachtwoord;;` in plaats van het huidige
`T:nopass` — dus wie de code scant komt er zonder iets te typen op, precies zoals nu.

Daarmee wordt "je moet bij het apparaat staan" de toegangsdrempel. Dat is voor het instellen van
een apparaat aan de muur precies de goede drempel.

| Werk | Schatting |
| --- | --- |
| Wachtwoord per apparaat, opgeslagen en getoond | een halve dag |
| WPA2 op het netwerk, QR-code aanpassen | een paar uur |
| Beproeven op hardware, met een telefoon | een paar uur |

**Achteraf kan het ook.** Een apparaat dat al bij iemand hangt krijgt dit met een gewone update
over de lucht. Er is dus geen deur die dichtgaat.

---

## 3. H5 — de flash ligt open

Dit zijn drie dingen die vaak in één adem worden genoemd, maar afzonderlijk te kiezen zijn.

### 3a. Flash encryption — de inhoud onleesbaar maken

De inhoud van de flash wordt versleuteld met een sleutel die in de chip zelf zit en er niet uit
te lezen is. Zonder dat kan iedereen met een USB-kabel en vijf minuten het wifi-wachtwoord van de
bewoner uitlezen.

Dat is niet theoretisch. Een apparaat dat kapot teruggestuurd wordt, een apparaat dat bij een
verhuizing achterblijft, een apparaat dat tweedehands wordt doorverkocht — in alle drie de
gevallen gaat het wachtwoord van iemands huisnetwerk mee.

### 3b. NVS encryption — hetzelfde voor de opgeslagen gegevens

Bouwt voort op flash encryption en versleutelt specifiek het deel waar de wifigegevens en de
API-sleutels staan. **De partitietabel houdt hier al ruimte voor vrij** (`nvs_keys`), dus dat werk
is al gedaan.

### 3c. Secure boot — alleen onze eigen firmware draait

Het apparaat start alleen firmware die door ons is ondertekend. Dat beschermt twee dingen.

Ten eerste kan niemand er eigen software op zetten en hem daarna teruggeven alsof er niets aan de
hand is.

Ten tweede, en dat weegt zwaarder: het apparaat werkt zichzelf bij vanaf GitHub. Het controleert
nu wel het **certificaat van GitHub**, maar geen handtekening onder het bestand zelf. Wie toegang
krijgt tot dat account kan dus software draaien op elk apparaat in het veld. Secure boot maakt van
"wat GitHub aanbiedt" ook "wat wij hebben ondertekend".

### Wat het kost

Hier zit meer aan vast dan werk.

| Kostenpost | Wat het inhoudt |
| --- | --- |
| **Onomkeerbaar** | Het wordt in de chip gebrand. Terugdraaien kan niet, en **aanzetten kan niet over de lucht** — het moet met een kabel, in de fabriek, vóór uitlevering |
| **Sleutelbeheer** | Er komt een ondertekensleutel die de hele levensduur van het product moet blijven bestaan. Kwijt betekent: nooit meer een update. Uitgelekt betekent: secure boot is waardeloos. Alleen voor secure boot — flash encryption en NVS-encryptie maken hun sleutel op het apparaat zelf. Zie [SLEUTELS.md](SLEUTELS.md) |
| **De bouwstraat** | Die sleutel moet in de bouwomgeving staan om releases te ondertekenen. Dat is een nieuwe plek waar een geheim ligt |
| **Onderhoud** | Een apparaat dat terugkomt is lastiger te onderzoeken: opnieuw flashen via de kabel gaat in de strengste stand niet meer |
| **Ontwikkeltijd** | Enkele dagen, inclusief het beproeven — en beproeven betekent hier dat een fout een bord onherstelbaar kan maken |

### Wat "later" kost

Elk apparaat dat al is uitgeleverd moet terug, of iemand moet erheen met een kabel. Er is geen
andere weg: dit is precies het stuk dat een update over de lucht niet kan aanzetten.

---

## 4. Wat er al klaarligt

Er is bij het ontwerp al rekening mee gehouden, en dat verlaagt de kosten van "nu doen":

- De partitietabel staat op `0x11000` in plaats van de standaard `0x8000`. Dat is met opzet: het
  laat de bootloader zijn volledige 64 kB plus 4 kB voor een handtekening. **Die keuze is al
  gemaakt en kan niet meer veranderen zodra apparaten zijn uitgeleverd** — dat deel van de deur
  staat dus al goed.
- De partitie `nvs_keys` staat er al in, gemarkeerd als versleuteld.
- Het apparaat heeft een scherm en toont al een QR-code, wat de oplossing voor C5 goedkoop maakt.

---

## 5. De afweging zoals de klant hem stelt

> "Het risico wordt nu als erg laag ingeschat, dus het krijgt geen prioriteit. Als het weinig tijd
> en geld kost, is het een optie."

Dat is een redelijke lijn, en voor de twee punten valt hij verschillend uit.

**Voor C5 klopt hij en pleit hij vóór doen.** Het risico is laag, maar de reparatie is ook laag —
ongeveer een dag — en zij maakt het instellen niet omslachtiger, omdat de QR-code het wachtwoord
meedraagt. Weinig geld voor een echte verbetering. En mocht het toch later moeten: dat kan.

**Voor H5 klopt de lijn niet helemaal, en dat is de kern van dit stuk.** "Het risico is nu laag"
gaat over de vloot van vandaag: een handvol apparaten bij de makers zelf. Het besluit gaat over
apparaten die er nog niet zijn, en over een deur die dichtgaat op het moment dat de eerste serie
de fabriek verlaat.

De vraag is dus niet "hoe groot is het risico nu", maar: **wat kost het als we hier over twee jaar
anders over denken?** Het antwoord is: alle apparaten terughalen. Dat is de prijs van uitstellen,
en die is bekend vóórdat het besluit valt.

Daar staat een echte kostenpost tegenover, en die is niet de ontwikkeltijd maar het
**sleutelbeheer**. Een ondertekensleutel die twintig jaar mee moet, is een verplichting die een
klein bedrijf serieus moet willen dragen. Die kwijtraken is erger dan hem nooit gehad hebben:
dan staan er apparaten in huizen die nooit meer een update krijgen.

---

## 6. Aanbeveling

**C5: doen, en niet als apart project.** Ongeveer een dag werk, het maakt het instellen niet
lastiger, en het beschermt iets dat van de bewoner is. Als het niet nu gebeurt, kan het altijd
nog — dus dit is geen besluit dat vastloopt.

**H5: nu beslissen, ook als de uitkomst "niet doen" is.** En als het besluit "wel doen" wordt,
dan in deze volgorde, want de kosten liggen niet waar men ze verwacht:

1. **Eerst het sleutelbeheer regelen.** De werkwijze staat in [SLEUTELS.md](SLEUTELS.md). Waar staat de sleutel, wie kan erbij, wat gebeurt er als
   die persoon er niet meer is, en hoe wordt hij bewaard buiten één laptop. Zonder antwoord op
   die vragen is de rest zinloos.
2. **Dan flash encryption en NVS encryption.** Die beschermen de gegevens van de bewoner en
   vragen geen ondertekensleutel — dus zij kunnen ook zonder stap 3.
3. **Dan pas secure boot.** Dat is het stuk dat de ondertekensleutel nodig heeft en dat het
   onderhoud het lastigst maakt.

Die volgorde geeft ook een tussenweg: **stap 2 zonder stap 3.** Dan zijn de gegevens van de
bewoner beschermd tegen iemand die het apparaat in handen krijgt, zonder dat er een sleutel
ontstaat die twintig jaar bewaakt moet worden. De firmware zelf blijft dan te vervangen door wie
er fysiek bij kan, en de update-keten leunt op de beveiliging van het GitHub-account.

Voor een pilot met een handvol huizen is dat een verdedigbare middenweg. Voor een product dat in
serie gaat is het dat op den duur niet.

---

## 7. Het besluit, genomen op 2026-09-05

**C5 is gedaan.** Het instelnetwerk staat op WPA2 met een wachtwoord per apparaat, dat op het
scherm staat en in de QR-code zit. Het kost de gebruiker niets: wie scant komt er zonder typen
op, net als bij het open netwerk dat het verving.

**H5 wordt niet gedaan.** Geen flash encryption, geen secure boot. De reden is niet gemak maar een
uitgangspunt: **de apparaten moeten na de pilot volledig herbruikbaar zijn.**

### Waarom dat uitgangspunt hier de doorslag geeft

De apparaten komen na de pilot terug voor volgende experimenten. Een apparaat dat daarna niet
meer met een kabel te herprogrammeren is, is elektronisch afval — en dat weegt zwaarder dan het
risico dat hier wordt afgedekt.

Dat is geen aanname maar staat in de documentatie van ESP-IDF:

> *In release mode, UART bootloader cannot perform flash encryption operations. New plaintext
> images can ONLY be downloaded using the over-the-air (OTA) scheme.*

| | Nog met een kabel te herprogrammeren? | Sleutels te bewaren |
| --- | --- | --- |
| Niets doen | ja | geen |
| Flash encryption, development mode | ja, een beperkt aantal keer | geen |
| **Flash encryption, release mode** | **nee** | geen |
| Release mode met een eigen sleutel per apparaat | ja | **één per apparaat, voor altijd** |
| Secure boot | ja, mits ondertekend met de eigen sleutel | één, voor altijd |

De ontsnapping in de vierde rij bestaat wel, maar ruilt het ene probleem in voor het andere: een
sleutel per apparaat die de levensduur moet halen. Dat is de last uit
[SLEUTELS.md](SLEUTELS.md), vermenigvuldigd met het aantal apparaten.

Secure boot brickt op zichzelf niets: zolang de sleutel er is, blijft het apparaat te
programmeren met alles wat ermee is ondertekend. Maar raakt die sleutel kwijt, dan is het
apparaat alsnog onbruikbaar — en dan is de uitkomst dezelfde als hierboven, alleen later en per
ongeluk.

### Wat er tegenover staat, eerlijk opgeschreven

Het argument "onze energiegegevens zijn niet geheim" klopt. Maar wat er in de flash ligt is niet
die data: het is het **wifi-wachtwoord van de bewoner**. Dat is niet van ons om weg te wuiven, en
het hoort in dit besluit genoemd te worden in plaats van eromheen.

Wat het risico klein houdt: de apparaten gaan naar bekende deelnemers en komen bij ons terug. Het
gaat dus om een apparaat dat tijdens de pilot wordt gestolen of kwijtraakt.

### Wat we in plaats daarvan doen

- **Wissen bij terugkomst.** Een apparaat dat terugkomt wordt gewist voordat het ergens anders
  heen gaat: drie keer de stroom eraf binnen tien seconden wist de wifigegevens en de
  API-sleutels. Dat kost niets en het brickt niets.
- **Zeggen wat het apparaat bewaart.** Een deelnemer hoort te weten dat er een wifi-wachtwoord in
  staat.

### Wat dit besluit níet afsluit

Dit besluit gaat over de weg via de hardware: flash encryption en secure boot. Het zegt **niet**
dat wachtwoorden voor altijd als leesbare tekst in de flash moeten blijven staan. Een oplossing in
de firmware die geen eFuses brandt en niets onomkeerbaar maakt, valt buiten dit besluit en mag
gewoon worden overwogen.

**Geparkeerd op 2026-09-05: verhullen in de firmware.** Het idee is om het wachtwoord niet als
leesbare tekst weg te schrijven maar het te verhullen met iets dat het apparaat zelf kent, zoals
zijn MAC-adres. Bewust nog niet gedaan, en met lage prioriteit voor deze pilot, tot de klant erover
heeft meegedacht.

Wat het zou opleveren en wat niet, zodat dat niet opnieuw uitgezocht hoeft te worden:

- **Het stopt `strings`.** Wie een flashdump doorkijkt ziet geen wachtwoord meer. Dat is de
  realistische situatie bij een pilot: een dump in een bugrapport, een apparaat dat wordt
  doorgegeven, iemand die uit nieuwsgierigheid kijkt.
- **Het stopt niemand die het probeert.** De MAC is geen geheim. Hij staat op de About-pagina, hij
  staat sinds v0.3.4 in de naam van het instelnetwerk, hij zit in elk wifi-frame, en de broncode
  staat publiek. Wie de flash kan uitlezen heeft het apparaat, en dan heeft hij de sleutel erbij.
- Noem het daarom **verhulling en geen versleuteling**. Met de eFuse-route van tafel is verhulling
  het plafond: zonder geheim in de hardware is er niets waar een sleutel uit kan komen die de
  bezitter van het apparaat niet ook heeft.
- Als het gebeurt: een SHA-256 over MAC plus een vaste waarde en dáármee XOR-en kost even veel
  werk als een kale XOR met de MAC, en haalt het herhalende patroon van zes bytes eruit.

En twee dingen die eerst moeten:

1. **De brug voor v0.2.x moet weg.** Die houdt het netwerk dat het laatst werkte leesbaar onder de
   oude sleutels `ssid`/`password`, zodat een terugval nog een netwerk vindt. Zolang die er is,
   staat het wachtwoord er alsnog leesbaar en levert verhullen niets op. De brug mag weg zodra
   geen apparaat meer een v0.2.x-image in zijn andere slot heeft.
2. **De terugval moet meegedacht worden.** Slaat een nieuwe versie verhulde wachtwoorden op en
   zakt zij door haar proeftijd, dan leest de oudere versie die bytes als het wachtwoord en komt
   het apparaat niet online. Dat is H15 opnieuw. Op te lossen met een verhoging van `layout` en de
   regels uit [NVS.md](NVS.md), maar niet in een uurtje.

### Wat het besluit zou veranderen

- Een eis van de klant, van Liander of vanuit de ACM-ontheffing. Dan is het geen afweging meer.
- Apparaten die niet meer terugkomen, bijvoorbeeld bij verkoop in plaats van een pilot. Dan
  vervalt het uitgangspunt waarop dit besluit rust.
- Een serie die groot genoeg is dat één gestolen apparaat niet meer het hele risico is.

---

## 8. Wat dit stuk niet beslist

De schattingen zijn schattingen. Wat er in dit stuk **niet** staat, omdat het niet met zekerheid
te zeggen is:

- Hoeveel de eerste serie apparaten gaat tellen, en dus hoe duur een terughaalactie zou zijn.
- Of de klant of de netbeheerder eisen stelt aan de beveiliging van apparaten in huizen. Dat kan
  het besluit van tafel halen: als het moet, hoeft het niet afgewogen te worden.
- Wat de ACM-ontheffing waaronder de gemeenschap draait hierover zegt. Zie
  [energiegemeenschap-wilhelminaweg.md](energiegemeenschap-wilhelminaweg.md), hoofdstuk 7.

Die drie horen in het gesprek thuis, en niet in de firmware.
