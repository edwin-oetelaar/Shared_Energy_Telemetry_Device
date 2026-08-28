# Energiegemeenschap Wilhelminaweg

Dit document beschrijft het project waar dit apparaat bij hoort. Het legt uit wat de
energiegemeenschap wil bereiken en hoe zij dat doet.

De tekst volgt de principes van Simplified Technical English, in het Nederlands: korte
zinnen, actieve vorm, tegenwoordige tijd, en één vaste term per begrip. De woordenlijst
onderaan legt die termen vast.

---

## 1. Doel

De energiegemeenschap deelt overtollige zonne-energie tussen de huishoudens in de straat.

De gemeenschap wil drie dingen bereiken:

1. De deelnemers gebruiken zo veel mogelijk van hun eigen zonne-energie.
2. Zonne-energie die één deelnemer niet gebruikt, gaat naar een andere deelnemer.
3. De deelnemers leveren zo min mogelijk terug aan het net.

## 2. Wat de gemeenschap is

De energiegemeenschap is een samenwerking tussen huishoudens aan de Wilhelminaweg in
Wageningen. De bewoners werken al enkele jaren samen aan de energietransitie. Eind 2024
zijn zij gestart met het initiatief voor de energiegemeenschap.

Het idee komt van Manfred van der Voort (ICR3ATE), met de batterijenbuurt als voorbeeld.

## 3. Deelnemers

Meerdere huishoudens doen mee. De deelnemers hebben niet allemaal dezelfde installatie.

| Type deelnemer | Zonnepanelen | Thuisbatterij | Rol in de gemeenschap |
| --- | --- | --- | --- |
| Opwekker met opslag | Ja | Ja | Wekt op, slaat op, deelt overschot |
| Opslag zonder opwek | Nee | Ja | Slaat gedeelde energie op en gebruikt die later |
| Alleen gebruiker | Nee | Nee | Gebruikt gedeelde energie direct |

## 4. Hoe de gemeenschap de energie verdeelt

De gemeenschap gebruikt zonne-energie in een vaste volgorde. Elke stap komt pas aan de
beurt als de vorige stap genoeg heeft.

1. **Eigen gebruik.** De zonne-energie dekt eerst het directe energiegebruik van het
   huishouden dat opwekt.
2. **Eigen opslag.** Wat overblijft, laadt de thuisbatterij. Het huishouden gebruikt die
   energie later op de dag, als de zon niet meer schijnt.
3. **Delen met de buren.** Wat daarna overblijft, stelt het huishouden beschikbaar aan de
   andere deelnemers.
4. **Terugleveren.** Wat de gemeenschap niet gebruikt, gaat terug naar het net.

Stap 2 verhoogt het eigen gebruik. Stap 3 verhoogt het gebruik binnen de gemeenschap.

## 5. Aanvulling uit het net

De gemeenschap heeft niet altijd genoeg zonne-energie. OM vult het tekort aan met groene
stroom uit het net.

## 6. Noodstroom

Een aantal deelnemers heeft zelf een thuisbatterij met omvormer gekocht. Die installatie
levert ook noodstroom. Bij een stroomstoring blijft het huishouden daardoor voorzien.

## 7. Juridische basis en tarief

De gemeenschap is een pilot. De ACM (Autoriteit Consument en Markt) heeft daarvoor
ontheffing verleend.

De ontheffing geeft de gemeenschap twee mogelijkheden:

- De deelnemers gebruiken het zelfleveringsplatform van OM.
- De deelnemers hanteren een eigen tarief voor de zonnestroom die zij delen.

## 8. Betrokken partijen

| Partij | Rol |
| --- | --- |
| Bewoners Wilhelminaweg | Deelnemers en initiatiefnemers |
| Liander | Netbeheerder; voert het project uit als pilot |
| OM-Nieuwe Energie | Levert het zelfleveringsplatform en de groene stroom |
| Gemeente Wageningen | Ondersteunt het initiatief |
| Manfred van der Voort (ICR3ATE) | Bedenker van het concept, met de batterijenbuurt |
| ACM | Verleent de ontheffing voor de pilot |

## 9. Tijdlijn

| Periode | Gebeurtenis |
| --- | --- |
| Enkele jaren vóór 2024 | De bewoners werken samen aan de energietransitie |
| Eind 2024 | Het initiatief voor de energiegemeenschap start |
| Augustus 2025 | De deelnemers brengen de installaties en het gebruik in kaart |

## 10. Status in augustus 2025

De deelnemers zetten de puntjes op de i. Zij brengen twee dingen in kaart:

- De opbouw van de installatie in elk huishouden.
- De hoeveelheid en de gelijktijdigheid van het energiegebruik.

Die twee gegevens bepalen hoeveel energie de gemeenschap werkelijk kan delen.

## 11. De rol van dit apparaat

Dit apparaat maakt de toestand van de gemeenschap zichtbaar in huis. Het haalt elke minuut
de meetwaarden op bij de Energyboxx-API en toont het resultaat op een ledring:

| Toestand | Betekenis | Ledring |
| --- | --- | --- |
| Overschot | De gemeenschap heeft energie over om te delen | Groen |
| Tekort | De gemeenschap moet energie inkopen | Geel |
| In balans | Vraag en aanbod zijn gelijk | Uit |

Een deelnemer ziet daardoor in één oogopslag of dit een goed moment is om een wasmachine
of een auto te laden. Zo verschuift het gebruik naar de momenten waarop de gemeenschap
zelf energie over heeft. De technische beschrijving van het apparaat staat in de
[README](../README.md).

## 12. Woordenlijst

De tekst gebruikt deze termen steeds in dezelfde betekenis.

| Term | Betekenis |
| --- | --- |
| Energiegemeenschap | De samenwerkende huishoudens aan de Wilhelminaweg |
| Deelnemer | Een huishouden dat meedoet aan de energiegemeenschap |
| Thuisbatterij | Een batterij in huis die energie opslaat voor later gebruik |
| Omvormer | Het apparaat dat gelijkstroom omzet in wisselstroom |
| Eigen gebruik | Energie die het huishouden gebruikt dat de energie zelf opwekt |
| Overschot | Zonne-energie die overblijft na eigen gebruik en opslag |
| Delen | Een overschot beschikbaar stellen aan een andere deelnemer |
| Terugleveren | Energie aan het net leveren die de gemeenschap niet gebruikt |
| Zelfleveringsplatform | Het systeem van OM waarmee deelnemers onderling energie leveren |
| Ontheffing | Toestemming van de ACM om af te wijken van de standaardregels |
| Pilot | Een proef waarin de gemeenschap de opzet in de praktijk beproeft |

---

## Openstaand punt

De brontekst eindigt halverwege een zin:

> "Het project is voor Liander een Pilot, maar voor ons een …"

De zin is niet afgemaakt en dit document vult hem niet in. Het punt lijkt te zijn dat de
gemeenschap het project anders ziet dan de netbeheerder: voor Liander is het een proef,
voor de bewoners iets blijvends. Vul de bedoelde formulering aan, dan neem ik hem over.

---

*Bron: projectbeschrijving Energiegemeenschap Wilhelminaweg. Herschreven op 2026-08-28 in
STE-stijl. De feiten, namen en data komen uit de brontekst; er is niets aan toegevoegd.*
