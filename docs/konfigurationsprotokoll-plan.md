# M4-Konfigurationsprotokoll und USB-Testzugang

Stand: 10.10.2026. Die Trägerplatine fehlt; M0 und die aktive M2-Abnahme bleiben
unverändert offen. W5 bleibt ausgelassen. Dieser Schritt verwendet nur den Pico
2 W am PC-USB und gibt keine Parallelportleitung frei.

## Umsetzung

1. **Gemeinsamer Kommandokern:** byteweise definierte, versionierte Formate für
   Fähigkeiten, Status, Scan, RAM-Profil, Connect/Disconnect, Save/Erase und
   Auftragsabfrage. Bestehenden WLAN-Service und W4-Journal wiederverwenden;
   keine Änderung des Flashformats. Befehlsannahme, Serviceabschluss und
   WLAN-Linkbereitschaft sind unterschiedliche Zustände.
2. **Eigener USB-Modus:** explizit `config on`, neue Transportversion, HELLO mit
   Sitzung, CRC32/COBS, begrenzte Pakete, eine offene Übertragung und exakte
   Wiederholung der letzten Anfrage. DTR-Verlust und Inaktivität beenden die
   Sitzung. Kein automatisches Wiederholen von Änderungen in einer neuen Sitzung.
3. **PC-Werkzeug:** Fähigkeiten vor Änderungen prüfen, Status/Scan auslesen,
   Passwort verdeckt abfragen, RAM-Profil setzen und separat verbinden/speichern.
   Flash-Löschen nur explizit. Keine Passwörter als CLI-Argumente oder in Reports.
4. **Tests/Build:** echte C-Handler mit simuliertem WLAN-Service prüfen;
   fehlerhafte Rahmen, Längen, Sequenzen/Sitzungen, verlorene Antworten,
   Queue-BUSY, verzögerte Aufträge, Scan-Generationen und Passwortfreiheit der
   Antworten abdecken. Python-Client gegen den C-Server prüfen. Pico-Release
   bauen und ELF/UF2 auf GPIO-/Flash-Invarianten prüfen.
5. **Nutzerprüfung:** Status und Scan am einzelnen Pico, dann RAM-Profil,
   Connect/Disconnect, bewusstes Speichern, Kaltstart und optional Löschen.
   Reale Ergebnisse getrennt von Simulation/Build dokumentieren.

## Lieferumfang dieser ersten Umsetzung

Eigenes Ziel `funkotto_m4_usb` auf W4-Basis, transportunabhängiger Dispatcher,
USB-Framing, `tools/wifi_config.py`, Protokollspezifikation, Hosttests und
Bedienungsanleitung. Bestehende W4-Konsole und `raw on` bleiben verfügbar.
Die gegenseitig exklusiven USB-Modi besitzen die WLAN-Kommandos jeweils allein.

## Stand der Umsetzung

Schritte 1–4 sind softwareseitig umgesetzt, lokal geprüft und in der GitHub-CI
bestanden. Schritt 5 begonnen: realer HELLO-/Statustest mit LINK_UP, gespeichertem
Profil (Sequenz 3) und fehlerfreiem USB-Austausch bestanden. Disconnect und
vollständiger Scan mit fünf Einträgen und anschließender Reconnect bis LINK_UP
ebenfalls bestanden (`attempts=2`, `links=2`, weiterhin keine USB-Fehler). Profiländerungen/Speicherbefehle
über das neue Werkzeug sowie separater M4_USB-Kaltstart stehen noch aus. [Prüfbericht](results/2026-10-10-m4-usb.md).

## Danach

Amiga-Client (68000/V34) mit explizitem lokalem Simulationsmodus, anschließend
Anbindung an den realen M2-Transport nach M0/M2-Messungen. Intuition-Oberfläche
und SANA-II bleiben Folgearbeiten. Ein erfolgreicher USB-Test ist keine
Amiga-Parallelport-Abnahme und keine vollständige M4-Hardwareabnahme.
