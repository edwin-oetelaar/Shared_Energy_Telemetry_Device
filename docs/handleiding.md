# Energy Owl — handleiding

Deze handleiding beschrijft versie **v0.3.0**. De versie staat op de About-pagina van het
apparaat zelf.

---

## 1. Wat het apparaat doet

Het apparaat hangt aan de muur of staat op een kast. Het vraagt elke minuut aan de
Energyboxx-server hoe de energiegemeenschap ervoor staat, en toont het antwoord op zijn scherm.

Zo ziet u in één oogopslag of dit een goed moment is om de wasmachine aan te zetten of de auto
te laden.

---

## 2. De beelden

Het scherm toont één van deze vier beelden, met daaronder hoeveel het is.

| Beeld | Kleur | Wat het betekent |
| --- | --- | --- |
| **Energie over in de groep** | groen | De gemeenschap heeft stroom over. Een goed moment om stroom te gebruiken. |
| **Energie inkopen voor groep** | geel | De gemeenschap koopt stroom in. Stel gebruik zo mogelijk uit. |
| **In balans** | donkergroen | Vraag en aanbod zijn ongeveer gelijk. |
| **Geen gegevens** | grijs | Het apparaat heeft geen actuele meting. Zie hoofdstuk 7. |

Onder het woord staat de hoeveelheid:

```text
  Energie over in de groep
         0,3 kW
```

Het getal staat er zonder plus of min. Het woord erboven zegt al welke kant het op gaat.

De woorden zeggen **in de groep** en **voor groep**, want het gaat over de gemeenschap als
geheel en niet over uw eigen huis. Dit apparaat meet niets bij u binnen.

---

## 3. Bediening

Het scherm is een aanraakscherm. Onder het scherm zitten drie knoppen.

| Handeling | Wat er gebeurt |
| --- | --- |
| Het scherm aanraken | De pijlen verschijnen |
| Pijl links of rechts | Naar het vorige of volgende beeld |
| Vijftien seconden niets doen | Het apparaat toont weer de actuele toestand |
| De knop `main` op het paneel | Meteen terug naar de actuele toestand |

U kunt door zeven beelden bladeren: het statusscherm, de About-pagina, het uilbeeld en de vier
energiebeelden.

**Een gebladerd beeld is geen meting.** Staat er linksboven **voorbeeld**, dan kijkt u naar een
beeld dat u zelf hebt opgezocht en niet naar de toestand van dit moment.

---

## 4. Het statusscherm

Het eerste beeld in de bladerlijst toont wat het apparaat over zichzelf weet:

```text
Wifi      OETELX (2 van 3)
          192.168.50.145
Sleutels  goed, nog 119 min
Meting    42 s geleden
          +0,3 kW netto
          4,1 in, 4,4 uit
Portaal   dicht
```

`(2 van 3)` betekent: het apparaat kent drie netwerken en gebruikt op dit moment het tweede.
Kent het er maar één, dan staat er niets achter.

Onderaan staat de knop **Sleutels invoeren**. Zie hoofdstuk 6.

---

## 5. Het apparaat instellen

Een nieuw apparaat kent nog geen netwerk. Het zet dan zijn eigen wifinetwerk aan en toont een
QR-code.

1. Scan de QR-code met uw telefoon, of zoek in de wifilijst het netwerk **SETD_Provisioning**.
2. Uw telefoon opent vanzelf een pagina. Gebeurt dat niet, ga dan naar `http://192.168.4.1`.
3. Kies uw eigen wifinetwerk en vul het wachtwoord in. Druk op **Connect**.
4. Vul daarna de client ID en het client secret van Energyboxx in. Druk op **Klaar**.

Het apparaat bewaart gegevens pas als het ze heeft beproefd. Uw wifiwachtwoord wordt opgeslagen
zodra de verbinding lukt, en de sleutels zodra de server ze accepteert.

Het scherm toont **Klaar, we zijn online** en gaat daarna over op de energiebeelden.

---

## 6. Meer dan één netwerk

