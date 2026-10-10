# FunkOtto: Plan für die erste funktionale Firmware v0.1

Stand: 10.10.2026 · Status: M1 und WLAN W1–W4 umgesetzt, Grundfunktionen am Pico bestätigt; M2 softwareseitig umgesetzt, W5 ausgelassen, M0 offen

## 1. Ziel und Entscheidung

FunkOtto verbindet den Amiga-Parallelport über einen Raspberry Pi Pico 2 W mit
WLAN. Die erste funktionale Firmware soll vom Amiga erkannt und konfiguriert
werden, Zugangsdaten stromausfallsicher speichern, sich nach dem Einschalten
selbstständig verbinden und Ethernet-Frames in beide Richtungen übertragen.

**Firmwareinstallation und Firmwareupdates erfolgen vorerst ausschließlich über
USB/BOOTSEL am Pico.** Kein Amiga-Flasher, kein SWD über DB25, kein OTA und keine
A/B-Firmwarepartitionen in v0.1. Zwei kleine Konfigurationssektoren dienen nur dem
WLAN-Profil; sie sind keine Firmware-Update-Slots.

Die Firmware wird als Ethernet-Adapter entwickelt: Der spätere Amiga-TCP/IP-Stack
übernimmt IP-Adresse, DHCP, DNS und TCP/UDP. Der Pico arbeitet als WLAN-Station mit
einer MAC-Adresse. Keine NAT-Funktion, kein WLAN-Modem mit AT-Kommandos und kein
Mehr-MAC-Bridging. Zunächst WPA2-Personal im 2,4-GHz-Netz und ein gespeichertes
Profil. Weitere Authentifizierungsarten erst nach gesonderter Prüfung.

Der SANA-II-Treiber und die Intuition-Oberfläche bleiben eigenständige Folgepakete.
Für v0.1 wird jedoch ein kleines 68000-/OS-1.3-kompatibles Diagnoseprogramm
benötigt, das reale Amiga-Kommunikation, Konfiguration und Rohpakete testet.
Ein erfolgreicher PC-Test allein erfüllt das Ziel nicht.

## 2. Definition: Wann ist v0.1 funktional?

- Der Amiga liest Identität, Firmware-/Protokollversion und Status zuverlässig.
- Ein ungeflashter Pico erhält eine reproduzierbar gebaute UF2 über USB.
- SSID und Passwort können über den Amiga gesetzt, geprüft, gespeichert und gelöscht werden.
- Nach vollständiger Stromunterbrechung verbindet sich der Pico ohne Konfigurationstool erneut.
- Bei unerreichbarem Access Point bleibt der Parallelport bedienbar; Verbindungsversuche werden wiederholt.
- Ethernet-Frames kommen vom Amiga zum WLAN und zurück. ARP und ein ICMP-Echo-Test mit dem Diagnoseprogramm sind nachgewiesen.
- Datenfehler, volle Puffer, Timeout, Hostneustart und WLAN-Ausfall erzeugen definierte Zustände statt Buskollisionen oder Hänger.
- Firmware, Buildanleitung, Protokoll, GPIO-Zuordnung und tatsächliche Messberichte sind versioniert.

Der Start unter 100 ms bis zur Parallelport-Bereitschaft ist ein **zu messendes
Optimierungsziel**, keine bestätigte Eigenschaft. WLAN-Verbindungszeit wird
separat gemessen. Höherer Durchsatz als eine Plipbox ist ein späteres Vergleichsziel.

## 3. Verbindliche Hardwarebasis: Nutzerstand vom 08.10.2026

Basis ist das vom Nutzer bearbeitete `AmiWiFi_Pico2W.zip`, unverändert importiert
mit Commit `d47ea64065e99507edfb3ae9dc4aa1727e00eb0b`. Es ersetzt den bisherigen
Rev-B2-Stand. [Importnachweis](results/2026-10-08-nutzer-kicad-import.md).
M1 prüft die folgende GPIO-Zuordnung und die Reset-/Freigabenetze direkt gegen
das aktuelle PCB. Dieser Netzabgleich ersetzt weder DRC/ERC noch Messungen.
Die Firmwarearbeit verändert keine KiCad-Dateien.

| Funktion | Pico-GPIO | Physische Modul-Pins |
| --- | --- | --- |
| Parallel D0–D7 | GP0–GP7 | 1, 2, 4, 5, 6, 7, 9, 10 |
| STROBE_N, Eingang | GP8 | 11 |
| SEL, Eingang | GP9 | 12 |
| BUSY, Ausgang | GP10 | 14 |
| POUT, Ausgang | GP11 | 15 |
| DATA_DIR | GP20 | 26 |
| FW_DATA_EN | GP21 | 27 |
| FW_CTRL_EN | GP22 | 29 |
| ACK_N, Ausgang | GP26 | 31 |
| Hardware-Reset RUN, aktiv Low | kein GPIO | 30 |

