# Footprint-Abgleich vom 15.10.2026

**Korrektur zum F8-Abgleich:** Die vollständigen Symbolpfade waren in dieser
Prüfung noch falsch. Der nachfolgende Bericht zur F8-Verknüpfung dokumentiert
Ursache, Pfadkorrektur und die zusätzliche native Zuordnungsprüfung.

Grundlage: Nutzerarchiv `AmiWiFi_Pico2W(1).zip`, nicht der ältere GitHub-Boardstand.

## Korrekturen

- J1, U1, U7 und U8: Die im Board bearbeiteten Footprints wurden mit KiCads nativer Footprint-Speicherfunktion in die jeweiligen lokalen Bibliotheksdateien übernommen. Boardgeometrie unverändert. U8 bleibt SN74LVC2G07DCKR / SC70-6.
- R18: eigener lokaler Footprint `AmiWiFi:R0402_R18`, statt des bauteilspezifischen R10-Eintrags.
- D2: gewähltes LED-0603-Handlöt-Lötbild und LED-Symbol lokal eingebunden. Keine globale `LED_SMD`- oder `Device`-Bibliothek für diese Bauteile erforderlich. LCSC-Feld C2909774 übernommen; keine Bauteil-/Lieferantenauswahl geändert.
- D2 und R18 mit Schaltplanpfad, Bauteilfeldern und Pad-Netzen in das Board übernommen. Beide stehen rechts neben der Platine: R18 (177,102 mm), D2 (182,102 mm). Platzierung und Routing stehen aus.
- Die bisher deaktivierte Prüfung `lib_footprint_mismatch` ist wieder als Warnung aktiv. Keine zusätzlichen Regeln unterdrückt.

## Verifikation

KiCad 10.0.7, DRC mit `--schematic-parity --all-track-errors`:

- 58 physische Schaltplansymbole / 58 Board-Footprints; alle IDs und Symbolpfade stimmen.
- 238 Pad-Netz-Zuordnungen stimmen exakt mit dem neu exportierten Schaltplan-Netzplan überein.
- 0 Bibliotheksabweichungen, 0 Schaltplanabgleich-Probleme, 0 sonstige DRC-Verstöße.
- **3 offene Verbindungen** der neuen LED-Schaltung: +3V3_IF → R18.1; R18.2 → D2.2/A; D2.1/K → GND. Dies sind weiterhin DRC-Fehler; das Board ist nicht fertig geroutet.
- ERC: 0 Fehler, **2 bestehende Warnungen** zu offenen Drahtenden bei (91,44;33,02) und (137,16;33,02) mm. Die zwei zusätzlichen Bibliothekswarnungen des Uploads sind behoben.
- Sämtliche 56 bestehenden Footprints, Leiterbahnen, Vias, Flächen einschließlich vorhandener Füllung, Umriss und sonstigen Boardobjekte sind strukturell identisch zum Upload. Die bestehenden PCB-Objektblöcke wurden bytegetreu erhalten. Schaltplan-Netze unverändert.
- Vorhandene Regel-Ausnahmen sind im nativen Bericht ausgewiesen; diese Prüfung ist keine elektrische Musterabnahme.

## Dateien und weitere Arbeit

Projekt: `hardware/AmiWiFi.kicad_pro`. Alle Dateien gemeinsam entpacken.
Aktuelle Repository-Berichte: `validation/2026-10-15-footprints/` und
`hardware/DRC.rpt`, `hardware/ERC.rpt`.
Die folgenden Archivpfade beziehen sich auf die korrigierte Download-ZIP.
Alte Vorschauen, Prüfberichte und Produktionsdateien liegen unter
`reference/Before_Footprint_Fix_2026-10-15/`; sie sind historische Daten.
Backups des Nutzerarchivs sind unverändert enthalten. Es wurden keine neuen
Gerber-/Fertigungsdateien erzeugt. Vor Fertigung D2/R18 platzieren und routen,
Flächen neu füllen, offene Drahtenden prüfen und ERC/DRC erneut ausführen.

Das lokale LED-Lötbild basiert auf KiCad/kicad-footprints,
`LED_SMD.pretty/LED_0603_1608Metric_Pad1.05x0.95mm_HandSolder.kicad_mod`
(Upstream master, abgerufen 15.10.2026; unveränderte Padgeometrie,
CC-BY-SA 4.0 mit KiCad-Ausnahme). Die optionale externe 3D-Modellreferenz wurde entfernt.
