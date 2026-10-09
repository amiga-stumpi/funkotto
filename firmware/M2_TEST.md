# FunkOtto M2: erster Amiga-Link

M2 ist als eigene Diagnosefirmware `funkotto_m2` implementiert. W4 bleibt separat
verfügbar. M2 enthält **kein WLAN** und verändert keine WLAN-Profile. Ein normaler
UF2-Update muss weiterhin am Gerät auf Profilerhalt geprüft werden; kein Full-Erase
verwenden. KiCad-Dateien wurden nicht verändert.

## Jetzt ohne aktive Bustests möglich

`FunkOttoDiag` aus `amiga/` auf den Amiga kopieren und aus der Shell starten:

```text
Stack 8192
FunkOttoDiag SELFTEST
```

Erwartet: `HELLO session=...`, `ECHO verified count=0x00000040`, anschließend
`PASS: local protocol selftest; no CIA access.` Dieser Selbsttest benötigt keinen
Adapter und greift nicht auf den Parallelport zu. Die 8-KiB-Stackeinstellung lässt
Reserve für Betriebssystemaufrufe. Bitte Amiga-Modell, CPU,
Kickstart-/Workbench-Version und Ausgabe festhalten. Dies ist der nächste
OS-1.3-Test; die bisherige 68000-Emulation verwendet simulierte OS-Aufrufe.

Die mitgelieferte `funkotto_m2.uf2` ist ein **gesperrter Standard-Build**:
`active_build=0`. Beim Flashen wie bisher den Pico entnehmen oder DB25 und
USB-C-Trägerversorgung trennen, dann BOOTSEL am Pico benutzen.
USB-Kommandos `info` und `status` funktionieren ohne WLAN und ohne Terminal beim
Boot. Erwartet: `state=0 bus_locked=1`. `link arm M0-VERIFIED` wird in diesem
Build abgelehnt. Ein Stromausfall bleibt damit im sicheren Bootzustand.

## Laborbetrieb nach elektrischer M0-Prüfung

Ein aktiver Build wird bewusst separat erstellt; er ist nicht die Standard-UF2.
Voraussetzung sind die Messungen am Nutzerboard aus dem
[M0-Abschnitt](../docs/firmware-v0.1-plan.md): Versorgung, RUN/Reset,
OE-Sperrung, Pegel und kontrollierte Richtungsübergabe. Keine Abnahme aus einem
CI-Ergebnis ableiten. Die Messungen bei realen Transfers gehören zum Laborablauf.

```sh
cmake -S firmware -B build/m2-lab -G Ninja \
  -DPICO_SDK_PATH=/pfad/zum/gepinnten/pico-sdk \
  -DCMAKE_BUILD_TYPE=Release -DFUNKOTTO_BUILD_WIFI=OFF \
  -DFUNKOTTO_M2_ACTIVE=ON -DFUNKOTTO_SOURCE_ID=<quellstand>
cmake --build build/m2-lab --target funkotto_m2
```

Auch dieser Build startet gesperrt. Nach der Messfreigabe im USB-Terminal:

```text
info
link arm M0-VERIFIED
status
```

Erwartet: `active_build=1`, `ARMED; waiting for host sync`. Nun auf dem Amiga,
ohne parallel.device/printer.device oder einen weiteren Portnutzer:

```text
FunkOttoDiag RUN M0-VERIFIED 64
```

Das Tool reserviert beide Parallelport-Ressourcen, synchronisiert, führt HELLO
und 64 geprüfte ECHOs aus. Längen: 0, 1, 2, 7, 63, 255, 512 und 1500 Bytes.
Muster wechseln zwischen 00, FF, 55, AA, Walking-One, Walking-Zero und
Pseudozufallsbytes. Ein Fehler beendet den Lauf mit Rückgabecode 20;
Erfolg mit 0. Ctrl-C bricht Wartephasen ab. Nach Fehlern nicht blind
fortsetzen: Ausgabe und `status` am Pico sichern.

Bei bestandenem ersten Lauf anschließend größere Serien bis 100.000 ECHOs,
Reset in beiden Richtungen, abgebrochene Transfers und beide Einschaltfolgen
prüfen. Der erste Lauf ist kein Durchsatz- oder Langzeitnachweis. IRQ-/SANA-II-
Treiber, Amiga-WLAN-Konfiguration und Netzwerkverkehr gehören zu Folgepaketen.

Zum Sperren:

```text
link lock
status
```

Wieder erwartet: `state=0 bus_locked=1`. Vor Verwendung des WLAN-Diagnosepfads
die bereits getestete W4-UF2 wieder installieren.

## Statuswerte und Messpunkte

| state | Bedeutung |
| --- | --- |
| 0 | Gesperrt, keine PIO-/DMA-Ressourcen reserviert |
| 1 | Warten auf SEL-Synchronisation; Daten gesperrt |
| 2 | SEL High bestätigt, Daten gesperrt |
| 3 | Empfang / RX-DMA |
| 4 | Request geprüft, Warten auf Host-Richtungsübergabe |
| 5 | TX-DMA lädt erstes Byte; externe Daten noch gesperrt |
| 6 | Antwort senden |
| 7 | Letztes Byte quittiert, Warten auf SEL Low |

`transactions` zählt vollständig quittierte Antworten. `aborts`, `timeouts`
und `invalid` zählen verworfene Transfers. Nach Rückgabe an die Shell kann der
RX-Leerlauf nach zwei Sekunden in state=1 wechseln; das ist keine offene
Datenausgabe. Der nächste RUN synchronisiert neu.

Aufzeichnen: STROBE_N/TP3, SEL/TP4, BUSY, POUT, DATA_OE_N/TP5, DATA_DIR/TP6
und RUN/TP7; einzelne Datenbits in weiteren Messdurchläufen. Prüfen, dass
DDRB vor SEL High auf Eingang steht, DATA_OE_N während der Richtungsänderung
High ist und POUT erst nach stabilem ersten Lesebyte High wird. Digitale
Logikanalysator-Aufnahmen ersetzen keine analogen Pegel-/Flankenmessungen.

[Protokoll](../protocol/M2.md) · [Prüfbericht](../docs/results/2026-10-09-m2.md)
