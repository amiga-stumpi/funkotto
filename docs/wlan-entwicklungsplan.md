# FunkOtto: Entwicklungsplan WLAN

Stand: 08.10.2026. Planung auf Grundlage von M1 (`ae67bce3b2dc`), Pico SDK 2.2.0
und dem unveränderten Nutzerboard. W1/W2 sind softwareseitig umgesetzt; Scan und erster WPA2-Join am Pico bestätigt.
Manuelles Wiederverbinden und automatische Erholung nach AP-Ausfall sind ebenfalls
am Pico bestätigt. Weitere Fehlerfälle und Dauerlauf bleiben offen.
[Implementierungsbericht](results/2026-10-08-wlan-w12.md) ·
[Testanleitung](../firmware/WLAN_TEST.md).

## Ziel und Ausgangspunkt

Der Pico soll WLAN-Netze finden, sich mit einem 2,4-GHz-WPA2-Personal-Netz verbinden,
Ethernet-Frames übertragen und später ein Profil dauerhaft speichern. Nach einem
Kaltstart oder Amiga-Hardware-Reset soll er damit automatisch erneut verbinden.
Die spätere IP-Konfiguration, DHCP, DNS und TCP/UDP übernimmt der Amiga-Stack.
Der Pico erhält für den normalen Betrieb keinen eigenen IP-Stack und arbeitet
mit einer Stations-MAC, die später auch der Amiga-Treiber verwendet.

Am 08.10.2026 hat der Nutzer USB-Diagnose und anschließend ausdrücklich den
Kaltstart ohne geöffnetes Terminal bestätigt. Beide Statusausgaben melden
`bus_locked=1`, `output_mask=0x00700000` und `watchdog_boot=0`.
[Nachweis und Grenzen](results/2026-10-08-firmware-m1.md).
Das bestätigt den Grundstart; Watchdog-Fehlerinjektion, elektrische Pegel und der
Resetpfad am Träger sind damit nicht gemessen.

Die WLAN-Stufen werden zunächst am einzelnen Pico über USB abgenommen. GP21/22
bleiben Low; GP0–11 und GP26 bleiben Eingänge. Die vorhandene Ausgangsmaske gilt
nur für die Adapter-GPIOs, nicht für die zusätzlich benötigten internen WLAN-Pins.
Die KiCad-Dateien bleiben unverändert. M0 sperrt weiterhin aktive Amiga-Bustests,
aber nicht isolierte WLAN-Arbeiten. M3 wird vor M2 begonnen; die USB-Teile von M4
können folgen, seine vollständige Amiga-Abnahme bleibt später erforderlich.

## Architekturentscheidung

Core 0 behält USB, Befehlsparser, sichere Adapter-GPIOs und die zentrale
Watchdog-Entscheidung. Core 1 besitzt WLAN-Zustandsautomat, CYW43-Treiber und
Ethernet-TX/RX. Aufträge, Ereignisse und Pakete werden über begrenzte Queues mit
klarer Pufferübergabe ausgetauscht. Keine CYW43-Aufrufe direkt aus der USB-Konsole;
keine USB-Ausgabe und kein Flash-Schreiben aus Empfangs-Callbacks.

Als Startarchitektur wird ein auf Core 1 initialisierter `async_context_poll`
mit CYW43 ohne lwIP gewählt. Die SDK-Arbeit wird dort regelmäßig ausgeführt;
zwischen Arbeiten wird auf Ereignis oder nächsten Termin gewartet. Dieses lokale
Pico-Polling erzeugt kein Amiga-Polling und legt dessen spätere IRQ-Strategie nicht fest.
Lange SDK-Aufrufe können Core 1 belegen: Initialisierung, Scan und Senden deshalb
messen; USB auf Core 0 darf dadurch nicht unbedienbar werden.

Besonderheiten des festgelegten SDK-Stands:

- `pico_cyw43_arch_none` benutzt einen Hintergrundkontext. Es ist kein fertiger
  Roh-Ethernet-Adapter: Standard-Callbacks für Link und Empfang können `panic`
  auslösen. Vor dem ersten Verbindungsversuch eigene Implementierungen für
  `cyw43_cb_tcpip_init/deinit`, `cyw43_cb_tcpip_set_link_up/down` und
  `cyw43_cb_process_ethernet` bereitstellen; Symbolauflösung im ELF prüfen.
- Das vorgefertigte Target `pico_cyw43_arch_poll` liegt in SDK 2.2.0 innerhalb der
  lwIP-Verfügbarkeitsbedingung. Für den geplanten Build ohne lwIP wird eine kleine
  projektlokale CMake-Verknüpfung aus `pico_cyw43_arch` und
  `pico_async_context_poll` mit `PICO_CYW43_ARCH_POLL=1`, `CYW43_LWIP=0` gebaut.
  Keine SDK-Quelldateien verändern. Dieser Build ist ein explizites W1-Prüftor.
- `LINK_UP` kommt aus dem authentifizierten STA-Link-Callback des Treibers.
  Ein bloß gestarteter Join oder ein TCP/IP-Status ohne IP-Adresse reicht nicht.
  Fehlerstatus und Linkverlust zusätzlich auswerten; Ereignisse einer alten
  Verbindung durch eine Sitzungskennung verwerfen.

Der Watchdog wird nur bei nachgewiesenem Fortschritt beider Kerne bedient.
Die reguläre Wiederverbindungswartezeit ist kein Stillstand: Core 1 arbeitet
weiter und liefert Lebenszeichen. Für WLAN-Initialisierung und Flash-Pausen
werden begrenzte, gemessene Sonderzustände vorgesehen. Kein dauerhaftes Füttern
allein aus einem Timer-IRQ. Die bestehende 8-s-Frist wird gegen die reale maximale
SDK-Blockierdauer geprüft, bevor sie für WLAN unverändert übernommen wird.

## Arbeitspakete und Abnahme

| Stufe | Umsetzung | Abnahme / Ergebnis |
| --- | --- | --- |
| W1 – Treiberbasis | CYW43-Abhängigkeit festlegen, Core 1, eigener SDK-Adapter und Callbacks, Initialisierung mit Länderkennung DE, MAC und Fehlerstatus | Neuer Build/CI bestehen; MAC gültig; USB ohne Terminal und bei Initialisierungsfehler bedienbar; Bus bleibt gesperrt |
| W2 – Scan und Verbindung | Asynchroner Scan, RAM-Profil, WPA2-Join, Status, Abbruch und Wiederverbindung | Reales Netz gefunden und authentifiziert; falsches Passwort/fehlender AP führen zu begrenzten Versuchen; AP aus/an erholt sich; USB bleibt bedienbar |
| W3 – Ethernet-Rohdaten | TX/RX-Adapter, feste Ringe, Längenprüfung, Zähler und USB-Testtransport | Rohframes in beide Richtungen mit LAN-Gegenstelle nachgewiesen; ARP/ICMP über den Testtransport; volle Puffer ohne Korruption |
| W4 – Dauerhaftes Profil | Zwei Flashsektoren, CRC/Sequenz/Commit, explizites Speichern/Löschen, automatischer Join nach Start | Kaltstart ohne USB-Konsole verbindet; unterbrochene Flashoperationen sind beherrscht; Profil bleibt beim normalen UF2-Update erhalten |
| W5 – Stabilisierung | Fehlertests, begrenzte Neustarts des WLAN-Treibers, Ressourcen-/Zeitmessungen, Dokumentation | 24-h-Test und definierte Ausfalltests bestanden; WLAN-Modul bereit für spätere M2/M5-Integration |

**Erste auszuliefernde Test-UF2: W1 + W2.** Damit kann der Nutzer scan/connect/status
auf seinem einzelnen Pico prüfen. Zugangsdaten liegen dabei zunächst nur im RAM.
Erst nach erfolgreichem Rohframe-Nachweis W3 wird die dauerhafte Speicherung W4
freigegeben. Jede Stufe liefert Quellen, UF2, Manifest, Testanleitung und Bericht.

### W1: reproduzierbare Basis und Ressourcen

