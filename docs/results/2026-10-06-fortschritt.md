# FunkOtto – Fortschritt am 06.10.2026

Aktueller Hardwarestand: **Rev B1**, KiCad 10. Softwarestand: Entwicklungsplan
für die erste funktionale Firmware; noch keine implementierte Firmware,
kein SANA-II-Treiber und kein Amiga-Konfigurationsprogramm im Repository.

## Erreicht

| Bereich | Stand |
| --- | --- |
| Hardwarebasis | Vom Nutzer bearbeitete Rev-A-Fassung als Grundlage übernommen |
| Reset-Erweiterung | Pico-RUN-Reset in Schaltplan und Board ergänzt, einschließlich Routing |
| KiCad-Korrektur B1 | Netznamen, NC-Netze, Bauteilattribute, Bibliothekskopien und Beschriftungen abgeglichen |
| PCB-Prüfung | DRC mit Schaltplanvergleich: 0 Fehler, 0 Warnungen, 0 offene Verbindungen, 0 Schaltplanabweichungen |
| Schaltplanprüfung | ERC: 0 Fehler, 0 Warnungen |
| Firmwareplanung | Hardwarezuordnung, Startablauf, Protokollrahmen, WLAN-Rohframes, persistentes Profil und Abnahme definiert |

Die 56 Bauteilpositionen, Padgeometrien, Leiterbahnen, Vias, Kupferflächen und
Außenkontur sind bei der Korrektur von Rev B auf B1 erhalten geblieben.
Beschriftungspositionen und Bibliothekszuordnungen wurden gezielt korrigiert.

## Festgelegte Hardwareentscheidungen

### Reset

Ein am Adapter anliegender Hardware-Amiga-Reset setzt auch den Pico zurück.
Fehlende Hostversorgung hält ihn ebenfalls in Reset. Die vorhandene
Bustreiber-Sperrlogik U5/U6 bleibt erhalten.

Gegenüber der Nutzerfassung Rev A wurden ergänzt:

| Referenz | Bauteil | Funktion |
| --- | --- | --- |
| U8 | SN74LVC2G07DBVR, SOT-23-6 | Zwei nichtinvertierende Open-Drain-Puffer; gemeinsame Ausgänge an Pico RUN |
| C11 | 100 nF | Versorgungspufferung für U8 |
| R36 | 100 kΩ | Pulldown für RESET_IN |
| TP7 | Testpunkt | Messzugang zu PICO_RUN_N |

**U8 ist der einzige zusätzliche IC.** Von Rev B auf B1 kamen keine weiteren
Bauteile hinzu; die drei PWR_FLAG-Symbole sind reine Schaltplankennzeichnungen.

U8 zieht `PICO_RUN_N` auf Low, wenn `RESET_IN` oder `HOST_PRESENT` Low ist.
Pico **Pin 30 (RUN)** wird nach Freigabe durch seinen internen Pullup angehoben.
Die Firmware muss nach diesem Reset vollständig neu starten, eine neue
Protokollsitzung aufbauen und WLAN erneut verbinden. Das gespeicherte Profil
soll im Flash erhalten bleiben; diese Softwarefunktion ist noch umzusetzen.

### Stromversorgung

Der Pico erhält seine Betriebsspannung über **Modulpin 39 (VSYS)**:

`USB-C J1, 5 V → Schottky-Diode D1 (SS14) → PICO_VSYS → Pico Pin 39`

Masse ist unter anderem an **Pin 38 (GND)** angeschlossen. Der Pico erzeugt
seine internen 3,3 V mit dem Regler auf dem Modul. Der Trägerregler versorgt
die externe 3,3-V-Logik. Pico VBUS/Pin 40 und 3V3_OUT/Pin 36 sind nicht an die
entsprechenden Trägerspannungen angeschlossen.

### Installation und Updates

Firmwareinstallation und Updates erfolgen vorerst über USB/BOOTSEL am Pico.
Updates über den Amiga bleiben zurückgestellt.

Für BOOTSEL im Sockel **DB25 und USB-C-Trägerversorgung trennen**, dann den
Pico an seinem eigenen USB-Anschluss verbinden. Alternativ Pico herausnehmen.
Ein versorgter Träger ohne Amiga hält RUN absichtlich Low. Versorgungstrennung
und Rückspeisungsverhalten sind am Prototyp noch zu prüfen.

## Prüfungen und Korrektur der früheren Aussage

