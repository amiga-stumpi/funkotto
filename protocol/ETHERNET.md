# FOE1 – Ethernet-Transport, erste Softwarebasis (M5a)

Stand: 15.10.2026. Portabler C-Codec/Server und Pico-USB-Testziel
`funkotto_m5_usb` mit produktivem WLAN-Backend implementiert; Softwaretests
bestanden, echter Pico-Test offen. Kein CIA-/PIO-Transport oder SANA-II-Treiber.
USB-Abgrenzung und Testablauf: [M5_USB_TEST.md](../firmware/M5_USB_TEST.md).
Das Format wird vor der ersten Hardwareintegration gemeinsam versioniert;
es ersetzt weder M2/B1536 noch die vorhandenen USB-Protokolle stillschweigend.

## Ziele

- Variable Rahmenlänge statt 1536-Byte-Padding, Ethernet II bis 1514 Bytes ohne FCS.
  IP-MTU 1500. Kein VLAN, Jumbo, 802.3-LLC oder vom Host eingespeistes EAPOL in v1;
  die vorhandene WLAN-Queue prüft diese Einschränkungen und die Quell-MAC.
- Bis zu acht ausstehende TX-Aufträge, RX-Puffer mit acht Slots. WLAN-Senden
  blockiert nicht die Abfrage von Empfang, Status oder Konfiguration.
- Verlorene Antworten durch identische Wiederholung behandeln, ohne doppelte
  Queue-Einträge, doppeltes RX-Pop oder erneut ausgeführtes SET/SAVE/ERASE.
- Ereignisbenachrichtigung für den späteren ACK/CIA-Interrupt, kein Dauerpolling
  als Ziel für SANA-II. GPIO-Puls und realer Interrupt-Handler sind noch offen.
- FOC1-Konfiguration über denselben exklusiv besessenen Transport.

## Paketformat

Alle Mehrbytewerte Big Endian, kein C-Struct-Casting. Gesamtlänge `28 + length`,
maximal 1543 Bytes. Das Paket enthält keine COBS-Kodierung und kein Padding.

| Offset | Bytes | Feld |
| --- | ---: | --- |
| 0 | 4 | ASCII `FOE1`, enthält Formatversion 1 |
| 4 | 1 | Opcode, Antwort mit Bit 7 gesetzt |
| 5 | 1 | Flags, muss 0 sein |
| 6 | 2 | Payload-Länge, 0…1515 |
| 8 | 4 | Sequenz, 1…0xffffffff, kein Wrap |
| 12 | 8 | Sitzung, HELLO-Request mit 0 |
| 20 | 4 | WLAN-Epoche, HELLO-Request mit 0 |
| 24 | length | Payload |
| 24+length | 4 | CRC-32/ISO-HDLC über Header und Payload |

CRC: reflektiertes Polynom `0xedb88320`, Initialwert/finales XOR `0xffffffff`;
Prüfvektor `123456789` ergibt `0xcbf43926`. CRC dient der Fehlererkennung,
nicht der Authentifizierung. Sitzung und Nonce sind ebenfalls kein Zugangsschutz.

Jede Antwort beginnt mit einem Statusbyte. Werte 0…9 entsprechen den bisherigen
Ethernet-Queue-Ergebnissen:

| Wert | Name | Bedeutung |
| ---: | --- | --- |
| 0 | OK | Operation erfolgreich; bei TX zunächst nur Queue-Annahme |
| 1 | EMPTY | Kein RX-Paket |
| 2 | BUSY | Kein TX-Slot / Empfang oder Abschluss vor ARM noch abzuholen |
| 3 | NO_LINK | Datenpfad nicht bereit oder WLAN getrennt |
| 4 | INVALID | Ungültiges Format/Argument/Ethernet-Paket |
| 5 | ABORTED | TX abgebrochen oder Ticket nicht mehr vorhanden |
| 6 | DRIVER | Fehler bei Übergabe an den WLAN-Treiber |
| 7 | PENDING | TX läuft noch; nächste Abfrage mit neuer Sequenz |
| 8 | SESSION | Sitzung fehlt, stimmt nicht oder HELLO während aktiver Sitzung |
| 9 | SEQUENCE | Unerwartete Sequenz oder veränderte Wiederholung |
| 10 | UNSUPPORTED | Unbekannter Befehl oder nicht angebotenes CONFIG |
| 11 | EPOCH | WLAN-Epoche veraltet; STATUS lesen |
| 12 | STALE | ARM-Ereignisstand inzwischen geändert/übergelaufen |
| 13 | INTERNAL | Backend-Vertragsfehler oder ungültiger/erschöpfter Sitzungsseed |