Den zum SDK gehörenden CYW43-Commit
`dd7568229f3bf7a37737b9e1ef250c26efe75b23` in `dependencies.json` ergänzen;
Bootstrap/CI und Manifest um Treiber- und Funkfirmware-Prüfsummen sowie benötigte
Lizenzhinweise erweitern. Bluetooth und lwIP bleiben ausgeschaltet. Den
bestehenden M1-Build als Diagnose-/Regressionstarget erhalten.

Vor und nach WLAN-Init Adapterpins prüfen. Interne CYW43-Pins GP23/24/25/29 gemäß
Pico-2-W-Boarddefinition nutzen; GP27/28 nicht zweckentfremden. Belegte PIO-Blöcke,
State Machines, DMA-Kanäle, IRQs, Timer, Flash, SRAM und beide Core-Stacks erfassen.
SDK-Ressourcen dynamisch reservieren, keine vermeintlich freien Nummern für den
späteren Parallelport fest eintragen. Belegungsfehler als Fehler melden.

### W2: Konfiguration und Wiederverbindung

Geplante USB-Bedienung (noch keine vorhandenen M1-Befehle):

| Befehl | Wirkung |
| --- | --- |
| `wifi status` | Zustand, gültige MAC, Link, RSSI soweit verfügbar, Fehler, Versuche und RAM/gespeichert-Status |
| `wifi scan` | Begrenzten Scan starten; Ergebnisliste mit SSID, BSSID, Kanal, RSSI und Authentifizierung |
| `wifi set` | Geführte Eingabe von SSID und Passwort; ausschließlich RAM ändern |
| `wifi connect` / `wifi disconnect` | Join starten beziehungsweise beenden und automatische Wiederholung stoppen |
| `wifi stats` | TX/RX, Drops, Queue-Höchststände und Linkwechsel |
| `wifi save` / `wifi erase` | Erst ab W4: ausdrücklich speichern beziehungsweise dauerhaft löschen |

SSID intern als Länge plus maximal 32 Bytes führen, Passwort zunächst als
8–63 Zeichen WPA2-Passphrase. Geführte Eingabe unterstützt Leerzeichen; zusätzlich
SSID als Hex-Bytes vorsehen, damit nicht druckbare Namen darstellbar bleiben.
Scan-Ausgaben sicher escapen, nie fremde Steuerzeichen ins Terminal übernehmen.
Versteckte SSIDs können manuell konfiguriert werden; ein Scan muss sie nicht finden.

Der bisherige 48-Byte-M1-Zeilenpuffer reicht für diese Eingaben nicht. WLAN-Konsole
mit eigenem begrenztem Puffer (zunächst 256 Bytes) und Zustandsparser ergänzen.
Überlange/ungültige Eingaben komplett verwerfen; kein Ausführen abgeschnittener
Reste. Passwort nicht zurücksenden, nicht in Status/Fehler/Logs aufnehmen;
Terminal-Lokalecho beim Eingeben ausschalten. Temporäre Kopien nach Nutzung
löschen. Keine echten Zugangsdaten in GitHub, Tests oder UF2 einkompilieren.

Zustände: `INITIALIZING`, `UNCONFIGURED`, `DISCONNECTED`, `CONNECTING`, `LINK_UP`,
`RETRY_WAIT`, `ERROR`; Scan läuft als gesonderter begrenzter Auftrag. Zunächst
höchstens 32 Scanergebnisse und 10 s Scanfrist, bei Überlauf sichtbarer Hinweis.
Scan während Join oder später unter Paketlast darf mit `BUSY` abgewiesen werden.

Join zunächst mit 15 s Frist; Wiederholung nach 1/2/4/8/16/30 s, danach maximal
30 s Abstand. Neue Konfiguration/Disconnect bricht alte Aufträge logisch ab.
`disconnect` unterdrückt automatische Versuche bis `connect` oder Neustart.
Fehlercode `BADAUTH` nur melden, wenn der Treiber ihn liefert; generischer Timeout
ist kein sicherer Beleg für ein falsches Passwort. Start mit gespeichertem Profil
verbindet automatisch, ohne Profil bleibt er bedienbar in `UNCONFIGURED`.
Energiesparen zunächst abschalten; Latenz und Verbrauch später gesondert bewerten.