`DATA_DIR=1` bedeutet Amiga→Pico; `DATA_DIR=0` Pico→Amiga.
Der Datenbus darf erst nach einer bestätigten Richtungsübergabe aktiv werden.
Die Pico-Pins 16–25 sind im Trägersockel nicht vorhanden.

### Gewählte Resetvariante

**Jeder am Adapter anliegende Hardware-Amiga-Reset setzt auch den Pico zurück.**
U5/U6 sperren weiterhin die Bustreiber. Zusätzlich verknüpft U8, ein
SN74LVC2G07DCKR (SC70-6/DCK) mit zwei nichtinvertierenden Open-Drain-Ausgängen, `RESET_IN`
und `HOST_PRESENT`. Seine Ausgänge liegen gemeinsam an `PICO_RUN_N` und
Pico-Modulpin 30 (`RUN`). Ein Low an einem Eingang zieht RUN auf Low.
Nur bei beiden High gibt U8 RUN frei; der interne Pico-Pullup zieht auf 3,3 V.
Keine direkte 5-V-Verbindung an RUN; kein zusätzlicher Pico-GPIO erforderlich.

U8-Pins: 1 RESET_IN, 2 GND, 3 HOST_PRESENT, 4 PICO_RUN_N, 5 +3V3_IF,
6 PICO_RUN_N. C11: 100 nF an VCC/GND; R36: 100 kOhm RESET_IN/GND;
TP7: RUN-Messpunkt. Bestehende FW_ENABLE-Pulldowns R28/R29 bleiben 4,7 kOhm.
Keinen Push-Pull-Puffer als Ersatz für U8 einsetzen.

Nach Reset startet die Firmware vollständig neu: sichere GPIOs, leere
PIO-/DMA-Zustände und Queues, neue Protokollsitzung, gespeichertes WLAN-Profil
lesen, WLAN neu initialisieren und verbinden. **Die WLAN-Verbindung bleibt bei
einem Hardware-Amiga-Reset nicht bestehen.** Profile im Flash bleiben erhalten;
ein unterbrochener Schreibvorgang muss auf den letzten gültigen Stand zurückfallen.
Ein Software-Neustart ohne elektrischen Resetimpuls erfordert weiterhin die
Protokoll-Resynchronisation. `LINK_RESET` kann eine gesunde WLAN-Verbindung erhalten.

### USB/BOOTSEL mit eingebautem Pico

**DB25 und USB-C-Versorgung des Trägers trennen**, dann BOOTSEL halten und den
Pico über seinen eigenen USB-Anschluss verbinden. D1 isoliert VSYS von der
Trägerversorgung, U8 bleibt unversorgt und seine Ioff-Ausgänge hochohmig.
Alternativ den Pico aus dem Sockel nehmen. Ein versorgter Träger ohne Amiga
hält RUN absichtlich Low und verhindert diesen USB-Servicebetrieb.
Die USB-C-Buchse des Trägers liefert nur Strom, keine Firmware-Datenverbindung.
Diese Versorgungsfolge und fehlende Rückspeisung sind am Muster zu verifizieren.

### M0 bleibt die elektrische Abnahme

Die Resetarchitektur ist festgelegt und in KiCad umgesetzt. Das ist noch kein
Messnachweis. Vor dem ersten aktiven Bustest:

1. RESET_IN, HOST_PRESENT, RUN/TP7 und beide OE_N-Signale aufzeichnen;
   Pegel und RUN-Low-Dauer gegen die Datenblattanforderungen prüfen.
2. Amiga-Reset während Pico→Amiga und Amiga→Pico testen; kein Wiederanlauf
   alter Busausgänge nach Resetfreigabe. FW_ENABLEs bleiben beim Boot aus.
3. Steuerpfad erst mit vorbereiteten Idle-Pegeln aktivieren; neue HELLO-Sitzung
   und bestätigte Richtungsübergabe vor Datenausgabe verlangen.
4. Beide Einschaltreihenfolgen, Host aus/an, Trägerversorgung und Pico-USB sowie
   Reset während Flash-Schreiben/Löschen prüfen.

Bis zur Abnahme bleiben aktive Datenbustests gesperrt. Softwarebau, Parser-,
Speicher- und isolierte WLAN-Tests können bereits laufen.

**Historischer Nachweis, nicht für das aktuelle Nutzerboard:**
Digitale Prüfung von B2 mit KiCad 10.0.6: **0 DRC-Fehler/0 Warnungen, 0 offene
Verbindungen, 0 Schaltplanabweichungen; ERC 0 Fehler/0 Warnungen.** DRC ausdrücklich
mit `--schematic-parity --all-track-errors`. Exakte Netznamen inklusive `/` und
NC-Netzen abgeglichen; kein Entfernen von Namenspräfixen im Vergleich.