Het apparaat onthoudt **drie** netwerken. Dat is handig als u het meeneemt: naar het werk, naar
de buren, of naar een hotspot op uw telefoon.

Bij het opstarten kijkt het apparaat welke van zijn netwerken in de lucht zijn en kiest het de
sterkste. Het netwerk dat de vorige keer werkte gaat voor. Een netwerk dat er niet is kost geen
wachttijd.

**Een netwerk toevoegen.** Druk op **Sleutels invoeren** op het statusscherm. Het apparaat zet
zijn eigen netwerk aan terwijl het gewoon online blijft. Volg daarna stap 1 tot 3 van hoofdstuk
5. De pagina zegt vooraf waar het nieuwe netwerk terechtkomt.

**Een netwerk vergeten.** Op dezelfde pagina staat achter elk onthouden netwerk een knop
**Forget**. De andere netwerken blijven staan.

**Klaar.** Onderaan die pagina staat een knop **Klaar**. Daarmee sluit u het portaal en gaat het
scherm terug naar de energiebeelden. Doet u niets, dan sluit het portaal zichzelf na een kwartier.

Is er al een netwerk met dezelfde naam bekend, dan vervangt het nieuwe wachtwoord het oude. Zijn
alle drie de plekken bezet, dan maakt het netwerk plaats dat het langst niet heeft gewerkt.

---

## 7. Als er iets niet goed gaat

| Op het scherm | Wat er aan de hand is | Wat u doet |
| --- | --- | --- |
| **Geen verbinding** | Het netwerk is weg of buiten bereik | Niets. Het apparaat blijft het proberen en komt vanzelf terug |
| **Wachtwoord klopt niet** | Het netwerk weigert het wachtwoord | Voer het netwerk opnieuw in; zie hoofdstuk 6 |
| **Sleutels nodig** | De wifi werkt, maar de Energyboxx-sleutels ontbreken | Druk op **Sleutels invoeren** en vul ze in |
| **Geen gegevens** | Er is verbinding, maar geen actuele meting | Niets. Het apparaat probeert het elke minuut opnieuw |
| **Instellen** met QR | Het apparaat wacht tot iemand het instelt | Zie hoofdstuk 5 |

Het apparaat geeft nooit op. Het blijft opnieuw proberen, met een oplopende wachttijd tot vijf
minuten.

**Alles wissen.** Zet het apparaat drie keer binnen tien seconden uit en aan. Dan vergeet het
alle netwerken en alle sleutels, en begint het opnieuw bij hoofdstuk 5. Doe dit alleen als u
opnieuw wilt beginnen.

---

## 8. Het apparaat werkt zichzelf bij

Het apparaat haalt nieuwe firmware op bij de maker en installeert die zelf. Dat gebeurt vijf
minuten na het opstarten en daarna elk uur.

Dit is **niet vrijwillig**. Er is geen knop om het uit te zetten. Zo houden alle apparaten in de
gemeenschap dezelfde versie.

Tijdens het bijwerken toont het scherm **Bijwerken** met het percentage. Daarna start het
apparaat opnieuw op. Dat duurt ongeveer een halve minuut.

Gaat er iets mis met een nieuwe versie, dan valt het apparaat vanzelf terug op de vorige. U
hoeft daar niets voor te doen.

Op de About-pagina staat de knop **Nu bijwerken** voor wie niet wil wachten.

---

## 9. Waar dit apparaat vandaan komt

De About-pagina toont de makers, de versie en een QR-code naar de projectpagina.

Het apparaat is gemaakt door Paddy, Job en Edwin bij Dolphin Solutions, voor de
energiegemeenschap aan de Wilhelminaweg. Zie
[energiegemeenschap-wilhelminaweg.md](energiegemeenschap-wilhelminaweg.md) voor wat die
gemeenschap is en waarom zij bestaat.

---

## 10. Wat dit apparaat niet doet

- Het meet niets in uw eigen huis. Het toont de toestand van de gemeenschap als geheel.
- Het schakelt niets. Het geeft geen opdracht aan uw wasmachine of laadpaal.
- Het bewaart geen geschiedenis. Het toont wat er nu is.
