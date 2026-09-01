# Bijwerken over de lucht

Dit document beschrijft hoe de Energy Owl nieuwe firmware ophaalt en installeert. Het beschrijft
ook hoe u een nieuwe versie uitgeeft.

## Doel

De vijf testapparaten staan bij mensen thuis. Een fix moet die apparaten bereiken zonder dat
iemand met een kabel langsgaat. Het apparaat haalt daarom zelf nieuwe firmware op bij GitHub.

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

## Een nieuwe versie uitgeven

1. Werk de code bij en breng die naar `main`.
2. Maak een tag: `git tag -a v0.2.0 -m "wat er verandert"`.
3. Duw de tag: `git push origin v0.2.0`.
4. Wacht op de workflow **Release**. Die bouwt de firmware en controleert of het versienummer in
   het bestand gelijk is aan de tag.
5. Open de concept-release op GitHub en lees de beschrijving na.
6. Publiceer de release.

Stap 6 is het moment waarop de update naar de apparaten gaat. Zolang de release een concept is,
haalt geen enkel apparaat het bestand op.

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
