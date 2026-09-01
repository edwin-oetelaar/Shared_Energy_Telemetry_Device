# Energiegemeenschap Wilhelminaweg

Dit document beschrijft het project waar dit apparaat bij hoort. Het legt uit wat de
energiegemeenschap wil bereiken en hoe zij dat doet.

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

## 8. Pilot voor de netbeheerder, volgende stap voor de deelnemers

Liander voert het project uit als pilot. De deelnemers zien het anders. Voor hen is de
energiegemeenschap een volgende stap in de ontwikkeling van de energiemarkt.

Die verwachting blijkt uit de investeringen. Een deelnemer koopt een thuisbatterij niet
voor de duur van een proef.

## 9. Betrokken partijen

| Partij | Rol |
| --- | --- |
| Bewoners Wilhelminaweg | Deelnemers en initiatiefnemers |
| Liander | Netbeheerder; voert het project uit als pilot |
| OM-Nieuwe Energie | Levert het zelfleveringsplatform en de groene stroom |
| Gemeente Wageningen | Ondersteunt het initiatief |
| Manfred van der Voort (ICR3ATE) | Bedenker van het concept, met de batterijenbuurt |
| ACM | Verleent de ontheffing voor de pilot |

## 10. Tijdlijn

| Periode | Gebeurtenis |
| --- | --- |
| Enkele jaren vóór 2024 | De bewoners werken samen aan de energietransitie |
| Eind 2024 | Het initiatief voor de energiegemeenschap start |
| Augustus 2025 | De deelnemers brengen de installaties en het gebruik in kaart |

## 11. Status in augustus 2025

De deelnemers zetten de puntjes op de i. Zij brengen twee dingen in kaart:

- De opbouw van de installatie in elk huishouden.
- De hoeveelheid en de gelijktijdigheid van het energiegebruik.

Die twee gegevens bepalen hoeveel energie de gemeenschap werkelijk kan delen.

## 12. De rol van dit apparaat

Dit apparaat maakt de toestand van de gemeenschap zichtbaar in huis. Het haalt elke minuut
de meetwaarden op bij de Energyboxx-API en toont het resultaat op een scherm:

| Toestand | Betekenis | Op het scherm |
| --- | --- | --- |
| Overschot | De gemeenschap heeft energie over om te delen | Energie over, groen |
| Tekort | De gemeenschap moet energie inkopen | Energie inkopen, geel |
| In balans | Vraag en aanbod zijn gelijk | In balans, donkergroen |
| Geen meting | Het apparaat heeft geen actuele gegevens | Geen gegevens, grijs |

Een deelnemer ziet daardoor in één oogopslag of dit een goed moment is om een wasmachine
of een auto te laden. Zo verschuift het gebruik naar de momenten waarop de gemeenschap
zelf energie over heeft.

Het apparaat werkt zichzelf bij. Het haalt nieuwe firmware op bij de maker en installeert die
zonder iets te vragen. Tijdens het bijwerken toont het scherm "Bijwerken", en daarna start het
apparaat opnieuw op. Dat duurt ongeveer een halve minuut. Het apparaat is in ontwikkeling, en
deelnemers krijgen verbeteringen zo vanzelf.

De technische beschrijving van het apparaat staat in de [README](../README.md).

## 13. Woordenlijst

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

## 14. Contact

Stel vragen over het project aan Paddy Noë, paddy@wilhelminaweg5.nl.