### W3: Rohframes vor Amiga-Integration nachweisen

Stand 09.10.2026: als `funkotto_w3` implementiert, einschließlich USB-Testmodus,
PC-ARP/ICMP-Werkzeug und automatisierter Queue-/Protokolltests. **Reale ARP/ICMP-Tests
bestanden (12/12 und 1.000/1.000, MTU 1500, keine Verluste; JSON/PCAP geprüft)**.
AP-Ausfall mit Wiederverbindung und erneutem Pakettest ist vom Nutzer bestätigt.
Damit kann W4 als nächster Entwicklungsschritt beginnen. Langzeitbetrieb und
die Ursache einzelner Latenzausreißer bleiben offen: [Testablauf](../firmware/W3_TEST.md),
[Prüfstand](results/2026-10-09-w3.md). W4 beginnt nach dem realen Rohframe-Nachweis.

TX über `cyw43_send_ethernet(..., is_pbuf=false)`, RX über den eigenen Callback.
Die unteren Treiberfunktionen hinsichtlich Kopieren/Übernahmedauer prüfen:
TX-Puffer erst nach bestätigter Übernahme freigeben; RX-Daten vor Callback-Ende
in eigenen Speicher kopieren. Callback führt keine Warteoperation und keinen
rekursiven Sendebefehl aus. STA-Eingang und Paketlänge vor dem Kopieren prüfen.

Anfangs je acht 1600-Byte-Puffer für TX/RX: 25.600 Byte Nutzspeicher plus Metadaten.
Ungetaggte Frames bis 1514 Bytes ohne FCS, MTU 1500. Ethernet-Mindestlänge/Padding
mit dem konkreten Treiber nachweisen. TX voll ergibt `BUSY`; RX voll verwirft
gezählt. Keine ungelesenen Pakete überschreiben. Bei Linkwechsel alte Aufträge
und Pakete definiert beenden; erfolgreiches SDK-Senden bedeutet keine bestätigte
Zustellung bei der Gegenstelle. Broadcast für ARP/DHCP einplanen und testen.

Ein PC-Testwerkzeug nutzt eine explizit aktivierte gerahmte USB-Diagnosesitzung
mit Typ, Länge, Sequenz und CRC. In diesem Modus keine ungerahmten Textausgaben
zwischen Paketen; Binärdaten nicht durch den Kommandozeilenparser leiten.
Der PC erzeugt ARP/ICMP-Testframes mit Stations-MAC und freier Test-IP; der Pico
transportiert sie unverändert. Eine zweite LAN-Gegenstelle beantwortet/mitschneidet
sie. AP-Client-Isolation ausschließen oder als Umgebungsfehler dokumentieren.
Beide Richtungen durch Sequenzen/Nutzdatenvergleich und Mitschnitt belegen.
Dieser USB-Test ist kein SANA-II-Ersatz und kein Amiga-Durchsatzbenchmark.

### W4: Speichern und Autoconnect

Bestehendes Layout behalten: Profil A bei Flashoffset `0x003FE000`, Profil B
bei `0x003FF000`, je 4096 Byte; Firmware vor XIP `0x103FD000` begrenzen und den
separaten E10-Sektor schützen. UF2-/Linkerprüfungen auf die WLAN-Ziele erweitern.

Datensatz mit Schema, Längen, Authentifizierung, Land, SSID/Passphrase, Sequenz,
CRC32 und zuletzt geschriebenem Commit-Marker. Inaktiven Sektor verwenden;
vor Erfolg zurücklesen und prüfen. Bei Start neuesten gültigen Stand wählen,
auch bei Sequenzüberlauf; unbekanntes Schema als inkompatibel melden. Beschädigte
oder leere Profile erzeugen `UNCONFIGURED`, keine Bootschleife.

