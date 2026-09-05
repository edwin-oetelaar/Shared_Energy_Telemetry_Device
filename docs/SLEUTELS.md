# Sleutelbeheer

Dit document beschrijft welke sleutels bij dit product horen, welke u moet bewaren en welke niet,
en hoe u dat doet. Het hoort bij bevinding **H5** en bij [BESLUIT-c5-h5.md](BESLUIT-c5-h5.md).

Het is geschreven voor een klein bedrijf. Geen sleutelkluis en geen ceremonie met vier mensen —
maar wel iets dat over vijf jaar nog werkt als degene die het opzette er niet meer is.

---

## 1. Welke sleutels er zijn

| Sleutel | Waarvoor | Wie maakt hem | Moet u hem bewaren? |
| --- | --- | --- | --- |
| **Flash encryption** | De inhoud van de flash onleesbaar maken | Het apparaat zelf, bij de eerste start | **Nee.** Hij komt de chip nooit uit |
| **NVS encryption** | De opgeslagen gegevens versleutelen | Het apparaat zelf | **Nee** |
| **Secure boot** | Alleen firmware draaien die u heeft ondertekend | **U**, één keer | **Ja, de hele levensduur van het product** |

Die eerste twee zijn de reden dat flash encryption veel goedkoper is dan het lijkt. De
ESP-IDF-documentatie zegt het zo:

> *we recommend keys are generated on the device during first boot (default behaviour)*

Er is dus niets te bewaren, niets te delen en niets kwijt te raken. **De hele last van dit
document gaat over één sleutel: die van secure boot.**

---

## 2. Wat er op het apparaat terechtkomt

Niet de sleutel. In de chip wordt een **vingerafdruk van de publieke sleutel** gebrand, 32 bytes:

> *A digest of the public key is stored in the eFuse.*

De privésleutel blijft bij u. Eén sleutel bedient de hele vloot; elk apparaat controleert er
alleen mee. Er is ruimte voor **drie** vingerafdrukken, elk apart in te trekken — dat is het
ingebouwde mechanisme om ooit van sleutel te wisselen.

De ESP32-S3 gebruikt **RSA-3072** (secure boot v2).

---

## 3. De sleutel maken

Eén keer, op een machine die u vertrouwt, en niet op een gedeelde bouwserver.

```bash
idf.py secure-generate-signing-key --version 2 --scheme rsa3072 setd-secure-boot.pem
```

Of met OpenSSL, wat op hetzelfde neerkomt:

```bash
openssl genrsa -out setd-secure-boot.pem 3072
```

Noteer meteen, op dezelfde plek als waar de sleutel bewaard wordt:

- de datum,
- waarvoor hij is: product en klant,
- welke vingerafdrukplek hij krijgt, 0, 1 of 2,
- wie hem heeft gemaakt.

Over vijf jaar is een `.pem` zonder die vier regels een bestand waarvan niemand meer durft te
zeggen wat het is.

---

## 4. Waar hij blijft

De regel is: **drie kopieën, twee plekken, één daarvan niet in hetzelfde gebouw.**

| Kopie | Waar | Waarom |
| --- | --- | --- |
| 1 | Versleutelde USB-stick, thuis of op kantoor | De werkkopie |
| 2 | Versleutelde USB-stick, andere locatie | Brand, diefstal, waterschade |
| 3 | Wachtwoordkluis als bijlage | Bereikbaar als de sticks er niet zijn |

En de regels eromheen:

- **Nooit in Git.** Zet `*.pem` in `.gitignore` als vangnet, maar vertrouw daar niet op.
- **Nooit in een chat, e-mail of ticket.** Ook niet "even snel".
- Elke kopie versleuteld, met een wachtwoord dat ergens anders ligt dan de sleutel zelf.
- Noteer wanneer u voor het laatst hebt gecontroleerd dat een kopie nog leesbaar is. Een
  USB-stick die drie jaar in een la ligt is geen kopie tot u het weet.

---

## 5. Wie erbij kan

Schrijf op — in het bedrijf, niet in dit document:

- wie de sleutel heeft,
- wie hem kan krijgen als die persoon er niet is,
- wat er gebeurt als die persoon vertrekt.

Dit is de vraag die bij een klein bedrijf het vaakst blijft liggen en het duurst uitpakt. Eén
persoon met de enige kopie is geen sleutelbeheer maar uitstel.

