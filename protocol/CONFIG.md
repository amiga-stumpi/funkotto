# FOC1: Konfigurationsbefehle und USB-Testtransport

Version 1.0, erster Implementierungsstand 10.10.2026. Ziel `funkotto_m4_usb`.
Die Befehls-Payloads sind transportunabhängig (`config_protocol.c`); der
USB-Transport (`config_wire.c`) ist ein eigener, exklusiver Modus. Diese
Spezifikation erweitert weder das M2-B1536-Framing noch RAW v1. Eine spätere
M2-Anbindung muss die Fähigkeit explizit melden und Sitzung/Replay garantieren.

## USB-Sitzung

115200 Baud als CDC-Einstellung (USB selbst bestimmt die Übertragungsrate).
Aus der Textkonsole `config on` senden. Nur bei freiem WLAN-Kommando-Mailboxplatz
wird `CONFIG v1 wire=2; ...` ausgegeben; danach keine Textausgaben im Binärmodus.
RAW und CONFIG sind gegenseitig exklusiv. Der Host liest die vollständige
Begrüßungszeile vor seinem HELLO. Der Einstieg deaktiviert den Ethernet-Testempfang,
verändert aber weder Profil noch gewünschten WLAN-Verbindungszustand.

Rahmen: `00 COBS(header || payload || crc32) 00`. Leere Trennrahmen ignorieren.
CRC32/ISO-HDLC (reflektiertes Polynom 0xEDB88320, init/xorout 0xFFFFFFFF), über
Header und Payload; Testvektor ASCII `123456789` = 0xCBF43926. CRC als BE32.
Alle Mehrbytewerte big endian; signierte Werte Zweierkomplement. Keine gepackten
C-Strukturen, keine Nullterminierung von SSIDs, keine Passwort-Rückgabe.

| Headeroffset | Breite | Inhalt |
| --- | --- | --- |
| 0 | 1 | USB-Transportversion **2** (RAW bleibt 1) |
| 1 | 1 | Opcode; Antwort = Anfrage OR 0x80 |
| 2 | 2 | Payloadlänge 0..128 |
| 4 | 4 | Sequenz, beginnend bei 1; 0 unzulässig |
| 8 | 8 | Sitzung |
| 16 | variabel | Payload, anschließend 4 Byte CRC |

Maximal 148 decodierte Bytes, Empfangspuffer 150 COBS-Bytes. Ungültige Version,
CRC, COBS oder Länge: verwerfen, keinerlei Zustandsänderung. Überlauf verwirft bis
zur nächsten Null. Unvollständiger Rahmen wird nach mehr als 2 s ohne Eingabe
verworfen; erst ein Null-Delimiter stellt wieder eine Grenze her. DTR-Verlust
oder mehr als 60 s ohne Eingabe löscht Transportzustand/Replaypuffer und kehrt
zur Textkonsole zurück. Bereits angenommene Serviceaufträge laufen dabei weiter.

HELLO: Opcode 0, Sequenz 1, Sitzung 0, leere Payload. Antwort mit zufälliger,
nichtnulliger 64-Bit-Sitzung und INFO-Payload. Kein WLAN-Link und keine gültige
MAC als Voraussetzung. Danach Sitzung übernehmen und Sequenz um eins erhöhen.
Nur eine Anfrage gleichzeitig. Bei Antwortverlust denselben kompletten Rahmen
wiederholen. Byteidentische letzte Anfrage liefert die gecachte Antwort,
**ohne erneute Befehlsausführung**. Veränderte Wiederholung, Lücken, alte Sequenz
oder ein zweites HELLO nach anderen Befehlen werden abgewiesen. Kein Wrap nach
0xFFFFFFFF: neue Sitzung erforderlich. Wiederholte HELLO direkt nach HELLO sind
erlaubt. Ein Fehler SESSION/SEQUENCE verschiebt die erwartete Sequenz nicht.
Andere gültig sequenzierte Anfragen verbrauchen ihre Sequenz, auch bei BUSY.