Speichern ausschließlich nach `wifi save`, nicht bei jedem Reconnect. Vor
Flashoperation beide Kerne und alle XIP-lesenden DMA-Zugriffe koordiniert
anhalten; SDK-Flash-Sicherheitsmechanismen passend zur Multicore-Architektur
initialisieren und prüfen. WLAN darf dafür sichtbar getrennt und danach wieder
verbunden werden. Keine Locks halten, die den angehaltenen Kern benötigen.
Fehlende Ruhebestätigung beendet den Auftrag ohne Flashänderung.

Löschen zuerst durch jüngeren gültigen Löschdatensatz absichern, dann alte
Geheimnisse entfernen. Nach Unterbrechung darf das ältere Profil nicht erneut
aktiv werden; Bereinigung beim Start fortsetzen. Erfolg erst nach Bereinigung.
Profile sind unverschlüsselt im Flash; Schutz vor Verlust ist keine Verschlüsselung.

## Prüfprogramm und Fertigkriterien

Automatisiert: Zustandsübergänge mit simuliertem Treiber und Zeit, Scan-/Parsergrenzen,
Queue-Überlauf, stale Ereignisse, Heartbeat-Ausfall, CRC, Sequenzüberlauf sowie
Stromausfallmodell an jeder Schreib-/Löschphase. Buildprüfungen: echte Callback-
Symbole, kein lwIP/UART, reservierte Flashbereiche und unveränderte Pinzuordnung.

Am Pico: korrektes/falsches Passwort, kein AP, AP-Neustart, Funkverlust,
Disconnect/Connect, USB ab/an, Betrieb ohne Terminal, Scanabbruch, volle Ringe,
Linkwechsel unter Last und absichtlicher Core-1-Stillstand für Watchdog-Nachweis.
WLAN-Initzeit, Joinzeit, Reconnectzeit und USB-Antwortlatenzen getrennt messen;
`safe_init_us` bleibt ausschließlich ein interner Timerstand.

W4-Abnahme: 100 Kaltstarts mit gültigem Profil, zusätzliche gezielte
Stromunterbrechungen in den Flashphasen und normaler BOOTSEL-Update-Test mit
anschließendem Profilvergleich. Elektrische RUN-/Amiga-Resettests erst am
abgenommenen Träger nachholen. W5: 24 Stunden bidirektionaler Pakettest,
Zähler/Paketverluste auswerten, keine Datenkorruption und keine unerklärten Resets.
Realtests werden durch Nutzerprotokolle bestätigt, nicht aus CI-Erfolg abgeleitet.

Nach W5 sind WLAN und USB-Konfiguration separat nutzbar. Zur vollständigen
FunkOtto-v0.1 fehlen weiterhin M0, Amiga-Link, dessen Konfigurationsbefehle und
Integration; SANA-II und OS-1.3-Oberfläche bleiben eigene Folgepakete.

## Geplante Dateien und überprüfte Quellen

Module: `firmware/src/wifi.c`, `wifi_sdk.c`, `ethernet.c`, `config.c` und zugehörige
Header; Tests mit Treiber-/Flash-Mocks; `tools/wifi_diag.py` für die PC-Abnahme.
WLAN-Ziele und Manifest neben M1; Schnittstellen so anlegen, dass USB und später
Amiga dieselben Konfigurations-/Paketoperationen aufrufen.

Quellen geprüft am 08.10.2026; Architekturentscheidungen oben sind Projektplanung:

- [SDK-Netzwerk-API](https://www.raspberrypi.com/documentation/pico-sdk/networking.html).
- [SDK 2.2.0 CMake-Targets](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2_common/pico_cyw43_arch/CMakeLists.txt).
- [SDK-Callbacks ohne lwIP](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/rp2_common/pico_cyw43_driver/cyw43_driver.c).
- [Festgelegter CYW43-Stand: Join, Link-Ereignisse und Ethernet-TX](https://github.com/georgerobotics/cyw43-driver/blob/dd7568229f3bf7a37737b9e1ef250c26efe75b23/src/cyw43_ctrl.c).
- [Pico-2-W-Pinbelegung des SDK](https://github.com/raspberrypi/pico-sdk/blob/a1438dff1d38bd9c65dbd693f0e5db4b9ae91779/src/boards/include/boards/pico2_w.h).