Korrektur gegenüber dem zuvor gelieferten Rev-B-Paket: Der alte CLI-Lauf ließ
den nativen Schaltplanvergleich aus. Daher blieben Netznamen- und Attributkonflikte
unentdeckt. B1 synchronisiert diese Daten und die projektlokalen Footprints,
kennzeichnet die realen Versorgungseinspeisungen mit PWR_FLAG und verwendet
für U4 ein Symbol für die fest verdrahtete Richtung B→A. Das U7-Symbol bildet
ausschließlich die 30 tatsächlich bestückten Sockelkontakte ab. Bestückungsdruck
und Bibliothekskopien sind bereinigt, ohne neue Prüfausnahmen anzulegen.
Diese B1-Korrektur ließ Padgeometrien, Platzierung und Kupfer unverändert.
B2 ändert ausschließlich das U8-Gehäuse und sein lokales Routing; die Resetfunktion
bleibt gleich. [B2-Prüfbericht](results/2026-10-07-revb2.md).
Keine Fertigungsfreigabe; elektrische Musterprüfungen bleiben offen.

Aktuelle Hardware-Referenzen, SHA-256:

- Schaltplan: `7c79beedb5473c872b8ad4855c7a03fb04484a50e73ab5bca5bac22929186a50`
- PCB: `b2f32df78c15e37ceb1bc0433f0356848106e7a86a1e82df63c3da5a7e26928c`
- Frühere Rev-B2-PCB-Prüfsumme: `a7a9cbb19c9f8817d6666fb08a87a784668ab8e951d3fde8aa5edc5f07c89981`

## 4. Architektur und Build

C/C++ mit Raspberry Pi Pico SDK, Ziel `PICO_BOARD=pico2_w`, Arm-Build für RP2350.
M1 legt SDK 2.2.0, TinyUSB, picotool und Buildwerkzeuge in
[`firmware/dependencies.json`](../firmware/dependencies.json) fest. Kein gleitendes `master` als reproduzierbare Buildabhängigkeit.
CI liefert UF2, ELF, MAP und ein Manifest mit Versions- und Prüfsummenangaben.
Keine WLAN-Zugangsdaten im Quelltext, Repository, Buildartefakt oder Testlog.

Geplante Aufteilung:

| Modul | Aufgabe |
| --- | --- |
| `board` | GPIOs, sichere Startpegel, Freigaben, Hardwarekennung |
| `parallel` | PIO, DMA, Byte-/Blocktransfer, Richtungswechsel und Timeout |
| `protocol` | Parser, CRC, Sitzungen, Kommandos, Antworten |
| `wifi` | CYW43-Initialisierung, Scan, Join, Wiederverbindung und Status |
| `ethernet` | Rohframes, MAC, TX/RX-Ringe, Filter und Zähler |
| `config` | RAM-Profil, zwei Flashsektoren, Speichern/Löschen |
| `diagnostics` | Laufzeit-/Fehlerzähler, Watchdog, optionale USB-Ausgaben |

Core 0 besitzt Parallelport, Protokoll und Pufferübergaben. Core 1 besitzt WLAN
und alle CYW43-Aufrufe. Die ersten Bring-up-Schritte dürfen noch einkernig laufen;
vor der Integration wird die Zuständigkeit verbindlich umgesetzt. Zwischen den
Kernen liegen begrenzte Queues und eindeutig übergebene Puffer. `volatile`
ersetzt keine Synchronisierung. Interrupts erledigen nur kurze Zustandsübergaben.
Flash-Schreiben erfordert einen koordinierten Stillstand beider Kerne und DMA.

PIO-State-Machines, DMA-Kanäle, IRQs und SRAM-Budget in einer Ressourcentabelle
festhalten. WLAN verwendet eigene SDK-Ressourcen; keine festen PIO-/DMA-Nummern
annehmen, die mit CYW43 kollidieren. Zunächst je acht feste 1600-Byte-TX-/RX-Puffer
als Budgetansatz; tatsächlichen Gesamtverbrauch einschließlich WLAN messen.

### Startfolge

1. Daten-GPIOs Eingang, beide FW_ENABLEs aus, DATA_DIR auf Amiga→Pico.
2. Steuer-Ausgangswerte vorbereiten, insbesondere ACK_N inaktiv; PIO/FIFOs leeren.
3. Timer, Protokoll, Linkzustand und sichere Freigabesequenz initialisieren.
4. Steuerpfad mit sicheren Idle-Pegeln freigeben und Parallelport-Status
   bereitstellen; Daten-Ausgabe erst nach neuer Sitzung/Richtungsübergabe.
   Kein Warten auf USB-Terminal oder WLAN.