Der Replaypuffer hält genau eine Anfrage und Antwort. Eine SET-Anfrage kann
bis zur nächsten angenommenen Anfrage (normalerweise JOB) das Passwort im RAM
enthalten; Ersetzen/Schließen löscht den Puffer. Parser-Zwischenpuffer und lokale
Profilkopien werden unmittelbar gelöscht. Die WLAN-RAM-/Flashkonfiguration
enthält weiter die für Verbindungen benötigten Zugangsdaten. Der USB-Kanal ist
weder verschlüsselt noch authentifiziert; keine echten Zugangsdaten mitschneiden.
Python und Betriebssystempuffer garantieren keine sichere Speicherlöschung.

## Antwortstatus (erstes Payloadbyte bei jedem Befehl)

| Wert | Bedeutung |
| --- | --- |
| 0 OK | Lesebefehl erfolgreich |
| 1 QUEUED | Änderungsauftrag angenommen; BE32-Auftragskennung folgt |
| 2 BUSY | Nicht angenommen; vorherigen Auftrag abwarten |
| 3 INVALID | Falsche Länge/Parameter |
| 4 UNSUPPORTED | Unbekannter Opcode |
| 5 STALE | Unbekannte Auftragskennung oder veraltete Scan-Generation |
| 6 EMPTY | Kein Scan-Eintrag an diesem Index |
| 7 SESSION | Falsche/geschlossene Sitzung |
| 8 SEQUENCE | Unzulässige Reihenfolge oder veränderte Wiederholung |

Fehlerantworten haben genau ein Byte. QUEUED hat genau fünf Bytes. Die folgenden
Offsets gelten jeweils ab Beginn der Antwortpayload, einschließlich Statusbyte.

## Befehle

| Opcode | Name | Anfragepayload | Erfolgsantwort |
| --- | --- | --- | --- |
| 1 | INFO | leer | Fähigkeiten, Format unten |
| 2 | STATUS | leer | WLAN-/Profil-/Scanstatus, Format unten |
| 3 | SCAN | leer | QUEUED + job |
| 4 | SCAN_GET | BE32 Generation + uint8 Index | Ein Eintrag, Format unten |
| 5 | SET_PROFILE | auth=1, SSID-Länge, Key-Länge (je uint8), SSID, Key | QUEUED + job |
| 6 | CONNECT | leer | QUEUED + job |
| 7 | DISCONNECT | leer | QUEUED + job |
| 8 | SAVE_PROFILE | leer | QUEUED + job |
| 9 | ERASE_PROFILE | leer | QUEUED + job |
| 10 | JOB | BE32 job | 8 Bytes: OK, BE32 job, ursprünglicher Opcode, pending, reply |

INFO (18 Bytes, auch HELLO): 0 Status; 1..4 ASCII `FOC1`; 5 Major=1; 6 Minor=0;
7..10 BE32 Fähigkeiten; 11..12 BE16 max_payload=128; 13 max_ssid=32;
14 min_key=8; 15 max_key=63; 16..17 ASCII Land `DE`.
Fähigkeitsbits: 0 Status, 1 Scan, 2 RAM-Profil, 3 Connect/Disconnect,
4 Flash-Save/Erase, 5 asynchrone JOB-Abfrage. Aktuell 0x0000003F.

SET: auth=1 bedeutet WPA2-AES-Personal. SSID 1..32 beliebige Bytes, Passwort
8..63 druckbare ASCII-Bytes (0x20..0x7E). Kein Open/WPA3, keine 64-stelligen
Hex-PSKs. SET beendet die bisherige Verbindung und ändert nur RAM; CONNECT
startet Verbindungsversuche; SAVE ist immer eine getrennte Benutzeraktion.
ERASE löscht RAM und persistentes Profil mit dem bestehenden W4-Verfahren.

