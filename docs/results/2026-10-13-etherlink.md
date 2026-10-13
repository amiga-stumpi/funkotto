# M5a / FOE1: Software-Prüfstand, 13.10.2026

Implementiert: portabler variabler Ethernet-Paketcodec, Sitzungs-/Replay-Server,
asynchrone TX-Tickets, RX, FOC1-Hülle und Benachrichtigungs-/ARM-Zustandsautomat.
Noch nicht in ein Pico-Firmwareziel eingebunden. M2 und die ausgelieferten
USB-WLAN-Firmwares bleiben unverändert. [Plan](../ethernet-transport-plan.md).

## Ausgeführte Prüfungen

- Hosttests unter AddressSanitizer und UndefinedBehaviorSanitizer erfolgreich,
  einschließlich aller bisherigen Tests. Lokal `ASAN_OPTIONS=detect_leaks=0`
  wegen eingeschränkter Umgebung; Sanitizer ansonsten aktiv. CI nutzt Standard.
- FOE1 mit produktiver `firmware/src/ethernet.c`: acht TX-Slots, BUSY bei vollem
  Puffer, PENDING/Abschluss/SDK-Fehler/Abbruch, Padding, RX-Reihenfolge, voller RX-
  Puffer, identische Wiederholung ohne doppelte Queue-Operation.
- CRC-Prüfvektor, alle Payload-Längen 0…1515, ungerade Bufferadressen,
  alle Kürzungen und jede einzeln beschädigte Byteposition eines großen Requests,
  10.000 deterministisch erzeugte fehlerhafte Pakete sowie anschließende Erholung.
- Falsche Sitzung, veränderte Wiederholung, veraltete Epoche, Epochwechsel genau
  zwischen Snapshot und Queue-Zugriff, historische RX-Antwort nach Linkwechsel,
  Sequenzende, CLOSE/Reset und neue Sitzung.
- Ereignismodell: einmalige Benachrichtigung, volle Queues vor ARM, Ereignis
  zwischen STATUS und ARM, TX-Abschluss, WLAN-/Konfigurationsänderung und
  Ereigniszählerüberlauf.
- CONFIG nutzt einen kontrollierten FOC1-Stub: Weitergabe, QUEUED-Antwort,
  Wiederholung und Löschen von Request-/Arbeitsbuffern geprüft. Kein realer
  Pico-Flashjob über FOE1 ausgeführt.
- 32 Python-Tests bestanden, darunter drei neue unabhängige FOE1-Wire-Tests
  mit Python `struct` und `zlib.crc32`.
- Derselbe C-Test für 68000 übersetzt und unter Unicorn 2.1.4 ausgeführt;
  ausgewählte Grenzlängen/Korruptionen, vollständige Queue-/Sitzungs-/Ereignisfälle.
  Alle ausgeführten Word-/Long-Zugriffe auf gerade Adressen geprüft. Keine
  Linux-Laufzeit, OS-Aufrufe, ROMs, CIA- oder Custom-Chip-Zugriffe.

## Grenzen

Kein echter FunkOtto-Paralleltransfer, kein CIA-Interrupt, kein SANA-II-Treiber,
keine reale WLAN-Anbindung dieses Protokolls und keine Durchsatzmessung.
Ein C-Backend-Vertrag definiert insbesondere atomare Epochprüfung und Queuezugriff;
die Umsetzung im synchronisierten Pico-WLAN-Dienst ist noch ein eigener Schritt.
Die neue Spezifikation erteilt keine M0-/M2-Hardwarefreigabe.