5. Profil prüfen und WLAN auf seinem eigenen Ausführungspfad initialisieren.
6. Bei gültigem Profil automatisch verbinden, sonst `UNCONFIGURED` melden.

`GET_INFO` funktioniert vor WLAN-Bereitschaft. Noch unbekannte Stations-MAC mit
Gültigkeitsbit ausweisen und später erneut abfragbar machen; kein erfundener
MAC-Wert. Frühbereitschaft darf nicht durch einen blockierenden WLAN-Init auf
Core 0 vorgetäuscht werden. Der Watchdog überwacht den Fortschritt beider Kerne,
nicht nur einen regelmäßig laufenden Timer.

## 5. Parallelport-Protokoll v1

Die folgenden Angaben bleiben der Integrationsrahmen. M2 implementiert das
[Diagnoseprofil B1536](../protocol/M2.md) mit konkreter Host-Registerfolge,
PIO/DMA und Fehlerrückkehr. Reale Oszillogramme und Timingabnahme fehlen noch.

- Halbduplex, Amiga als Transaktionsmaster. Pico sendet Daten ausschließlich
  innerhalb eines ausdrücklich angeforderten Lesevorgangs.
- SEL steuert die Linkphase, STROBE markiert Portzugriffe. BUSY ist Handshake/
  Rückdruck; POUT zeigt wartende Daten oder Antworten. ACK_N ist zunächst
  optional und erst nach erfolgreichem Polling-Betrieb als Paketereignis aktiv.
- Zuerst langsamer Byte-Handshake für den Hardwaretest, danach begrenzte Blöcke
  mit einmaligem Block-Handshake. Keine garantierte Spitzenrate in v0.1.
- Datenrichtungen ausschließlich mit deaktiviertem Translator und abgestimmter
  CIA-Datenrichtung wechseln. Erstes Lesebyte vorladen; notwendige Dummy-Zyklen
  ausdrücklich definieren und prüfen. Automatische CIA-STROBE-Impulse nicht
  mit einer frei programmierbaren GPIO-Taktquelle verwechseln.
- Anfangs maximal ein offener Auftrag; große Pakete dürfen in bestätigte
  Teilblöcke zerlegt werden. Byteabstand und Timeout sind Mess-/Protokollwerte,
  keine von der Amiga-CPU-Geschwindigkeit abhängigen NOP-Schleifen.

Rahmenvorschlag: 8-Byte-Header mit Magic `0x4157`, Version, Opcode, Payload-Länge
und Sequenznummer, danach Payload und CRC16/CCITT-FALSE über Header+Payload.
Mehrbytewerte big endian. Explizit byteweise kodieren, keine C-Strukturen direkt
übertragen. Bei Protokollfreigabe alle Feldbreiten, Statuswerte und CRC-Testvektoren
festschreiben. Bisherige Magic bleibt zur Kontinuität des Hardwareplans erhalten.

Kontrollpayload maximal 1518 Bytes; Ethernet zunächst ungetaggt, MTU 1500,
maximal 1514 Bytes einschließlich Ethernet-Header, ohne FCS. VLAN wird nicht als
unterstützt gemeldet. Mindestlänge und Padding beim CYW43-Senden empirisch prüfen.
Parser prüft Länge vor jedem Kopieren, Versionsverträglichkeit und CRC vor jeder
Zustandsänderung. Unbekannte Kommandos erzeugen `UNSUPPORTED`.

| Kommandogruppe | Geplante Kommandos |
| --- | --- |
| Erkennung | `HELLO`, `GET_INFO`, `GET_STATUS`, `GET_STATS` |
| Diagnose | `ECHO`, `LINK_RESET` |
| WLAN | `WIFI_SCAN`, `SCAN_NEXT`, `CONNECT`, `DISCONNECT` |
| Profil | `SET_PROFILE`, `SAVE_PROFILE`, `ERASE_PROFILE` |
| Daten | `TX_FRAME`, `RX_FRAME` |

`HELLO` etabliert eine neue Sitzung mit Epochkennung. Alte Antworten, Sequenzen
und DMA-Reste dürfen nach Reset keine neue Sitzung beeinflussen. Wiederholung
desselben gültigen Auftrags liefert das gespeicherte Ergebnis statt erneutem
Senden eines Ethernet-Pakets. Genau-einmal-Zustellung über einen Pico-Stromausfall
wird nicht behauptet; bei unklarer TX-Zustellung Status entsprechend melden.

Timeout, abgebrochener Rahmen und verlorene Synchronisation führen zu BUSY,
gesperrtem Datenbus, geleerten Teilrahmen und neuer Synchronisation. Fehler beim
Parallelport sollen eine gesunde WLAN-Verbindung möglichst nicht neu starten.
Davon ausgenommen ist der elektrische Amiga-Reset: Er setzt über RUN den
gesamten Pico zurück und erfordert einen neuen WLAN-Verbindungsaufbau.

