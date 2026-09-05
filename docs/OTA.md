# Bijwerken over de lucht

Dit document beschrijft hoe de Energy Owl nieuwe firmware ophaalt en installeert. Het beschrijft
ook hoe u een nieuwe versie uitgeeft.

## Doel

De vijf testapparaten staan bij mensen thuis. Een fix moet die apparaten bereiken zonder dat
iemand met een kabel langsgaat. Het apparaat haalt daarom zelf nieuwe firmware op bij GitHub.

Bijwerken is niet vrijwillig. Het apparaat vraagt de gebruiker niets. Het installeert elke
gepubliceerde versie die nieuwer is dan de zijne, en start daarna opnieuw op. Er is geen
instelling om dat te weigeren. Wie een versie uitgeeft, verandert dus alle apparaten die op het
netwerk zitten. Behandel het publiceren van een release naar die maatstaf.

## Vaste termen

| Term | Betekenis |
|---|---|
| Release | Een uitgave op GitHub met een versienummer en een firmwarebestand |
| Versienummer | Drie getallen, bijvoorbeeld `v0.2.0`, uit de git-tag |
| Slot | Een van de twee plekken in flash waar firmware staat |
| Proeftijd | De periode waarin nieuwe firmware moet bewijzen dat zij werkt |
| Terugval | Het bootprogramma start de vorige firmware weer |

## Hoe het apparaat bijwerkt

1. Het apparaat kijkt vijf minuten na het opstarten voor het eerst, en daarna elk uur.
2. Het apparaat vraagt het firmwarebestand op bij GitHub.
3. Het apparaat leest eerst het versienummer in dat bestand.
4. Is de versie niet nieuwer, dan stopt het apparaat en haalt het de rest niet op.
5. Is de versie nieuwer, dan schrijft het apparaat de firmware in het vrije slot.
6. Het scherm toont "Bijwerken" met het percentage.
7. Na het schrijven start het apparaat opnieuw op in de nieuwe firmware.

Het apparaat kijkt alleen als er een netwerkverbinding is. Zonder verbinding slaat het de beurt
over en probeert het een uur later opnieuw.

## De knop

De About-pagina heeft de knop **Nu bijwerken**. Druk daarop om meteen te laten kijken. U bereikt
de About-pagina met de pijlen op het scherm of met de knoppen op het apparaat.

De About-pagina toont ook de regel `Update`. Die regel zegt wat de updater doet:

| Regel | Betekenis |
|---|---|
| `niets te doen` | Er is niets nieuws gevonden |
| `zoeken` | Het apparaat vraagt het bij GitHub |
| `bezig, 42%` | Het apparaat schrijft de nieuwe firmware |
| `klaar, herstart` | Het schrijven is gelukt; het apparaat start opnieuw op |
| `mislukt` | De laatste poging is niet afgemaakt; het apparaat probeert het later opnieuw |

## Proeftijd en terugval

Nieuwe firmware staat na de eerste start in proeftijd. Het apparaat maakt de firmware pas
definitief als het zijn werk doet. Er zijn twee bewijzen:

| Bewijs | Wanneer |
|---|---|
| Een volledige ronde langs de API | Het apparaat heeft een token en een meting opgehaald |
| Tien minuten netwerk | Het apparaat is tien minuten aaneengesloten verbonden |

Het tweede bewijs is er voor een storing bij de API. Firmware die het netwerk haalt, is altijd nog
te vervangen door een volgende versie.

Blijft beide bewijzen uit en start het apparaat opnieuw op, dan zet het bootprogramma de vorige
firmware terug. Firmware die meteen vastloopt, verdwijnt zo vanzelf.

## De terugval beproeven

Een vangnet dat nooit is beproefd, is een aanname. Deze beproeving is gedaan op 2026-09-01; de
uitkomst staat in [REVIEW.md](REVIEW.md), vierde ronde. Beproef de terugval daarom met een release die
met opzet stuk is. Doe dit met een apparaat op tafel, met een kabel binnen bereik.

De opzet: een tak naast `main`, met daarin één regel die het apparaat drie seconden na de start
laat vastlopen. Drie seconden is te kort voor beide bewijzen uit de vorige paragraaf, dus de
firmware maakt zichzelf nooit definitief.

