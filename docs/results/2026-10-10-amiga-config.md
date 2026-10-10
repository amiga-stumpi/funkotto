# Amiga-Konfiguration: lokaler erster Stand, 10.10.2026

Implementiert: austauschbarer FOC1-Client, Produktionsdecoder mit lokalem
WLAN-Simulator, OS-1.3-Shell-Start, eigenes RAW:-Fenster, verdeckte Eingabe,
Status/Scan/Set/Connect/Disconnect/Save/Erase, simulierter Neustart, SELFTEST.
Plan: [amiga-konfiguration-plan.md](../amiga-konfiguration-plan.md).

## Ausgeführt

- Freestanding-68000-Build mit GCC 13.3.0, Warnungen als Fehler, HUNK-Relokationen
  geprüft. Keine SDK-/ROM-Verteilung und kein aktiver Paralleltransport.
- 20 Unicorn-68000-Durchläufe mit expliziten V34-OS-Mocks bestanden: kompletter
  Profilablauf, doppelte/versteckte Scan-SSIDs, verstecktes Passwort, Überlänge,
  Steuersequenzen, Backspace, minimale/maximale Profillängen, Abbruch/EOF, Open-/Library-/Write-Fehler,
  ungültige Argumente und Workbench-Nachrichtenrückgabe.
- Eigener gemessener Programmstack maximal 688 Bytes (OS-Stackbedarf nicht
  simuliert). Registererhalt und alle ausgeführten Word-/Long-Zugriffe auf
  68000-Ausrichtung geprüft. CIA-/Custom-Chip-Bereiche sind nicht gemappt.
- Native Clienttests unter ASan/UBSan: Lebenszyklus, fehlerhafte Antwortlängen,
  Job-ID, Überlänge, Timeout, Abbruch und keine blinde Wiederholung von SAVE.
  Gesamter Hosttest-Lauf erfolgreich; lokal Leak-Erkennung wegen eingeschränktem
  /proc deaktiviert, ASan/UBSan aktiv. CI verwendet die normalen Einstellungen.

Beim ersten Emulatorlauf fiel eine GCC-Store-Zusammenfassung auf: Bytefelder
im Produktionsdecoder wurden als Word/Long auf ungeraden Offsets geschrieben.
Für diesen Amiga-Build ist Store-Merging deaktiviert; Byte-Decoder verwenden
volatile Bytezugriffe. Die Pico-Firmware wurde dafür nicht verändert.

## Nicht abgenommen

Das Programm wurde noch nicht auf dem echten A500/Kickstart 1.3 ausgeführt.
RAW:-Handler und Fensterverhalten müssen dort geprüft werden. Die Simulation
ist weder ein WLAN-Test noch ein Flash-Powerfail-Test. M0, aktiver M2-Bus,
SANA-II und die spätere Intuition-Oberfläche bleiben offen.