## 6. WLAN und Roh-Ethernet

V0.1 benötigt echte Ethernet-Rohframes. Die „RAW API“ von lwIP ist nicht dieselbe
Schnittstelle. Auch `pico_cyw43_arch_none` allein darf nicht ungeprüft als fertiger
Ethernet-Adapter behandelt werden. M3 enthält deshalb einen frühen Integrations-
versuch mit der festgelegten SDK-Version: TX über `cyw43_send_ethernet` und ein
eigener RX-Einstieg, etwa `cyw43_cb_process_ethernet`, mit überprüftem Callback-
Kontext, Puffereigentum und Linkzustandsbehandlung. Standard-Stub-Callbacks dürfen
nicht unbemerkt aktiv bleiben. SDK-Anpassungen in einem kleinen Adapter kapseln.

Pico-IP-Stack, eigener DHCP-Client oder ein lokaler Ping vom Pico sind kein Ersatz
für diesen Rohframe-Nachweis. Linkbereitschaft meint WLAN-Assoziation und
Authentifizierung, nicht eine vom Pico bezogene IP-Adresse. Die WLAN-Stations-MAC
wird später dem Amiga als Netzwerkkartenadresse mitgeteilt.

Nichtblockierender Zustandsautomat:
`UNCONFIGURED → INITIALIZING → CONNECTING → LINK_UP` sowie `RETRY_WAIT` und `ERROR`.
Ein Verbindungsversuch erhält zunächst 15 s als anpassbaren Grenzwert. Danach
Wiederholung mit 1/2/4/8/16/30 s Abstand, maximal 30 s. Manuelles `DISCONNECT`
verhindert automatische Neuversuche bis `CONNECT` oder Neustart. Falsches Passwort
wird als solches gemeldet, sofern der Treiber es unterscheidet; keine schnelle
Endlosschleife. Länderkennung für Deutschland korrekt konfigurieren.

Scan wird auf ausdrücklichen Auftrag gestartet, mit begrenzter Ergebniszahl und
Timeout. Scan unter Last darf für v0.1 mit `BUSY` abgewiesen werden. SSIDs sind
Bytefolgen bis 32 Bytes; keine ungeprüften C-Strings. Zunächst WPA2-Passphrasen mit
8–63 Zeichen; 64-stellige Hex-Schlüssel sind ohne separate Implementierung nicht
unterstützt. Das Statuskommando liefert niemals das Passwort.

Empfangene Frames werden vor Ende des Callbacks in eigene Puffer kopiert.
TX-Puffer erst nach der bestätigten Übernahme gemäß SDK-Vertrag freigeben.
Bei vollen Ringen: eindeutiger Rückdruck für Host-TX, gezählter Drop für WLAN-RX;
nie ungelesene Frames still überschreiben. Broadcast muss für ARP/DHCP funktionieren.
Multicast und optionale Filter erst nach eigenem Test als Fähigkeiten melden.

## 7. Dauerhafte WLAN-Konfiguration

Ein Profil enthält Schema-Version, SSID-Länge/-Bytes, Authentifizierung,
Passwort-Länge/-Bytes, Länderkennung, Sequenznummer und CRC32. Zwei getrennte
Flashsektoren mit jeweils 4096 Bytes am Flashende reservieren; Kapazität aus dem
Board-/SDK-Ziel übernehmen und im Build prüfen. Linkerskript und UF2 dürfen diese
8192 Bytes nicht als Firmware belegen. CI prüft die Adressbereiche.

In M1 bereits reserviert: Profile ab XIP `0x103FE000`; zusätzlich der davorliegende
4-KiB-Sektor ab `0x103FD000` ausschließlich für den RP2350-E10-UF2-Block bei
`0x103FDF00`. Der normale Firmwarebereich endet davor. Damit liegt auch dieser
SDK-Kompatibilitätsblock außerhalb der Profile. Der UF2-Prüfer kontrolliert
beide Blocktypen. Ein realer Profilerhalt über BOOTSEL-Updates ist in M4 zu testen.

`SET_PROFILE` ändert nur die RAM-Konfiguration, `CONNECT` testet sie.
`SAVE_PROFILE` speichert nach ausdrücklicher Benutzeraktion. Ablauf:

1. Neue Hostaufträge pausieren, Link/DMA beruhigen, Flash-Operation ankündigen.
2. Inaktiven Sektor löschen und Datensatz mit höherer Sequenz schreiben.
3. Inhalt zurücklesen und CRC prüfen; Commit-Marker in separater Flashseite zuletzt schreiben.
4. Erst danach Erfolg melden; bisheriges gültiges Profil bis dahin erhalten.
5. Betrieb und Hostkommunikation wieder freigeben.