Transport-/Sitzungsfehler antworten mit genau einem Statusbyte. Ungültige CRC,
Länge, Magic, Flags, Antwort-Bit im Request, Opcode 0 oder Sequenz 0 erzeugen
keine Antwort und führen keinen Auftrag aus. Der aufrufende Transport setzt
ein Zeitlimit und verwirft unvollständige Pakete, bevor er den Decoder aufruft.

## Sitzung, Sequenz und Wiederholung

Der Host hat genau einen offenen **Transportauftrag**, darf aber mehrere
angenommene **WLAN-Sendeaufträge** gleichzeitig verfolgen. HELLO hat Sequenz 1,
Sitzung/Epoche 0 und eine frische 8-Byte-Hostnonce. Der Server vergibt ein neues
64-Bit-Sitzungstoken aus einem vom Integrator frisch gesetzten Bootseed und
einem monotonen Zähler. Seed 0 und Überlauf werden abgelehnt. Neue Sitzung:
alte Ethernet-Queues verwerfen; WLAN-Profil und Funkverbindung bleiben bestehen.

Weitere Requests haben dieselbe Sitzung und exakt die nächste Sequenz. Der
Server speichert den letzten akzeptierten Request und seine vollständige Antwort.
Nur dessen byteidentische Wiederholung liefert exakt diese Antwort erneut.
Auch `PENDING`, `BUSY`, `EMPTY`, `EPOCH` und Argumentfehler werden gecacht und
verbrauchen die Sequenz. **Eine erneute Zustandsabfrage braucht eine neue Sequenz.**
SESSION/SEQUENCE sowie zurückgewiesene HELLOs verändern Sequenz und Cache nicht.

Der nächste Request gilt zugleich als Bestätigung, dass der Host die vorige
Antwort übernommen hat. Bei RX muss der Host die Daten deshalb vor der nächsten
Sequenz in seinen eigenen Puffer kopieren. Durch Wiederholung wird kein zweites
Paket entnommen. Nach einem verlorenen TX_RESULT bleibt das verbrauchte Ergebnis
im Antwortcache verfügbar. Der Cache ist ein Snapshot: nach WLAN-Wechsel kann
eine Wiederholung noch eine Antwort mit der alten Epoche enthalten.

Bei Timeout ausschließlich den vollständigen unveränderten Request innerhalb
derselben Sitzung erneut senden. Host prüft CRC, Opcode, Sequenz, Sitzung und
Antwortformat vor Übernahme. Antwort mit bekannter veralteter Epoche nicht als
neuen Linkzustand übernehmen. Kein unbegrenztes Retry; nach endgültigem Abbruch
Transportbesitz freigeben und neu synchronisieren. Keine automatische Wiederholung
von Mutationen über einen Sitzungswechsel hinweg.

Vor Sequenzüberlauf CLOSE verwenden; bei erschöpfter Sequenz kann nur noch ein
externer Transportreset die Sitzung beenden. `fo_el_reset()` löscht Cache und
Queues; der physische Transport muss vorher DMA/Busausgaben sicher stoppen.
Es gibt keine Exactly-once-Zusage über Reset/Stromverlust: bereits an WLAN
übergebene Pakete können trotz verlorener Antwort gesendet worden sein.

## Befehle

