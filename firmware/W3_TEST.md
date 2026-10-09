# FunkOtto W3 – Ethernet über USB prüfen

Firmware `0.1.0-w3`, Ziel `funkotto_w3`, Pico 2 W. W3 implementiert Roh-Ethernet
mit je acht festen TX-/RX-Puffern. Reale Tests am 09.10.2026 mit 12/12 und
anschließend 1.000/1.000 geprüften Antworten einschließlich MTU 1500 bestanden.
Der 1.000er-Lauf wurde anhand von JSON und PCAP geprüft. Langzeitbetrieb,
Latenzausreißer und Linkverlust unter Rohdatenlast bleiben offen.
Der Amiga-Bus bleibt gesperrt; WLAN-Zugangsdaten bleiben ausschließlich im RAM.

## 1. Flashen und WLAN verbinden

Pico herausnehmen oder **DB25 und USB-C-Trägerversorgung abziehen**, bevor sein
eigener USB-Port verwendet wird. BOOTSEL halten, Pico mit PC verbinden,
`funkotto_w3.uf2` auf `RP2350` kopieren. Kein Update über den Amiga.

Serielles Terminal öffnen (115200, keine Flusssteuerung, lokales Echo aus).
Befehle wie bei W1/W2:

```text
info
status
wifi set
```

SSID und danach Passwort auf die jeweilige Aufforderung eingeben; danach:

```text
wifi connect
wifi status
```

Auf `wifi=LINK_UP` warten. `status` muss weiterhin `bus_locked=1` und
`output_mask=0x00700000` melden. Terminal **schließen**, aber Pico angeschlossen
lassen: Trennen der Stromversorgung löscht das RAM-Profil. Zugangsdaten werden
weder vom PC-Testprogramm benötigt noch in dessen Bericht gespeichert.

## 2. USB-Verbindung prüfen

ZIP vollständig entpacken. Python 3.10 oder neuer und pyserial installieren:

```text
python -m pip install pyserial==3.5
python tools/wifi_diag.py --port COM5
```

`COM5` durch den Pico-Port ersetzen; unter Linux z. B. `/dev/ttyACM0`.
Das Programm aktiviert `raw on` selbst. Es zeigt Stations-MAC, Link, Link-Epoche
und Paket-/Fehlerzähler. Dieser Aufruf prüft nur USB/Status, noch keine LAN-Pakete.
Nach Programmende kann das normale Terminal wieder geöffnet werden.

Beim Werkzeug aus `b09aa6182ef8` kann einmalig `usb_bad_frames=1` erscheinen:
Die Textbegrüßung vor dem Binärmodus wird im Host-Fehlerzähler mitgezählt.
Das ist bei sonst gültigen Antworten und `usb_retries=0` allein kein Beleg für
einen Übertragungsfehler. Zusätzliche Fehler im laufenden Test gesondert prüfen.

## 3. ARP und ICMP über eine LAN-Gegenstelle testen

Benötigt werden eine erreichbare IPv4-Gegenstelle im selben Subnetz, die Ping
beantwortet, und eine **freie Test-IP** für den Pico. Die Test-IP nicht vom PC
oder einem anderen Gerät übernehmen; im Router prüfen und möglichst außerhalb
des DHCP-Pools wählen. Das Programm sendet vor Verwendung drei ARP-Probes und
bricht bei erkanntem Adresskonflikt ab. Ein ausbleibender ARP-Konflikt beweist
nicht, dass ein ausgeschaltetes Gerät die Adresse nie verwendet.

Beispiel, **Adressen und Port an das eigene LAN anpassen**:

```text
python tools/wifi_diag.py --port COM5 --source-ip 192.168.178.250/24 --target-ip 192.168.178.1 --count 12 --pcap w3.pcap --report w3.json
```

- Die Subnetzangabe `/24` bedeutet `255.255.255.0`; die tatsächliche Netzmaske verwenden.
- Die Gegenstelle kann ein Router oder zweiter Rechner sein. Firewall muss ICMP-Echo erlauben.
- Gastnetz/AP-Client-Isolation kann den LAN-Zugriff sperren, obwohl WLAN `LINK_UP` meldet.
- Kein DHCP, Routing oder TCP/IP-Stack auf dem Pico: Das Python-Programm erzeugt
  ARP/IPv4/ICMP und beantwortet ARP-Anfragen für die Test-IP.
