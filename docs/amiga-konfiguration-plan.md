# Amiga-Konfiguration: Plan und erster Stand

Ziel: WLAN-Konfiguration auf A500 / Kickstart 1.3 / Workbench 1.3 / 68020,
mit einem auch für den 68000 übersetzten Programm. Die Trägerplatine fehlt
noch; aktive Parallelport-Tests bleiben von M0 abhängig.

## Schritte

1. **FOC1-Client und austauschbarer Transport.** INFO, STATUS, SCAN/SCAN_GET,
   SET, CONNECT/DISCONNECT, SAVE/ERASE und JOB; Bytezugriffe für Big Endian
   und ungerade Protokolloffsets. Antworten vor der Anzeige prüfen.
2. **Lokaler Adapter-Simulator.** Produktionsdecoder `config_protocol.c`,
   simuliertes WLAN-Backend, verzögerte Jobs, Scan mit doppelter/versteckter
   SSID, RAM-Profil und simulierter Flash. Kein Zugriff auf CIA oder Datenträger.
3. **OS-1.3-Textprogramm.** Shell-Start, eigenes RAW:-Textfenster für verdeckte
   Passworteingabe; Status, Scan und Profilverwaltung. SELFTEST funktioniert
   direkt in der Shell. Keine Funktionen ab V36. Dies ist die erste Umsetzung.
4. **Prüfung und Paket.** Client-Negativtests, ausführbare HUNK-Datei unter
   68000-Emulation mit V34-OS-Mocks; Eingabe, Passwortausgabe, Abbruch, Fehler,
   Aufräumen und Registerkonvention. Danach manueller Test auf dem echten A500.
5. **Echter Adaptertransport – nach M0/M2-Hardwarefreigabe.** Konfigurations-
   kanal in den Paralleltransport integrieren, exklusiven Besitz gegenüber
   SANA-II koordinieren, Sitzungswechsel/Reset und ausstehende Jobs testen.
   Keine automatische Wiederholung unsicher abgeschlossener Mutationen.
6. **Bedienoberfläche.** Nach bestätigtem Textprogramm eine Intuition-Oberfläche
   für 1.3 mit Netzwerkliste, Passworteingabe, Status und Speicheraktionen.

## Umfang des ersten Pakets

`FunkOttoConfig SELFTEST` prüft den lokalen Protokollablauf.
`FunkOttoConfig SIM` startet eine interaktive Simulation. Speichern bedeutet
hier ausschließlich ein zweites RAM-Profil im laufenden Programm. `reboot`
simuliert einen Adapterneustart und übernimmt dieses Profil; nach `quit` ist
alles weg. Weder ein Pico noch ein SANA-II-Treiber werden dabei angesprochen.
Die vorhandene Pico-Firmware e92d982f16d1 muss dafür nicht geändert werden.

Der USB-Konfigurationspfad bleibt das PC-Testwerkzeug. Die gemeinsame Grenze
für PC und Amiga ist die FOC1-Befehlsebene; USB-COBS-Framing gehört nicht in
den Amiga-Bediencode. Der Amiga-Client wartet mit DOS Delay auf Jobs und
Verbindungsstatus (begrenzt auf etwa 30 Sekunden), statt die CPU zu beschäftigen.
Dies legt nicht den späteren Interruptbetrieb des SANA-II-Datenpfads fest.

## Grenzen und Abnahmekriterien

- Erstfassung: ASCII-Eingabe für SSID; 1–32 Bytes, Leerzeichen erlaubt. Schlüssel
  8–63 druckbare ASCII-Zeichen, keine Kommandozeilenoption dafür, kein Echo und
  keine Dateiablage. Empfangene SSIDs werden unabhängig davon byteweise sicher
  dargestellt. Scan-Auswahl nach BSSID und nicht-ASCII-Eingabe folgen später.
- `set` bearbeitet das RAM-Profil. `save` und `erase` verlangen im Programm YES.
  Gleiche gespeicherte Zugangsdaten erhöhen die simulierte Flash-Sequenz nicht.
- Abbruch/Timeout kann eine bereits angenommene Aktion nicht zurücknehmen.
  Kein automatisches erneutes SET/SAVE/ERASE nach Transportfehlern.
- Ein bestandener Emulatorlauf ist kein Test von Kickstart 1.3, dem realen
  RAW:-Handler, WLAN oder elektrischen Signalen. Diese Freigaben getrennt halten.
- Reale Erstabnahme: SELFTEST, Scanliste, verstecktes Testpasswort, Connect,
  Save/Reboot, Erase/Reboot und Beenden ohne Guru. Anleitung unter
  `amiga/config/README.md`.

## Quellen für die OS-Schnittstelle

- AmigaDOS-Dokumentation zu CON:/RAW: und byteorientierter Eingabe:
  https://wiki.amigaos.net/wiki/AmigaDOS_Introduction
- Console-Device-Dokumentation zu Eingabebytes und Sondertasten-Sequenzen:
  https://wiki.amigaos.net/wiki/Console_Device

RAW: ist ein eigenes Fenster; die Shell muss nicht in einen anderen Modus
geschaltet werden. Der Parser verwirft Zeilen mit Steuersequenzen oder Überlänge
vollständig. Unterstützt werden Return, Backspace und Abbruch, kein Cursor-Editor.