| Op | Befehl | Request-Payload | Antwort-Payload |
| ---: | --- | --- | --- |
| 1 | HELLO | Hostnonce[8] | OK, Nonce[8], Caps:u32, MaxFrame:u16, Slots:u8, MAC[6], Link:u8, MaxPayload:u16 |
| 2 | STATUS | leer | siehe unten, 29 Bytes |
| 3 | TX | Ethernet-Frame[14…1514] | OK, Ticket:u32; bei Fehler nur Status |
| 4 | TX_RESULT | Ticket:u32, ungleich 0 | Ergebnis:u8, SDK-Fehler:i32 |
| 5 | RX | leer | OK, Ethernet-Frame; bei Fehler nur Status |
| 6 | CONFIG | FOC1-Opcode:u8, FOC1-Request[0…128] | OK, vollständige FOC1-Antwort[1…128]; bei äußerem Fehler nur Status |
| 7 | ARM | EventGeneration:u32 | OK, EventGeneration:u32; bei Fehler nur Status |
| 8 | CLOSE | leer | OK |

HELLO-Caps: Bit 0 Ethernet, Bit 1 asynchrones TX, Bit 2 Ereignismodell,
Bit 3 optional FOC1. Die Ereignisfähigkeit beschreibt die Protokollschnittstelle;
sie bestätigt noch keine physisch installierte IRQ-Leitung in diesem Teststand.
Die aktuelle Epoche steht im Antwortheader. Nonce muss im Host exakt passen.

STATUS-Payload:

| Offset | Inhalt |
| ---: | --- |
| 0 | OK |
| 1 | WLAN-Link, 0/1 |
| 2 | Freie TX-Slots, 0…8 |
| 3 | Wartende RX-Pakete, 0…8 |
| 4 | Abzuholende TX-Abschlüsse, 0…8 |
| 5 | Ereignisbits |
| 6 | Benachrichtigung scharf, 0/1 |
| 7 | EventGeneration:u32 |
| 11 | MAC[6] |
| 17 | RX-Änderungszähler:u32 (Annahmen + Überläufe) |
| 21 | TX-Änderungszähler:u32 (OK + Fehler + Abbrüche) |
| 25 | FOC1-Abschlusszähler:u32 |

TX-OK bedeutet Pufferübernahme, nicht erfolgreiche Funkübertragung. Das Ticket
wird mit TX_RESULT verfolgt. Erst ein endgültiges Ergebnis gibt den Slot frei;
PENDING behält ihn. WLAN-Treiber-OK ist keine Empfangsbestätigung der Gegenstelle.
Kurze Ethernet-Frames werden vom bestehenden Backend auf 60 Bytes aufgefüllt;
FCS wird nicht über FOE1 übertragen. RX-Überlauf verwirft neue Pakete im Backend
und ist über den RX-Änderungszähler sowie später die WLAN-Statistik auswertbar.

TX/RX müssen die aktuelle Epoche angeben. Das Backend vergleicht **unter demselben
Lock wie die Queue-Operation** erneut, damit ein WLAN-Wechsel zwischen Snapshot
und Zugriff keine alten TX-Daten annimmt oder neue RX-Daten falsch etikettiert.
Andere Befehle ignorieren das Request-Epochenfeld, insbesondere TX_RESULT muss
Abbrüche aus der alten Epoche abholen können.

CONFIG trägt unverändert die FOC1-Payload aus [CONFIG.md](CONFIG.md). Äußeres
OK bedeutet nur erfolgreiche Befehlsweitergabe; FOC1-QUEUED/JOB und LINK_UP
müssen weiterhin separat ausgewertet werden. Der aktuelle Test nutzt dafür
einen kleinen Stub, noch keine Verbindung zur laufenden Pico-Konfiguration.
Ein ausstehender Flashjob wird durch Transport-CLOSE nicht rückgängig gemacht.
Der Integrator muss Backend-Jobzustand über Sitzungswechsel erhalten oder bis
zum Abschluss exklusiv sperren. Passwortbytes liegen bis zum nächsten akzeptierten
Request im Replay-Cache; dieser und Arbeitsbuffer werden beim Ersetzen/Reset
explizit gelöscht. Keine Zugangsdaten in Antworten oder Diagnoselogs.

