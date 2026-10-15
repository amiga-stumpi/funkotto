# F8-Verknüpfung korrigiert – 15.10.2026

Die erste Footprint-Korrektur hat die Bibliothekskopien und Felder angeglichen,
aber einen vorhandenen Fehler in allen vollständigen Symbol-Verknüpfungspfaden
übersehen. Der DRC-Schaltplanabgleich meldete hierfür keine Abweichung.
Die damalige Aussage, alle Symbolpfade seien korrekt, war deshalb falsch.

## Ursache und Änderung

Die Board-Footprints verwendeten `/Hauptschaltplan-UUID/Symbol-UUID`.
Die frisch mit KiCad 10.0.7 exportierte Netzliste verwendet für das Hauptblatt
`sheetpath.tstamps = /` und als Bauteilkennung die Symbol-UUID. Der passende
Boardpfad lautet deshalb `/Symbol-UUID`.

Alle 58 Boardpfade wurden auf genau den exportierten Pfad korrigiert.
Die UUIDs der Footprints selbst wurden nicht geändert. Positionen, Winkel,
Pads, Netze, Routing, Vias, Umriss und Flächen blieben vollständig unverändert.
Schaltplan und Bibliotheksgeometrien wurden in diesem Nachtrag nicht geändert.

## Prüfung

- Native KiCad-10.0.7-Suche `BOARD.FindFootprintByPath` mit vollständigen Pfaden
  aus der frisch exportierten Schaltplan-Netzliste: vorher 0/58 Treffer,
  danach 58/58 Treffer, jeweils die richtige Referenz.
- Der neue Prüfer `tools/check_schematic_links.py` schlägt mit dem bisherigen
  Board erwartungsgemäß fehl und besteht mit dem korrigierten Board.
- Ein Strukturvergleich lässt ausschließlich diese 58 Pfadänderungen zu.
- DRC einschließlich Schaltplanvergleich: 0 sonstige Verstöße, 0
  Schaltplanabweichungen; weiterhin 3 offene LED-Verbindungen.
- Der grafische F8-Dialog wurde hier nicht bedient. Geprüft wurden die
  vollständigen Zuordnungspfade mit KiCads nativer Suchfunktion.
- D2/R18 bleiben zur Platzierung neben dem Board. Die beiden bereits
  dokumentierten offenen Drahtenden im Schaltplan sind unverändert.

## Verwendung

Das vollständige korrigierte ZIP in einen neuen Ordner entpacken, alte
Projektfenster schließen und `hardware/AmiWiFi.kicad_pro` öffnen.
F8 soll die bestehenden Bauteile jetzt erkennen. Bei unverändertem Projekt
darf keine Liste mit 58 neu hinzuzufügenden Footprints mehr erscheinen.

Wenn seit dem letzten ZIP bereits weitergearbeitet wurde, kann stattdessen
das bestehende Board einmalig anhand der Referenzen neu verknüpft werden:

1. Vom Boardstand vor der versehentlichen Neuplatzierung ausgehen; falls nötig
   Aktualisierung rückgängig machen oder den zuletzt korrekten Stand öffnen.
2. F8: „Footprints anhand ihrer Referenzbezeichner erneut mit Schaltplansymbolen
   verknüpfen“ aktivieren (Wortlaut kann abweichen). „Footprints ohne Symbole
   löschen“ und „Footprints durch die in Symbolen angegebenen ersetzen“ für
   diesen Reparaturschritt deaktiviert lassen. Vorschau auf unerwartete
   Hinzufügungen prüfen, dann Board aktualisieren und speichern.
3. Anschließend die erneute Verknüpfung anhand der Referenzen wieder ausschalten.

Offizielle Erläuterung:
https://docs.kicad.org/10.0/en/pcbnew/pcbnew.html#forward-and-back-annotation

Reproduzierbare Prüfung:
```
kicad-cli sch export netlist -o current.net hardware/AmiWiFi.kicad_sch
python3 tools/check_schematic_links.py current.net
```