Jobnummern starten je Sitzung bei 1, nur der jüngste angenommene Job bleibt
abfragbar. pending=1 bedeutet noch laufend, reply dann 255. pending=0 bedeutet
abgeschlossen: reply 0 OK, 1 BUSY, 2 NO_PROFILE, 3 DRIVER_ERROR, 4 INVALID,
5 FLASH_ERROR. Bis zum Abschluss wird kein weiterer Änderungsauftrag angenommen;
Leseabfragen funktionieren weiter. JOB-Antworten sind ebenfalls Replay-Snapshots:
Fortschritt mit neuer Sequenz abfragen, nicht mit Wiederholung derselben Abfrage.
Bei ausgeschöpften Jobnummern neue Sitzung beginnen. Der Dispatcher setzt einen
exklusiven Core-0-Auftraggeber voraus; USB erzwingt diese Zuständigkeit.

**Serviceabschluss ist nicht Linkbereitschaft:** CONNECT/OK aktiviert die
Verbindungsversuche; anschließend STATUS bis LINK_UP prüfen. SCAN/OK startet den
Scan; anschließend scanning=0, passende scan_done/scan_generation und sdk_error=0
abwarten. Bei gewünschter Verbindung/Autoretry kann SCAN mit Service-BUSY enden;
vorher ausdrücklich DISCONNECT ausführen. SAVE/ERASE-OK bestätigt dagegen den
abgeschlossenen Speicherauftrag einschließlich W4-Verifikation. Kein Exactly-once-
Versprechen über Stromausfall oder Sitzungsende: nach unklarem Ausgang Status
prüfen, niemals automatisch einen neuen Änderungsauftrag senden.

## STATUS (51 + SSID-Länge Bytes)

| Offset | Format | Inhalt |
| --- | --- | --- |
| 0 | u8 | OK |
| 1 | u8 | Zustand: 0 INITIALIZING, 1 UNCONFIGURED, 2 DISCONNECTED, 3 CONNECTING, 4 LINK_UP, 5 RETRY_WAIT, 6 ERROR |
| 2 | u8 | Fehler: 0 OK, 1 NO_PROFILE, 2 TIMEOUT, 3 BADAUTH, 4 NONET, 5 DRIVER, 6 LINK_LOST, 7 JOIN_FAILED |
| 3 | u8 Bits | 0 configured, 1 stored, 2 auto_retry, 3 mac_valid, 4 rssi_valid, 5 scanning, 6 sdk_busy, 7 command_busy |
| 4 | u8 | storage_state: 0 EMPTY, 1 PROFILE, 2 DELETED, 3 CORRUPT, 4 INCOMPATIBLE, 5 AMBIGUOUS |
| 5 | u8 Bits | 0 flash_profile, 1 flash_ready |
| 6 | 6 Bytes | MAC; nur gültig mit mac_valid |
| 12 | BE i32 | RSSI; nur gültig mit rssi_valid |
| 16 | BE i32 | sdk_error |
| 20/24/28 | je BE32 | epoch / attempts / links |
| 32/36 | je BE32 | storage_sequence / storage_error |
| 40/44 | je BE32 | scan_generation / scan_done |
| 48/49 | je u8 | scan_count / scan_truncated |
| 50 | u8 | SSID-Länge |
| 51 | Bytes | SSID, ohne Passwort |

storage_error: 0 OK, 1 IO, 2 VERIFY, 3 FORMAT, 4 NOT_READY.

SCAN_GET: 0 OK; 1..4 BE32 Generation; 5 Index; 6 Anzahl; 7 truncated;
8 auth (roher CYW43-Scanwert, **nicht** SET-auth); 9..10 BE16 Kanal;
11..12 BE i16 RSSI; 13..18 BSSID; 19 SSID-Länge; 20.. SSID-Bytes.
Versteckte SSID darf Länge 0 haben. Während des Scans BUSY; veraltete oder nullige
Generation STALE; Index >= Anzahl EMPTY. SSID/Scanwerte sind keine Terminal-
Steuersequenzen: das PC-Tool gibt sie JSON-escaped aus.