Die RAM-Ausführung, Interrupts, zweiter Kern und sämtliche Flash-lesenden DMA-
Zugriffe müssen den Anforderungen von `flash_safe_execute` entsprechen.
Eine Funktionsmarkierung „läuft aus RAM“ allein genügt nicht. WLAN darf beim
seltenen Speichern kurz pausieren oder neu verbinden; das muss sichtbar sein.

Beim Start den neuesten vollständig gültigen Datensatz verwenden. Sind beide
ungültig, `UNCONFIGURED` statt Bootschleife. Sequenzüberlauf berücksichtigen.
`ERASE_PROFILE` schreibt zunächst einen gültigen, jüngeren Löschdatensatz,
bevor alte Zugangsdaten entfernt werden. Nach Stromausfall darf nicht versehentlich
das ältere Profil reaktiviert werden. Erfolg erst nach vollständigem Löschen melden;
unvollständige Bereinigung beim nächsten Start beenden. Keine automatische
Flash-Schreiboperation bei jedem Reconnect.

Zugangsdaten sind gegen versehentlichen Verlust abgesichert, nicht gegen physischen
Flash-Zugriff verschlüsselt. Normale USB-Updates sollen das Profil erhalten, wenn
Image und Speicherlayout es auslassen. Ein vollständiges Flash-Löschen entfernt es.
Dieses Verhalten mit dem konkreten Updateverfahren testen und dokumentieren.

## 8. Arbeitspakete, Reihenfolge und Abnahme

| Schritt | Ergebnis | Abnahme |
| --- | --- | --- |
| M0 Hardware-/Protokollreview | Reset-/Hostverhalten und sichere Freigaben geklärt | Rev-B-RUN-Pfad und OE-Gating gemessen; kein Wiederanlauf mit altem Datenbus |
| M1 Build und Grundstart | SDK-Pin, CI, UF2, sichere GPIOs, Versionsausgabe | Build aus frischem Checkout; Start ohne USB-Terminal; Datenbus bleibt gesperrt |
| M2 Amiga-Link | PIO/DMA, Polling-Diagnosetool, HELLO/ECHO, Fehlerfälle | Nach M0 reale 68000-/OS-1.3-Transfers in beide Richtungen, kein Buskonflikt |
| M3 WLAN-Rohdaten | Scan, Join, Stations-MAC, Roh-TX/RX | Frame-Nachweis an einem zweiten LAN-Rechner; kein Pico-IP-Stack als Ersatz |
| M4 Konfiguration | Amiga-Kommandos, Flashprofil, Autoconnect | 100 Kaltstarts; gezielte Stromunterbrechung und RUN-Reset in jeder Schreib-/Löschphase |
| M5 Integration | Ethernet-Ringe, Rückdruck, Status und Recovery | ARP/ICMP vom Amiga-Diagnosetool über FunkOtto; AP-Ausfall und Hostreset überstanden |
| M6 Freigabe v0.1 | Releasekandidat, Installationsanleitung, Messbericht | 24-h-Dauerlauf ohne Datenkorruption/Buskollision; alle Einschränkungen dokumentiert |

**Fortschritt am 08.10.2026:** M1-Quellen, USB-Konsole, GPIO-Sperre, Watchdog,
Buildworkflow und automatisierte Prüfungen sind vorhanden. Zwei lokale Builds
aus getrennten Quellverzeichnissen liefern bytegleiche UF2/BIN-Dateien.
GPIO-/Parser-Hosttests und Artefaktprüfungen bestehen. Der Nutzer hat USB-Status
und Kaltstart ohne geöffnetes Terminal am realen Pico bestätigt. Watchdog-
Fehlerinjektion und elektrische Musterabnahme bleiben offen.
Die vorgezogene isolierte M3-Entwicklung beschreibt der
[WLAN-Entwicklungsplan](wlan-entwicklungsplan.md).
**Fortschritt am 09.10.2026:** Die isolierte WLAN-Entwicklung W1–W4 ist
umgesetzt. Rohframe-Transport (1.000/1.000 Pings), Wiederverbindung sowie
Speichern, Kaltstart-Autoconnect, Löschen und erneutes Speichern sind am Pico
bestätigt. [W3-Prüfstand](results/2026-10-09-w3.md) ·
[W4-Prüfstand](results/2026-10-09-w4.md).
W5 (WLAN-Stabilisierung) wird auf ausdrücklichen Nutzerwunsch ausgelassen;
dies betrifft nicht M5 (Integration). M2 ist softwareseitig umgesetzt: eigener
PIO/DMA-Build, gemeinsame Protokolldefinitionen und 68000-HUNK-Diagnose.
[Prüfstand](results/2026-10-09-m2.md). Nutzer-SELFTEST mit 64 ECHOs bestanden;
Testsystem: Amiga 500 mit 68020, Kickstart 1.3 und Workbench 1.3.
Realer 68000-Lauf und aktive M2-Abnahme bleiben offen.
M0 bleibt vor aktiven Bustests erforderlich. M4 ist durch die vorhandene
Flashprofil-/Autoconnect-Basis vorbereitet; Amiga-Konfigurationskommandos und
die weitergehende Abnahme fehlen noch. M2-Hardwareabnahme sowie M5–M6 bleiben offen.