1. Maak de tak en de fout:

   ```
   git checkout -b test/rollback main
   ```

   Voeg in `main/main.c` een taak toe die drie seconden wacht en dan `abort()` aanroept.

2. Tag de tak en duw de tag. **Breng deze tak nooit naar `main`.**

   ```
   git tag -a v0.2.2 -m "Met opzet stuk: beproeving van de terugval"
   git push origin test/rollback
   git push origin v0.2.2
   ```

3. Publiceer de release en kijk mee op de seriële lijn.

4. Wat u hoort te zien: het apparaat haalt de kapotte versie op, start erin op, loopt vast,
   start opnieuw op, en staat daarna weer op de vorige versie.

5. **Trek de kapotte release meteen in.** Zolang die de laatste is, haalt het apparaat hem elk uur
   opnieuw op:

   ```
   gh release edit v0.2.2 --draft=true
   ```

6. Geef daarna een goede versie uit met een hoger nummer, zodat het apparaat weer bij is.

7. Ruim de tag en de tak op:

   ```
   git push origin --delete v0.2.2 test/rollback
   git tag -d v0.2.2
   ```

## Een nieuwe versie uitgeven

Deze paragraaf geeft de volledige procedure op de opdrachtregel. De voorbeelden gebruiken
`v0.2.3`. Vervang dat nummer door het nummer dat u uitgeeft.

U hebt `git` en `gh` nodig. `gh` moet zijn aangemeld: controleer dat met `gh auth status`.

### Stap 1 — Haal de laatste `main` op

```
git checkout main
git pull --ff-only
```

### Stap 2 — Controleer dat de werkmap schoon is

```
git status --porcelain
```

Deze opdracht hoort niets te tonen. Elk bestand dat wel verschijnt, komt niet in de release
terecht, want de bouwserver bouwt de tag en niet uw werkmap.

### Stap 3 — Kies het nummer

```
git tag --list 'v*' --sort=-v:refname | head -3
```

Neem het hoogste nummer en verhoog het. Zie "Regels voor het versienummer" hieronder.

### Stap 4 — Maak de tag

```
git tag -a v0.2.3 -m "wat er verandert"
```

Gebruik `-a`. Een tag zonder toelichting zegt over een half jaar niets meer.

### Stap 5 — Duw de tag

```
git push origin v0.2.3
```

De workflow **Release** start hierdoor vanzelf.

### Stap 6 — Wacht op de bouw

```
gh run watch $(gh run list --workflow=Release --limit 1 --json databaseId -q '.[0].databaseId')
```

De workflow bouwt de firmware, controleert of het versienummer in het bestand gelijk is aan de
tag, en hangt het bestand aan een **concept**-release.

Mislukt de bouw, kijk dan in het log:

```
gh run view --log-failed
```

### Stap 7 — Controleer de concept-release

```
gh release view v0.2.3
```

Controleer twee dingen:

| Wat | Verwachting |
|---|---|
| `draft` | `true` — de release staat nog stil |
| Bijlage | `energy-owl.bin` staat erbij |

### Stap 8 — Publiceer

```
gh release edit v0.2.3 --draft=false
```

Dit is het moment waarop de update naar de apparaten gaat.

### Stap 9 — Controleer wat de apparaten krijgen

```
curl -sL -o /tmp/served.bin \
  https://github.com/edwin-oetelaar/Shared_Energy_Telemetry_Device/releases/latest/download/energy-owl.bin
python -m esptool image_info /tmp/served.bin | grep "App version"
```

Er hoort `App version: v0.2.3` te staan. Staat er een ouder nummer, wacht dan een minuut: GitHub
bewaart het antwoord op de vaste URL kort in een tussengeheugen.

### Stap 10 — Kijk mee bij het eerste apparaat

```
python3 tools/monitor.py /dev/ttyACM0 600
```

Het apparaat kijkt vijf minuten na het opstarten en daarna elk uur. Wilt u niet wachten, druk dan
op **Nu bijwerken** op de About-pagina.

## Een release intrekken

Werkt een uitgegeven versie niet, zet de release dan terug op concept:

```
gh release edit v0.2.3 --draft=true
```

Apparaten die de versie nog niet hebben, halen hem daarna niet meer op. Apparaten die hem al
hebben, houden hem: intrekken haalt niets terug. Geef daarom altijd meteen een nieuwe versie uit
met een hoger nummer, met daarin de reparatie of de vorige toestand.

