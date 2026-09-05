#!/bin/sh
#  Maak een PDF van een document om uit te delen.
#
#      tools/handleiding-pdf.sh                      -> handleiding.pdf
#      tools/handleiding-pdf.sh docs/OTA.md          -> OTA.pdf
#
#  Waarom een script en geen losse opdracht: de opmaakkeuzes hieronder - de
#  taal voor het afbreken van woorden, de marges, de titelpagina - horen bij
#  elkaar en zijn een keer uitgezocht. Wie hier een tweede document doorheen
#  haalt, krijgt hetzelfde resultaat.
#
#  Nodig: pandoc en pdflatex.

set -e

BRON="${1:-docs/handleiding.md}"
NAAM=$(basename "$BRON" .md)
DOEL="${2:-$NAAM.pdf}"

VERSIE=$(git describe --tags --abbrev=0 2>/dev/null || echo "onbekend")
DATUM=$(date +%Y-%m-%d)

command -v pandoc   >/dev/null || { echo "pandoc ontbreekt"; exit 1; }
command -v pdflatex >/dev/null || { echo "pdflatex ontbreekt"; exit 1; }

pandoc "$BRON" -o "$DOEL" \
    --pdf-engine=pdflatex \
    --toc --toc-depth=1 \
    --metadata title="$(head -1 "$BRON" | sed 's/^# *//')" \
    --metadata subtitle="Energy Owl" \
    --metadata author="Dolphin Solutions" \
    --metadata date="$VERSIE · $DATUM" \
    -V lang=nl \
    -V geometry:margin=25mm \
    -V fontsize=11pt \
    -V linkcolor=black \
    -V colorlinks=true

echo "$DOEL"
