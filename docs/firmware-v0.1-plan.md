# FunkOtto: Plan für die erste funktionale Firmware v0.1

Stand: 05.10.2026 · Status: Entwicklungsplan, noch keine Implementierung

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

## 3. Verbindliche Hardwarebasis: Rev B mit Pico-Reset

Basis ist `FunkOtto_Pico2W_RevB.zip`, abgeleitet aus der **vom Nutzer bearbeiteten**
KiCad-10-Datei `AmiWiFi_Pico2W_RevA(1).zip`. AmiWiFi bleibt der KiCad-Dateiname.
Kontur, bestehende Bauteilpositionen, Beschriftungen, 600 Leiterbahnsegmente,
63 Vias und Flächeneinstellungen der Nutzerfassung bleiben erhalten; Füllungen
werden um die neuen Bauteile und Verbindungen aktualisiert.

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
SN74LVC2G07DBVR mit zwei nichtinvertierenden Open-Drain-Ausgängen, `RESET_IN`
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

Digitale Prüfung mit KiCad 10.0.6: 0 PCB-DRC-Fehler, 0 offene Verbindungen,
73 vorhandene Beschriftungswarnungen. ERC: 3 bereits in der Nutzerfassung
vorhandene Fehler für nicht als getrieben erkannte Versorgungspins (U1 GND,
U1 IN, U7 VSYS); keine zusätzlichen Meldungen. 220 angeschlossene PCB-Pads
mit nativ exportierter Schaltplan-Netzliste abgeglichen. Keine Fertigungsfreigabe.

Hardware-Referenzen, SHA-256:

- Nutzer-Archiv: `923df59e6a17f6ac96b90cc4c9c0e388622e9b6f66aeec76e7cd953a6bc07e1f`
- Rev-B-Schaltplan: `30d87fa32a20574fe839f5839d823b3b1f2fcf8a8ecf895ad6c0d82132d865de`
- Rev-B-PCB: `b20890a57fae0c7c1573d8c3f4a35c496433d36aea16e5a9d12943aa7764c8a8`

## 4. Architektur und Build

C/C++ mit Raspberry Pi Pico SDK, Ziel `PICO_BOARD=pico2_w`, Arm-Build für RP2350.
SDK-Release, Commit, Submodule, Compiler und CMake-Version in M1 festlegen und
versionieren. Kein gleitendes `master` als reproduzierbare Buildabhängigkeit.
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

Die folgenden Angaben sind ein Entwurfsrahmen. M2 liefert erst die verbindliche
Signal-/Timing-Spezifikation mit Oszillogrammen und Host-Registerfolge.

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

M1 und isolierte M3-Arbeiten können vor Abschluss M0 beginnen. M2-Bustests sind
von M0 abhängig; M5 benötigt M2–M4. Keine Kalenderzusage ohne verfügbares Muster,
Messungen und geklärten Resetpfad. Optimierung erfolgt nach funktionaler Abnahme.

Das Amiga-Diagnoseprogramm verwendet ausschließlich 68000-/V34-taugliche APIs,
reserviert die Parallelport-Ressourcen und stellt sie beim Beenden wieder her.
Es läuft zunächst exklusiv ohne SANA-II-Treiber; später greift das Prefs-Tool über
dessen Device-Schnittstelle zu. Beide dürfen nie gleichzeitig die CIA steuern.
Ein kleiner ARP/ICMP-Test verwendet eine explizit gewählte, freie Test-IP; ein
vollständiger TCP/IP-Stack wird nicht in das Diagnoseprogramm eingebaut.

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
Dieser Commit enthält ausschließlich den Plan; die beschriebenen Module existieren
noch nicht als funktionsfähige Implementierung.

## 10. Quellen und Referenzen

Technische Quellen geprüft am 05.10.2026. Vor Implementierung M1/M3 die tatsächlich
verwendeten SDK-/Treiberstände festschreiben; Beispiele ersetzen keine Messung.

- [Pico 2 W Datenblatt](https://datasheets.raspberrypi.com/picow/pico-2-w-datasheet.pdf)
- [Pico SDK: Netzwerk und CYW43](https://www.raspberrypi.com/documentation/pico-sdk/networking.html)
- [Pico SDK: Flash und Multicore](https://www.raspberrypi.com/documentation/pico-sdk/high_level.html)
- [CYW43-Treiber: Ethernet und Join](https://github.com/georgerobotics/cyw43-driver/blob/main/src/cyw43_ctrl.c)
- [Pico SDK Architekturdefinitionen](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/pico_cyw43_arch/include/pico/cyw43_arch.h)
- [TI SN74LVC2G07: Open-Drain-Puffer, Ioff, Pinbelegung](https://www.ti.com/lit/gpn/sn74lvc2g07)
- Eigener Schaltplan-/PCB-Netzlistenabgleich des oben bezeichneten Rev-B-Pakets
  und Vergleich mit der unveränderten Nutzerfassung.

Vor Wiederverwendung fremden Codes dessen Lizenz prüfen und Hinweise erhalten.
Dieser Plan entscheidet noch nicht über die Lizenz des späteren FunkOtto-Codes.