## Ereignisse und späterer Interruptbetrieb

Bits: RX_READY=1, TX_DONE=2, LINK_CHANGED=4, CONFIG_DONE=8, EVENT_OVERFLOW=16.
RX/TX sind Zustände der Queues; Link-/Konfigurationsänderungen bleiben bis ARM
gespeichert. EventGeneration steigt, wenn seit dem letzten Snapshot neue
RX-Annahmen/-Drops, TX-Abschlüsse, FOC1-Abschlüsse oder Linkänderungen auftreten.

Hostablauf: STATUS lesen, RX und TX-Abschlüsse abholen, Konfigurationsjob/Link
bearbeiten; danach ARM mit der gesehenen Generation. ARM pollt vor dem Vergleich.
Neue Ereignisse in dieser Lücke führen zu STALE; nicht abgeholte RX/TX-Abschlüsse
zu BUSY. Danach erneut abarbeiten, nicht schlafen. Bei OK ist genau eine neue
Benachrichtigung scharf. Auch bereits wartende TX-Aufträge dürfen so auf ihren
späteren Abschluss warten. Generation überläuft nicht: bei Erschöpfung ist ein
Sitzungswechsel erforderlich.

Der Pico-Integrator ruft `fo_el_poll()` regelmäßig und `fo_el_take_irq()` zur
Entnahme einer Benachrichtigung auf. Änderungen nach erfolgreichem ARM werden
dadurch beim nächsten Poll erkannt. Nach einer Benachrichtigung bleibt der
Mechanismus bis zum nächsten ARM unscharf. Die spätere Umsetzung erzeugt daraus
einen definierten ACK_N-Puls; der Amiga-Handler weckt nur den Treiber-Task, der
Pakete und Replies abarbeitet. Keine Byteübertragung im Interrupt-Handler.
Pulsbreite, CIA-Ressourcenbesitz und Rennen beim Interrupt-Acknowledge müssen
zusätzlich im echten GPIO-/Treiberpfad geprüft werden. Sie werden hier nicht
durch die Software-Simulation als gelöst behauptet.

## Physischer Transport und offene Integration

FOE1 erwartet bereits abgegrenzte vollständige Pakete. Ein künftiges Profil
liest zunächst den 24-Byte-Header, begrenzt die deklarierte Länge, dann Payload
und CRC. Der Header allein autorisiert keine Aktion. Bei falscher Länge, Timeout,
abgebrochener DMA oder CRC-Fehlern muss der Buspfad begrenzt abbrechen und
resynchronisieren; nicht in beliebigen Bytes nach Magic suchen und weiterfahren.
Beide Seiten müssen das neue Profil explizit wählen. Das bestehende M2-PIO
wartet weiter auf 1536 Bytes und kann FOE1 **nicht unverändert übertragen**.

Es gelten weiterhin: Datenbus vor Richtungswechsel freigeben, OE zuerst sperren,
erst nach bestätigter Richtung aktivieren, Reset sperrt die Treiber. Exklusiver
Owner verhindert gleichzeitigen Zugriff durch USB-Rohpakettest und Amiga/SANA-II.
Das Konfigurationstool soll später über das Device gehen statt selbst die CIA
parallel zum Treiber zu bedienen.

CRC ist zunächst eine einfache Referenzimplementierung. Optimierter CRC-Code,
Paketbündelung, Burst-Profil und SANA-II-Hostzustandsautomat sind nächste Schritte.
Pico-Backend und USB-FOE1-Testzugang sind implementiert, realer Pico-Test offen. Variable Längen sparen
Übertragungsvolumen, sind aber keine Durchsatzmessung oder Geschwindigkeitszusage.
