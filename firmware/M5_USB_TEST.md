# M5-USB: FOE1 mit dem echten WLAN-Dienst testen

Stand: 15.10.2026. Eigenes Ziel `funkotto_m5_usb.uf2`, Pico 2 W.
Softwaretests und Build bestanden; der reale Pico-Test dieser Version steht aus.
Die Trägerplatine wird nicht benötigt. Der Parallelport bleibt gesperrt.

## Installation und erster Test

1. ZIP vollständig entpacken. Andere Terminalprogramme auf COM6 schließen.
2. Pico per BOOTSEL starten und `funkotto_m5_usb.uf2` auf das USB-Laufwerk kopieren.
   Nur diese UF2 verwenden, kein Flash-Löschprogramm. Die geprüfte UF2 schreibt
   nicht in die vorhandenen W4-Profilsektoren. Das gespeicherte WLAN-Profil und
   Autoconnect bleiben nutzbar. Bei späterem Test im Sockel gelten die bisherigen
   Hinweise zur getrennten Trägerversorgung in W4_TEST.md.
3. USB neu verbinden, auf WLAN-Verbindung warten. Python-Abhängigkeit einmalig:

```bat
python -m pip install pyserial==3.5
python tools/etherlink_diag.py --port COM6 --report m5-status.json
```

Erwartet: `stage=M5_USB`, `passed=true`, `hello.protocol=FOE1`,
`config_info.protocol=FOC1`, `wifi.state=LINK_UP`, `after.link=true`,
`after.tx_free=8`, `after.tx_done=0`, `usb_bad_frames=0`, `usb_retries=0`.
Ein Statuslauf kann auch ohne WLAN-Link bestehen; für Pakettests ist LINK_UP nötig.

Ein noch nicht eingerichteter Pico lässt sich mit dem mitgelieferten bisherigen
`wifi_config.py` konfigurieren; `config on`, `raw on` und die Textkonsole bleiben
als getrennte Modi vorhanden. Immer nur ein Programm öffnet den seriellen Port.

## ARP und Ping durch FOE1

Die Test-IP muss frei sein und darf nicht gleichzeitig auf PC/Amiga verwendet
werden. Beispiel mit dem bisher verwendeten Netz, sofern `.234` weiterhin frei ist:

```bat
python tools/etherlink_diag.py --port COM6 --source-ip 192.168.25.234/24 --target-ip 192.168.25.1 --count 12 --report m5-ping.json --pcap m5-ping.pcap
```

Es werden ARP-Konfliktproben, ARP-Auflösung und ICMP mit 0/56/512/1472 Bytes
Nutzdaten ausgeführt. Damit reicht der Test bis Ethernet 1514 / IP-MTU 1500.
Erwartet: 12/12 Antworten, kein Verlust, unveränderte WLAN-Epoche,
am Ende `tx_free=8` und `tx_done=0`. Bei Konflikt eine andere freie IP wählen.
Die IP-Protokolle laufen im PC-Werkzeug; der Pico erhält keinen eigenen IP-Stack.

## Mehrere asynchrone Sendeaufträge

```bat
python tools/etherlink_diag.py --port COM6 --source-ip 192.168.25.234/24 --target-ip 192.168.25.1 --count 32 --window 4 --report m5-window4.json
python tools/etherlink_diag.py --port COM6 --source-ip 192.168.25.234/24 --target-ip 192.168.25.1 --count 32 --window 8 --report m5-window8.json
```

`window` ist die Zahl ausstehender TX-Tickets, nicht die Zahl gleichzeitig
laufender USB-Anfragen. Auch ein bereits gesendetes Paket hält seinen Slot bis
zur Abholung des Abschlusses. Bei window=8 muss `maximum_outstanding=8` und
`minimum_tx_free=0` erscheinen, danach werden alle Slots wieder freigegeben.
Ein voller RX-Puffer kann in einem stark mit Broadcasts belegten WLAN zu Verlusten
führen; der Test meldet sie als Fehler. JSON-Reports zur Auswertung zurückgeben.