**Delen met de klant.** Wil de klant een kopie, geef die dan als **bewaarkopie** en niet als
werkkopie: één keer overhandigd, vastgelegd wanneer en aan wie, en niet gebruikt voor het
dagelijkse ondertekenen. Zo blijft er één plek waar releases vandaan komen, en heeft de klant
toch de zekerheid dat het product niet met u verdwijnt.

---

## 6. Ondertekenen: op uw eigen machine of in de bouwstraat

Dit is de enige echte kruising, en ESP-IDF maakt hem gemakkelijker dan verwacht: **de
bouwomgeving heeft de sleutel niet nodig.**

> *To use remote signing, disable the option CONFIG_SECURE_BOOT_BUILD_SIGNED_BINARIES and build
> the firmware. The private signing key does not need to be present on the build system.*

| | De sleutel in GitHub Actions | Ondertekenen op uw eigen machine |
| --- | --- | --- |
| Gemak | Een release is één tag duwen, zoals nu | Tag duwen, bestand ophalen, ondertekenen, terugzetten |
| Waar het geheim ligt | Op een server van een ander, als secret | Alleen bij u |
| Wie kan ondertekenen | Iedereen die de bouwstraat kan wijzigen | Alleen wie de sleutel heeft |
| Als het account wordt overgenomen | De aanvaller kan ondertekenen | De aanvaller kan niets ondertekenen |

**Aanbeveling: onderteken op uw eigen machine.** Voor een vloot van een handvol apparaten met een
paar releases per maand weegt het gemak niet op tegen dit: secure boot is er juist om te
beschermen tegen een overgenomen account. Een sleutel ín dat account maakt de bescherming ongedaan
die u ermee kocht.

De procedure in [OTA.md](OTA.md) krijgt er dan één stap bij, tussen de bouw en het publiceren:

```bash
idf.py secure-sign-data --keyfile setd-secure-boot.pem \
       --output energy-owl-signed.bin energy-owl.bin
```

Het ondertekende bestand gaat als bijlage aan de release, in plaats van het bestand dat de
bouwstraat maakte.

---

## 7. Van sleutel wisselen

Er passen drie vingerafdrukken in de chip, en dat is er met opzet meer dan één. Wilt u ooit van
sleutel wisselen, dan is de weg: een tweede sleutel toevoegen, een firmware uitbrengen die met
beide is ondertekend, wachten tot alle apparaten die hebben, en dan de eerste intrekken.

**Let op één ding bij het inschakelen.** ESP-IDF trekt ongebruikte plekken standaard in, zodat
niemand er later een eigen sleutel bij kan zetten. Dat is veiliger, maar het haalt ook de
mogelijkheid weg om ooit te wisselen. Wilt u die openhouden, dan moet u dat bewust instellen
(`CONFIG_SECURE_BOOT_ALLOW_UNUSED_DIGEST_SLOTS`) en accepteren dat er lege plekken blijven staan.

Voor een pilot van een handvol apparaten die u zelf nog kunt flashen, is dat een kleine keuze.
Voor een serie in huizen is het het verschil tussen "we wisselen van sleutel" en "we halen alles
terug".

---

## 8. Wat er misgaat als het misgaat

**De sleutel is kwijt.** De apparaten in het veld krijgen nooit meer een update. Zij blijven
draaien wat erop staat, en elke fout die daarin zit blijft er ook. Dit is het ergste dat er kan
gebeuren, en het gebeurt door slordigheid en niet door een aanval.

**De sleutel is uitgelekt.** Wie hem heeft kan firmware maken die uw apparaten accepteren. Wissel
van sleutel via hoofdstuk 7 — als u die mogelijkheid hebt opengehouden. Zo niet, dan is
terughalen de enige weg.

**U weet niet meer of hij is uitgelekt.** Behandel dat als uitgelekt.

---

## 9. Volgorde van invoeren

1. **Sleutelbeheer eerst.** Hoofdstuk 3 tot 5. Zonder antwoord op "waar ligt hij en wie kan
   erbij" heeft de rest geen zin.
2. **Dan flash encryption en NVS-encryptie.** Die vragen geen sleutel van u en beschermen de
   gegevens van de bewoner. Zij kunnen ook zonder stap 3.
3. **Dan pas secure boot.** Dat is het stuk dat de ondertekensleutel nodig heeft, dat het
   onderhoud lastiger maakt, en dat niet meer terug te draaien is.

Alle drie moeten met een kabel, vóórdat een apparaat de deur uit gaat. Over de lucht kan het niet.
