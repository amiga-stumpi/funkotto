# Ethernet-Transport entwickeln, bevor die Platine verfügbar ist

Stand: 15.10.2026. Das ist M5a als Softwarevorbereitung; M0 und die aktive
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

## 3. Pico-Integration und USB-Testzugang – softwareseitig umgesetzt

Eigenes gesperrtes Ziel `funkotto_m5_usb` mit echten WLAN-Queues und FOC1.
TX/RX prüfen die Epoche unter demselben Lock wie die Queueoperation. Flashjobs
lassen die FOE1-Sitzung bestehen und halten abgebrochene TX-Abschlüsse abholbar.
FOC1-Jobzustand bleibt über FOE1-Sitzungswechsel erhalten. USB-Testmodus mit
COBS, begrenzten Puffern, Teilpaket-/Idle-Timeout, DTR-Abbruch und Boot-Seed.
Die Konsole bedient jeweils nur einen der RAW-/FOC1-/FOE1-Modi.

PC-Testwerkzeug mit Status, FOC1, ARP/ICMP, bis zu acht TX-Tickets und JSON/PCAP.
Host-/Clienttests und Pico-Build bestanden; **M5-USB-Grundprüfung am Pico bestanden**.
[Prüfbericht](results/2026-10-15-m5-usb.md) ·
[Testablauf](../firmware/M5_USB_TEST.md).

Am realen Pico bestanden: Status/FOC1, 12 Pings, 32 Pings mit Fenster 4/8 und
1000 Pings mit Fenster 4. Der Achter-Dauerlauf mit altem Client verlor 23/1000
Antworten. Der Client bedient deshalb jetzt RX zwischen TX-Anfragen und
TX-Abschlüssen; alle acht Tickets bleiben dabei weiterhin belegbar.
Die Wiederholung mit korrigiertem Client bestand mit 1000/1000 Antworten,
ohne abgeleiteten RX-Überlauf oder USB-Fehler, einschließlich PCAP-Nutzdatenprüfung.
[Clientkorrektur und Messauswertung](results/2026-10-15-m5-rx-client.md).
Ebenfalls bestanden: DISCONNECT/CONNECT, SAVE des bereits gespeicherten Profils,
Wiederverbindung und 32 Pings danach sowie Strg+C mit anschließenden 32 Pings
ohne angeforderten Pico-Neustart. Alle TX-Slots wurden freigegeben.
Die Messgrenzen und weiteren Fehlerfallprüfungen stehen im Prüfbericht;
die Grundprüfung ist keine vollständige Fehlerfall- oder Parallelportabnahme.

## 4. Amiga-Client und portfreie Diagnose – nächster Software-Schritt

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