- Gesendet wird mit der tatsächlichen Stations-MAC. Der Pico übernimmt Ethernet II
  ohne VLAN, maximal 1514 Bytes ohne FCS; kürzere TX-Frames werden auf 60 Bytes
  mit Nullen aufgefüllt. IP-MTU ist 1500.

Erwartet: `arp peer=...`, zwölf `echo ... verified=1` und abschließend
`"passed": true`, `"lost": 0`. Die vier Payload-Größen 0, 56, 512 und 1472 Bytes
prüfen Mindestlänge, kleine Frames und MTU-Grenze. Jede Echo-Antwort wird auf
MAC/IP-Adressen, Checksummen, Kennung, Sequenz und exakten Nutzinhalt geprüft.

`w3.pcap` enthält TX/RX am USB-Testclient und lässt sich mit Wireshark öffnen.
TX-Einträge bedeuten SDK-Annahme, nicht einen Funkmitschnitt. Für unabhängigen
Nachweis zusätzlich auf der LAN-Gegenstelle ARP/ICMP mitschneiden. Die RTT enthält
USB, Host-Polling und WLAN; sie ist **kein Amiga-Durchsatzbenchmark**.

## 4. Weitere Abnahme

1. Den Lauf mit `--count 1000` wiederholen; Bericht, PCAP und `info` aufbewahren.
2. Während eines Laufs AP ausschalten: Fehler/Timeout ist erwartet; keine
   endlose Blockade. Bei USB-Timeout ist der Ausgang der letzten TX-Anfrage
   unbekannt; nicht automatisch als neues Paket nachsenden.
3. AP einschalten, Testport schließen, Terminal öffnen und automatische
   WLAN-Wiederverbindung prüfen. Danach einen **neuen** Testlauf starten.
4. Terminal öffnen: `status`, `wifi status`, `wifi stats`; Bus unverändert gesperrt.
5. Programm abbrechen/Port schließen und erneut öffnen. Der Client zieht DTR
   beim Start für 150 ms auf Low; das beendet eine alte Rohdatensitzung, ohne
   den Pico oder das RAM-Profil zurückzusetzen.

Bei vollem RX-Ring verwirft die Firmware neue Pakete und erhöht `rx_full`.
Bereits gespeicherte Frames bleiben unverändert. `rx_inactive` zählt Frames
außerhalb einer aktiven Testsitzung, `rx_flushed` bei Link-/Sitzungswechsel
verworfene Warteschlangen. Auf einem belebten LAN sind diese Zähler nicht
zwangsläufig null. `tx_used` umfasst wartende, aktive und noch nicht abgeholte
TX-Abschlüsse. `tx_busy` meldet erschöpfte Slots. Der USB-Test arbeitet bewusst
mit nur einer offenen Anfrage; acht parallele TX-Aufträge und volle Ringe
werden zusätzlich in den nativen C-Tests geprüft.

USB-Prüfsummenfehler führen zum Verwerfen bis zum nächsten Null-Delimiter.
Der Client wiederholt dieselbe Anfrage mit gleicher Sequenz; die Firmware
liefert ihre gespeicherte Antwort erneut, ohne ein zweites Mal zu senden oder
RX zu entnehmen. Ein neuer Pico-/USB-Sitzungsschlüssel wird nicht stillschweigend
akzeptiert. [Protokolldetails](USB_PROTOCOL.md).

## Build und automatische Prüfungen

Gleiche Toolchain und SDK-Pins wie in `README.md`; Standard-CMake baut M1,
W1/W2 und W3 gemeinsam. Beispiel aus dem Repository:

```text
python tools/check_board_contract.py
python tests/run_host_tests.py
python -m unittest discover -s tests -p "test_*.py"
python tools/check_firmware_artifacts.py build/m1 --target funkotto_w3
python tools/package_firmware.py build/m1 .deps/pico-sdk build/m1/funkotto-w3.zip --target funkotto_w3
```

Die beiden Profilsektoren und der eigene RP2350-E10-Sektor bleiben getrennt;
W3 schreibt keine Profile. Ergebnisse im [W3-Prüfbericht](../docs/results/2026-10-09-w3.md).
