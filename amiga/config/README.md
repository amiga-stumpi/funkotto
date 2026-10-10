# FunkOttoConfig – erster Amiga-Teststand

Shell-Programm für 68000 und neuer, Ziel Kickstart/Workbench 1.3.
**Diese Ausgabe arbeitet nur mit einem lokalen simulierten Adapter.**
Keine Platine und kein Pico erforderlich. Keine CIA-Zugriffe, keine echten
WLAN-Verbindungen und keine Dateien mit Zugangsdaten. Vorhandene Firmware
kann unverändert bleiben. Bitte nur ein erfundenes Testpasswort verwenden.

## Auf dem A500 starten

ZIP auf dem PC entpacken und `FunkOttoConfig` binär auf den Amiga kopieren.
In der Amiga-Shell in das Verzeichnis wechseln. Falls das Kopierwerkzeug das
Ausführungsrecht nicht erhalten hat: `Protect FunkOttoConfig +e`.

```text
Stack 8192
FunkOttoConfig SELFTEST
FunkOttoConfig SIM
```

SELFTEST muss mit dieser Zeile enden:

```text
PASS: local FOC1 configuration selftest; no CIA access.
```

SIM öffnet ein eigenes RAW:-Textfenster. Bitte in diesem Fenster tippen.
Es übernimmt das Echo selbst, damit das Passwort verborgen bleibt und die
aufrufende Shell ihren Eingabemodus behält. Workbench-Doppelklick wird in dieser
Ausgabe noch nicht unterstützt. Das Fenster mit `quit` schließen.

## Testablauf

Jeden Befehl und jede Eingabe mit Return abschließen:

1. `status`: anfangs `UNCONFIGURED`, `configured=0`, `stored=0`.
2. `scan`: drei künstliche Ergebnisse, zweimal `Demo WLAN` mit unterschiedlichen
   BSSIDs und Kanälen sowie eine versteckte SSID. Das ist keine Umgebungsmessung.
3. `set`: SSID `Demo WLAN`, danach ein erfundenes Passwort wie `test1234`.
   Das Passwort darf nicht auf dem Bildschirm erscheinen. `set` verbindet noch
   nicht und speichert noch nicht im simulierten Flash.
4. `connect`: `LINK_UP` und `stored=0`.
5. `save`, danach `YES`: simuliertes Profil gespeichert. `status` zeigt
   `stored=1`, `sequence=1`. Ein zweites identisches Save erhöht die Sequenz nicht.
6. `reboot`, danach gegebenenfalls zweimal `status`: simulierte automatische
   Wiederverbindung mit gespeichertem Profil. Keine echte Hardware wird resettet.
7. `disconnect`, danach `scan`: Scan wieder möglich. Ein Scan bei aktivem
   Verbindungswunsch liefert absichtlich `job_reply=1` (BUSY).
8. `erase`, danach `YES`, `reboot`, `status`: `UNCONFIGURED`, `stored=0`,
   `flash_state=2`, `sequence=2` (wenn zuvor genau ein neues Profil gespeichert wurde).
9. `quit`: Fenster zu, Rückkehr in die Shell ohne Absturz.

Die simulierten Daten leben nur bis zum Beenden. Ein neuer Programmstart beginnt
leer; `reboot` testet das Laden des simulierten Flash innerhalb derselben Sitzung.

Zusatzprüfungen: Backspace korrigiert die letzte Eingabe; ein kurzes Passwort oder
zu lange SSID wird abgelehnt. Cursor-/Funktionstasten und andere Steuersequenzen
verwerfen die ganze Zeile. ASCII-SSID mit Leerzeichen ist möglich, nicht-ASCII-
Eingabe und Auswahl direkt aus der Scanliste folgen später. Ctrl-C/Ctrl-D beendet
das Programm; je nach Handler wird ein externer Break erst bei Rückkehr aus Read
ausgewertet. `quit` im Fenster ist der normale Weg zum Beenden.

## Fehler und Grenzen

`client=-1`: Transportfehler; `-2`: ungültige Antwort; `-3`: Wartezeit abgelaufen;
`-4`: Abbruch; `-5`: Protokoll- oder Jobfehler. `job_reply=255` bedeutet, dass kein
abgeschlossenes Jobergebnis vorliegt. Bei einem Timeout/Abbruch kann eine zuvor
angenommene Aktion schon erfolgt sein. Mutationen werden nicht blind wiederholt.

Der Transport besitzt eine separate Austauschfunktion und eine wartende Funktion.
Der Client nutzt dieselbe FOC1-Payload-Spezifikation wie das PC-USB-Werkzeug; der
Simulator verwendet direkt den Produktionsdecoder der Firmware. Ein künftiger
Paralleltransport ist noch **nicht implementiert**. Er folgt nach M0-/M2-Abnahme.
SANA-II, Intuition-GUI und echter WLAN-Betrieb vom Amiga sind weitere Schritte.

## Entwicklung und Prüfung

```sh
python tools/build_amiga_config.py --source-id development
python tests/run_config_amiga_emulation.py build/amiga-config/FunkOttoConfig
python tests/run_host_tests.py
python tools/package_amiga_config.py build/amiga-config build/funkotto-amiga-config.zip
```

Compiler und Emulator sind wie beim M2-Diagnosetool in der CI festgelegt:
GCC 13.3.0 m68k, Binutils 2.42 und Unicorn 2.1.4. Kein Amiga-SDK, Linux-Laufzeitcode
oder Kickstart-ROM wird mitgeliefert. `-fno-store-merging` verhindert zusammengefasste
Stores auf ungeraden Protokolloffsets. Der HUNK-Test kontrolliert tatsächlich alle
Word-/Long-Zugriffe der ausgeführten Pfade auf 68000-Ausrichtung.

Die lokalen Prüfungen verwenden OS-Mocks; der erste echte OS-1.3-Test dieses
Programms steht aus. Ein früherer erfolgreicher FunkOttoDiag-Test zählt nicht
als Abnahme von FunkOttoConfig. Bitte die SELFTEST-Ausgabe und Auffälligkeiten
beim obigen SIM-Ablauf zurückmelden.