M1 und isolierte M3-Arbeiten können vor Abschluss M0 beginnen. M2-Bustests sind
von M0 abhängig; M5 benötigt M2–M4. Keine Kalenderzusage ohne verfügbares Muster,
Messungen und geklärten Resetpfad. Optimierung erfolgt nach funktionaler Abnahme.

Das Amiga-Diagnoseprogramm verwendet ausschließlich 68000-/V34-taugliche APIs,
reserviert die Parallelport-Ressourcen und stellt sie beim Beenden wieder her.
Es läuft zunächst exklusiv ohne SANA-II-Treiber; später greift das Prefs-Tool über
dessen Device-Schnittstelle zu. Beide dürfen nie gleichzeitig die CIA steuern.
Ein kleiner ARP/ICMP-Test verwendet eine explizit gewählte, freie Test-IP; ein
vollständiger TCP/IP-Stack wird nicht in das Diagnoseprogramm eingebaut.

### Vorbereitung ohne Trägerplatine (10.10.2026)

Der Nutzer hat noch keine FunkOtto-Trägerplatine zur Verfügung. M0-Messungen
sind deshalb noch nicht möglich; die zuvor beschriebene Messfolge ist eine
Anleitung, kein durchgeführter Test. Der einzelne Pico 2 W und der Amiga 500
mit 68020, Kickstart 1.3 und Workbench 1.3 stehen für getrennte Softwaretests
zur Verfügung. W5 bleibt ausgelassen.

Empfohlene Reihenfolge für die weitere Softwarearbeit; die folgenden Erweiterungen
waren zunächst geplant. Die Konfigurationsbefehle und der USB-Prüfzugang sind
inzwischen als eigenes Ziel `funkotto_m4_usb` softwareseitig umgesetzt;
[Umsetzungsplan](konfigurationsprotokoll-plan.md),
[Prüfstand](results/2026-10-10-m4-usb.md). Amiga-Client und SANA-II sind weiter offen:

1. **M4-Konfigurationsbefehle vorbereiten.** Gemeinsame Operationen für Status,
   Scan/Ergebnisse, RAM-Profil, Verbinden/Trennen sowie explizites Speichern/Löschen
   auf die bestehende W4-Basis aufsetzen. Binärformate, Versions-/Fähigkeitskennung,
   Auftragskennung und asynchrone Abschlussmeldung festlegen. Wiederholte
   Übertragung desselben Auftrags darf insbesondere keine erneute Flashoperation
   auslösen. M2-Diagnoseprotokoll und bestehende USB-Diagnose versioniert erhalten;
   neue Funktionen nur bei passender gemeldeter Fähigkeit verwenden.
2. **USB-Prüfzugang am einzelnen Pico.** Dieselben Kommandohandler über einen
   ausdrücklich aktivierten PC-Testtransport ansprechen, ohne Parallelportfreigabe.
   Vorhandene Konsole und WLAN-Profile erhalten. Längen-/Statusfehler, besetzte
   Auftragsqueue, Wiederholungen und veraltete Sitzungen prüfen. Passwörter niemals
   in Antworten, Konsolenlogs, Reports oder Testmitschnitten ausgeben; für
   Protokollmitschnitte ausschließlich synthetische Zugangsdaten verwenden.
3. **Amiga-Konfiguration zuerst als Shellprogramm.** 68000-/V34-kompatiblen Client
   mit austauschbarem Transport entwickeln. Ein expliziter Simulationsmodus
   erlaubt auf dem A500 Eingabe, SSIDs mit Leerzeichen, verdeckte Passworteingabe,
   Statusanzeige, Fehlerbehandlung und Abbruch zu testen, ohne CIA-Zugriffe.
   Er simuliert einen Adapter im Programm; damit ist noch keine Verbindung
   zwischen Amiga und USB-Pico hergestellt. Speichern/Löschen nur ausdrücklich.
   Danach kann die Intuition-Oberfläche auf derselben Clientlogik aufbauen.
4. **M5 und SANA-II vorbereiten.** Paket-/Konfigurationsschnittstelle, Pufferbesitz,
   Rückdruck und Fehlerabbildung mit simuliertem Transport prüfen. Anschließend
   Device-Grundgerüst und Request-/Abort-/Close-Lebenszyklen vorbereiten.
   Paketbenachrichtigung über ACK/CIA-Interrupt konzeptionell und in Simulation
   prüfen; die M2-Pollingdiagnose legt den späteren Treiberbetrieb nicht fest.
   Kein Durchsatz- oder Interrupt-Timingnachweis ohne Trägerplatine.

