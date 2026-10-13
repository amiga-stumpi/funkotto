# Ethernet-Transport entwickeln, bevor die Platine verfügbar ist

Stand: 13.10.2026. Das ist M5a als Softwarevorbereitung; M0 und die aktive
M2-Abnahme bleiben Voraussetzung für die reale Parallelportintegration.
W5 bleibt wie vereinbart ausgelassen und ist nicht mit M5 gleichzusetzen.

## 1. Protokollbasis – umgesetzt

FOE1 legt variable Paketlängen, CRC32, Sitzungen/Sequenzen und WLAN-Epochen fest.
HELLO handelt Fähigkeiten aus; STATUS zeigt Puffer und Ereignisse. TX nimmt
bis zu acht WLAN-Aufträge an, TX_RESULT holt deren Abschlüsse, RX ein Paket.
CONFIG kapselt FOC1. ARM bereitet Benachrichtigung nach abgearbeiteten Ereignissen
vor; CLOSE beendet den exklusiven Ethernet-Besitz.

Implementierung: `protocol/etherlink.c` / `.h`,
[vollständige Spezifikation](../protocol/ETHERNET.md).
Kein automatischer Netzwerkzugriff, keine Hardwarefreigabe und kein Firmwarewechsel.

## 2. Tests ohne Platine – umgesetzt

- Native Hosttests mit den echten W3-Queues: Rückdruck, verlorene Antworten,
  doppelte Requests, Linkwechsel, CRC-/Längenfehler, Session-/Sequenzwechsel.
- Benachrichtigungsmodell: RX, TX-Abschluss, WLAN-/Konfigurationsänderung;
  Ereignisse zwischen Abholen und ARM dürfen nicht verloren gehen.
- Unabhängige Python-Referenz für Header, Bytefolge und CRC.
- Derselbe C-Lebenszyklustest auf einem emulierten 68000, inklusive ungerader
  Paketadressen. Keine OS-/CIA-/PIO-Zeiten werden dadurch getestet.

[Prüfstand](results/2026-10-13-etherlink.md).

## 3. Pico-Integration und USB-Testzugang – nächster Schritt

Ein eigenes gesperrtes Testziel verbindet den Backend-Vertrag mit dem laufenden
WLAN-Dienst. TX/RX benötigen neue atomare Epoch-Prüfungen unter dem Queue-Lock;
die derzeitigen `fo_net_*`-Funktionen allein reichen dafür noch nicht aus.
FOC1-Dispatcher anbinden, Jobzustand bei Sessionverlust erhalten, exklusiven
Besitz gegenüber USB-RAW und Konsole prüfen. Session-Seed pro Boot initialisieren.
FOE1 über einen expliziten USB-Testmodus mit klarer Paketabgrenzung testen:
ARP, ICMP, mehrere ausstehende TX-Aufträge, voller RX-Puffer, Link-/Resetfälle.
Der Parserkern selbst besitzt bewusst keine unbeschränkte Bytesuche/Streamsitzung.

## 4. Amiga-Client und portfreie Diagnose – danach

Host-Zustandsautomat mit Zeitgrenzen, begrenzten identischen Retries und strikter
Reply-Zuordnung entwickeln. Tickets, RX-Pufferbesitz und Epochwechsel verwalten;
mehrere Backend-Aufträge bei nur einem offenen Transportrequest. Konfiguration
über dieselbe Owner-Schnittstelle führen. Simulator für Paket- und IRQ-Ereignisse
an den künftigen SANA-II-Request-/Abort-/Close-Lebenszyklus anschließen.

## 5. Elektrischer Transport – nach M0/M2

Neues explizit gewähltes PIO-/DMA-Profil mit Header-/Nutzlängenphase entwickeln.
Das B1536-Diagnoseprofil unverändert weiterführen. Richtungswechsel, OE-Sperren,
Teilpakete, Timeouts und Reset am Muster messen. ACK_N-Benachrichtigung und
CIA-Interrupt-Handler separat testen, einschließlich verpasster Pulse und
gleichzeitiger RX/TX/Config-Ereignisse. Datenübertragung im Treiber-Task.

## 6. Optimierung – nach funktionaler Abnahme

Rohdurchsatz, TCP-/UDP-Nutzdurchsatz und CPU-Last getrennt messen. Dann
68000-Assembler für Portschleifen/CRC, Burst-Übertragung und gegebenenfalls
gebündelte Antworten vergleichen. Die erste Referenzimplementierung ist noch
kein schneller Datenpfad. 28 Bytes Rahmenaufwand ersetzen das feste M2-Padding;
das allein garantiert keinen bestimmten Durchsatz oder Vorsprung gegenüber Plipbox.