Anschließend kann der bewährte Dauerlauf wiederholt werden:

```bat
python tools/etherlink_diag.py --port COM6 --source-ip 192.168.25.234/24 --target-ip 192.168.25.1 --count 1000 --report m5-1000.json --pcap m5-1000.pcap
```

## Konfiguration durch dieselbe FOE1-Schnittstelle

```bat
python tools/etherlink_diag.py --port COM6 --config-action disconnect --report m5-disconnect.json
python tools/etherlink_diag.py --port COM6 --config-action connect --report m5-connect.json
python tools/etherlink_diag.py --port COM6 --report m5-reconnected.json
python tools/etherlink_diag.py --port COM6 --config-action save --report m5-save.json
```

CONNECT bestätigt zunächst den Konfigurationsjob; LINK_UP kann später folgen.
SAVE darf die Verbindung kurz anhalten und neu aufbauen. `completion.result=OK`
ist die Jobbestätigung. Ein anschließender Status- und Pinglauf prüft die
Wiederverbindung. Der Report enthält die SSID, aber kein Passwort.
Für Set/Scan/Erase bleibt `wifi_config.py` verfügbar; dessen explizite
Löschbestätigung wird nicht durch dieses Diagnoseprogramm ersetzt.

## Abbruch, Wiederholung, Wiederanlauf

- Bei verlorener Antwort wiederholt der Client nur exakt dieselbe USB-Anfrage.
  TX-Aufträge werden dadurch nicht doppelt in die WLAN-Queue eingetragen.
- Bei Sitzungsverlust oder ausbleibender Antwort nach begrenzten Wiederholungen
  bricht er ab. Keine automatische erneute Einlieferung von Ethernet-Paketen
  nach einem unklaren Ergebnis.
- Port schließen/DTR Low: FOE1-Besitz freigeben, Queues und Transportpuffer leeren.
  Ein bereits angenommener Konfigurationsjob läuft zu Ende; sein Zustand liegt
  außerhalb der FOE1-Sitzung. Bei „BUSY“ dessen Abschluss abwarten.
- Nach 60 Sekunden ohne USB-Eingabe kehrt die Firmware zur Konsole zurück.
  Teilpakete werden nach 2 Sekunden verworfen, bis eine Nullgrenze eintrifft.
- Bei AP-Ausfall darf der Paketlauf mit NO_LINK/EPOCH/ABORTED abbrechen. Nach
  Wiederverbindung einen neuen Status- und Pinglauf starten.
- Nach Pico-Stromverlust einen neuen Lauf starten; die alte Sitzung ist ungültig.

## Technischer Transport

Expliziter Konsolenbefehl `etherlink on`, Begrüßung `ETHERLINK FOE1 COBS;...`.
Ein USB-Paket ist `0x00 + COBS(FOE1-Paket) + 0x00`. Das innere Paket bleibt
unverändert: 24-Byte-Header, höchstens 1515 Bytes Payload, 4-Byte-CRC32;
maximal 1543 Bytes vor bzw. 1551 Bytes nach COBS (ohne äußere Nullbytes).
Parser und Antwortpuffer sind statisch und begrenzt. Keine Konsolenausgaben im
Binärmodus. CLOSE lässt den Modus für eine identische Wiederholung offen;
DTR Low oder Idle-Timeout beendet ihn. Die Sitzungskennung wird pro Boot zufällig
initialisiert und bei jeder neuen HELLO-Sitzung weitergezählt.

USB nutzt Abfragen, keine unaufgeforderten Ereignispakete und keine ACK_N-Pulse.
Das FOE1-Ereignismodell ist angebunden, der elektrische IRQ-Pfad bleibt offen.
Diese USB-Latenzen sind kein Amiga-Durchsatz- oder CPU-Lastnachweis.

Nächster Software-Schritt: portabler Amiga-FOE1-Client mit Simulation und
68000-Prüfung. Echter Paralleltransport und SANA-II folgen nach den jeweiligen
Abnahmen; M0 und aktive M2-Messungen bleiben offen. W5 bleibt ausgelassen.