Geprüft mit KiCad 10.0.6:

```sh
kicad-cli pcb drc --schematic-parity --all-track-errors -o DRC.rpt hardware/AmiWiFi.kicad_pcb
kicad-cli sch erc -o ERC.rpt hardware/AmiWiFi.kicad_sch
```

Die Befehle werden im entpackten Hardwarepaket ausgeführt. Das Hardwarepaket
liegt separat vor; dieser Fortschrittsstand ergänzt die Dokumentation und
Prüfberichte im Software-Repository.

Die ursprüngliche Prüfung von Rev B ließ den nativen Schaltplanvergleich aus.
Der zusätzliche eigene Vergleich normalisierte Netznamen durch Entfernen von
`/` und übersprang offene Pins. Damit wurden echte KiCad-Projektabweichungen
übersehen. Die Nutzerberichte zeigten 73 DRC-Warnungen, zusätzlich 208
Schaltplan-/Footprintmeldungen und drei ERC-Versorgungsfehler.

Rev B1 korrigiert insbesondere `GND` gegenüber `/GND`, die generierten NC-Netze,
J1-Felder und Testpunktattribute. Der U7-Schaltplan bildet jetzt genau den
bestückten Teil-Sockel mit Pins 1–15 und 26–40 ab. Die Versorgungseinspeisungen
sind mit PWR_FLAG gekennzeichnet. U4 verwendet ein Symbol für die tatsächlich
fest verdrahtete Richtung B→A bei DIR=GND.

Es wurden keine zusätzlichen Prüfregeln deaktiviert oder neuen Ausnahmen
angelegt. Bereits vorher ignorierte Kategorien stehen unverändert am Ende
der Berichte. Die Null-Befunde gelten für die aktiven Projektprüfungen und
belegen noch keine elektrische Funktion oder Fertigungsfreigabe.

Prüfnachweise:

- [Nativer DRC-Bericht](2026-10-06-revb1/KiCad_DRC.txt)
- [Nativer ERC-Bericht](2026-10-06-revb1/KiCad_ERC.txt)
- [Prüfzusammenfassung und Umfang](2026-10-06-revb1/checks.json)

## Nächste Schritte

1. **M0, elektrische Hardwareabnahme:** Versorgung, Einschaltreihenfolgen,
   RESET_IN/HOST_PRESENT/RUN, Pulsbreiten und beide OE-Signale messen.
   Kein Wiederanlauf alter Busausgänge nach Resetfreigabe. Aktive Amiga-Bustests
   erst nach erfolgreicher Abnahme.
2. **M1, Firmwaregrundlage:** Pico-SDK und Toolchain festlegen, reproduzierbaren
   UF2-Build, sichere GPIO-Startpegel und Versions-/Statusausgabe implementieren.
3. **M2, Amiga-Link:** Erst konservativer Handshake, HELLO/ECHO und ein
   68000-/OS-1.3-taugliches Diagnoseprogramm; danach PIO/DMA-Blocktransfers.
4. **M3/M4, WLAN und Profil:** Ethernet-Rohframes nachweisen, Profilbefehle und
   zwei Flashsektoren für unterbrechungssicheres Speichern/Löschen implementieren.
5. **Integration:** ARP/ICMP vom Amiga, Reset während Transfers/Flashoperationen,
   Wiederverbindung, Durchsatz und Dauerlauf messen.

M1 und isolierte WLAN-Arbeiten können bereits vor M0 beginnen. SANA-II-Treiber
und native Amiga-Konfigurationsoberfläche bleiben Folgepakete. Bootzeit und ein
Vorsprung gegenüber der Plipbox sind bisher ungemessene Entwicklungsziele.

Der verbindliche Ablauf steht im [Firmware-v0.1-Plan](../firmware-v0.1-plan.md).

## Identifikation des Hardwarestands

Separates Paket: `FunkOtto_Pico2W_RevB1.zip`. SHA-256:

| Datei | SHA-256 |
| --- | --- |
| ZIP | `71fa696c74a5893066b6ea507cb20f81100e55547d6f991705b95405922db9ba` |
| `hardware/AmiWiFi.kicad_sch` | `f0fb988d79334e68befdd067eee3bf335fcb88e4ed832d5c7ed0fed0b4c8fd65` |
| `hardware/AmiWiFi.kicad_pcb` | `c3a6cf897fa701b54783b80b83dd6cc1cf037b86b4e3dd8d645b2d0211e7ec6f` |