Abnahme dieser Vorarbeiten: PC/Pico-USB-Test und portfreier Amiga-Test werden
getrennt dokumentiert. M0 und die aktive M2-Abnahme bleiben offen. Sobald die
Platine vorliegt: Versorgung/Reset/Sperrzustände messen, danach kontrollierte
M2-Transfers und erst anschließend den vorbereiteten Konfigurationsclient über
den echten Parallelport testen. Diese Vorarbeiten ersetzen keine M5-Hardwareabnahme.

### Prüfmatrix

- Hosttests: Parser, CRC-Vektoren, Längengrenzen, abgeschnittene Rahmen,
  Duplikate, Sequenzüberlauf, Queue-Überlauf und simulierte Flashabbrüche.
- Hardware: beide Richtungen mit 00/FF/55/AA, Walking-Bit und Pseudozufallsdaten;
  mindestens 100.000 Transfers mit unterschiedlichen Längen und Langzeitlauf.
- WLAN: korrektes/falsches Passwort, fehlender AP, AP-Neustart, Funkverlust,
  nicht gespeichertes Profil und vollständiger Stromverlust.
- Reset: Amiga-Neustart während Kommando, Senden, Empfangen und Flashspeichern;
  beide Einschaltreihenfolgen sowie Pico-Neustart bei laufendem Amiga.
- Bootmessung: stabile Versorgung→erste Antwort, WLAN-Init, WLAN-Link und
  Amiga-Netzwerkbereitschaft getrennt; Minimum/Median/Maximum dokumentieren.
- OS-Test: echter 68000-Amiga mit OS 1.3. Emulator prüft ABI/Fehlerpfade,
  nicht das elektrische CIA-Timing.

Der vorhandene 8-Kanal-/24-MHz-Analysator kann Handshakeabläufe in mehreren
Messdurchgängen erfassen. Für Pegel, Überschwingen und genaue Flanken ist das
Oszilloskop nötig; eine digitale 24-MHz-Abtastung beweist keine analoge Signalgüte.

## 9. Geplante Repository-Ergebnisse

- `firmware/`: Build, Quellen und Boarddefinition.
- `protocol/`: freigegebene Protokollspezifikation und gemeinsame Definitionen.
- `amiga/diag/`: erstes Diagnose-/Konfigurationsprogramm.
- `tests/`: Hosttests, Prüfvektoren und Hardwaretest-Anleitungen.
- `docs/results/`: Messberichte mit Hardware-, Firmware- und Testumgebungsversion.
- `.github/workflows/`: reproduzierbare Builds und aussagekräftige Prüfungen.

Später: `amiga/device/` für SANA-II und `amiga/prefs/` für die OS-1.3-GUI.
Es werden keine Dummy-Treiber oder leeren Tests als fertige Funktionen ausgegeben.
Implementiert sind M1, WLAN W1–W4 sowie M2-Diagnoseprotokoll, PIO/DMA und
Amiga-CLI. SANA-II, Amiga-WLAN-Konfiguration, gemeinsame Integration und
weitergehende Hardwareabnahmen bleiben ausstehend.

## 10. Quellen und Referenzen

Technische Quellen geprüft am 05.10.2026. Vor Implementierung M1/M3 die tatsächlich
verwendeten SDK-/Treiberstände festschreiben; Beispiele ersetzen keine Messung.

- [Pico 2 W Datenblatt](https://datasheets.raspberrypi.com/picow/pico-2-w-datasheet.pdf)
- [Pico SDK: Netzwerk und CYW43](https://www.raspberrypi.com/documentation/pico-sdk/networking.html)
- [Pico SDK: Flash und Multicore](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html)
- [CYW43-Treiber: Ethernet und Join](https://github.com/georgerobotics/cyw43-driver/blob/main/src/cyw43_ctrl.c)
- [Pico SDK Architekturdefinitionen](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_cyw43_arch/include/pico/cyw43_arch.h)
- [TI SN74LVC2G07: Open-Drain-Puffer, Ioff, Pinbelegung](https://www.ti.com/lit/gpn/sn74lvc2g07)
- Eigener PCB-Netzabgleich des Nutzerstands vom 08.10.2026 für die M1-Pinbelegung.
- [Festgelegter SDK-Stand und Werkzeuge](../firmware/dependencies.json).

Vor Wiederverwendung fremden Codes dessen Lizenz prüfen und Hinweise erhalten.
Dieser Plan entscheidet noch nicht über die Lizenz des späteren FunkOtto-Codes.