## De lus die nooit mag ontstaan

Dit is de ergste afloop van een uitgave, en hij mag niet kunnen gebeuren. Wie een versie
uitbrengt hoort te weten hoe hij eruitziet.

**Het scenario.** Er gaat een versie uit die opstart maar geen netwerk kan vinden — een fout in
de wifi-code, een verkeerde instelling, iets dat de CI niet ziet omdat het pas op een draaiend
apparaat bestaat.

1. Het apparaat haalt de versie op, schrijft hem, en start erin op.
2. De nieuwe versie vindt geen netwerk. De proeftijd loopt daarom nooit af: er komt geen
   telemetrie, en er is ook geen netwerk dat tien minuten aanstaat.
3. **Het apparaat valt niet vanzelf terug.** Het blijft staan met firmware die niets kan.
   Terugvallen gebeurt pas bij een herstart, en niets in de firmware herstart.
4. De gebruiker ziet een dood apparaat en trekt de stekker eruit. Nu valt het terug op de vorige
   versie, die wél verbindt.
5. Vijf minuten later kijkt die vorige versie of er nieuwe firmware is, vindt dezelfde kapotte
   uitgave, haalt hem op, en start er weer in op.
6. Vanaf stap 2.

Elke stroomonderbreking koopt de gebruiker ongeveer vijf minuten werkend apparaat.

**Waarom een fabrieksreset niet helpt.** Drie keer de stroom eraf wist de wifigegevens en de
API-sleutels. De gebruiker stelt het apparaat opnieuw in, het verbindt, en loopt binnen vijf
minuten weer in stap 1. Het wissen raakt de updatelogica niet, dus het brengt de gebruiker
precies terug waar hij vandaan kwam — met het gevoel dat hij het zelf erger heeft gemaakt.

**De uitweg is dan een kabel.** Bij apparaten op een vensterbank betekent dat terughalen.

### Wat dit betekent voor wie uitgeeft

- **Trek een kapotte uitgave meteen in** en geef er een hogere overheen. Zie "Een release
  intrekken". Zolang de kapotte versie de laatste is, haalt elk apparaat hem elk uur opnieuw op.
- **Beproef een uitgave op hardware voordat u hem publiceert**, en dan met het netwerk erbij:
  een versie die bouwt en opstart is niet hetzelfde als een versie die verbindt.
- De uitgave met opzet stuk maken en de terugval beproeven — zie "De terugval beproeven" — dekt
  het geval van een versie die **crasht**. Zij dekt niet het geval van een versie die netjes
  draait en alleen geen netwerk vindt. Dat is het gevaarlijkere van de twee, want er is geen
  crash die het aanwijst.

### Wat de firmware zou moeten doen

Onthouden welke versie is teruggevallen, en die niet nog een keer installeren. Een nummer in NVS,
in een eigen namespace zodat de fabrieksreset het niet wist, en de updater die een uitgave met
dat nummer overslaat. Een latere, hogere versie komt er dan gewoon door, dus een reparatie
bereikt het apparaat altijd nog.

Dat zit er **nog niet** in. Zie **H16** in [REVIEW.md](REVIEW.md).

## Regels voor het versienummer

- Gebruik drie getallen met een `v` ervoor: `v0.2.0`.
- Verhoog het nummer bij elke uitgave.
- Een apparaat installeert alleen een hoger nummer. Gelijke nummers slaat het over.

Het versienummer komt uit de git-tag. Het staat in het firmwarebestand zelf, op de About-pagina en
in het log. Een bouw uit een werkmap krijgt een nummer met `-dirty` erachter. Zo'n bouw hoort niet
in een release.

## De eerste keer

Apparaten met firmware van voor deze versie kunnen zichzelf niet bijwerken. Sluit die apparaten
een keer met een kabel aan en flash ze. Daarna gaat het vanzelf.

## Grenzen

- Het apparaat haalt de firmware op via HTTPS. Het controleert het certificaat van GitHub, maar
  het controleert geen handtekening onder het bestand zelf. Zie H5 in [REVIEW.md](REVIEW.md).
- Het apparaat controleert het versienummer, niet de inhoud. Een release met een verkeerd
  bestand bereikt alle apparaten. De workflow controleert daarom de tag tegen het bestand.
