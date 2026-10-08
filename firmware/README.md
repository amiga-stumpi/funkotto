# FunkOtto – erste Pico-Firmware (M1)

`0.1.0-m1` ist die erste USB-Diagnose-Firmware für **Pico 2 W / RP2350 Arm**.
Sie ist noch kein Netzwerkadapter. M1 liefert Build, sicheren Grundstart,
USB-Konsole und Watchdog. WLAN, Parallelport-Protokoll, Profilspeicherung und
SANA-II folgen in den nächsten Arbeitspaketen.

## Implementiert

- GPIO-Initialisierung vor USB/Diagnose: GP21 `FW_DATA_EN` und GP22 `FW_CTRL_EN`
  dauerhaft Low, GP20 `DATA_DIR` High (Amiga → Pico).
- GP0–GP11 und GP26 bleiben Eingänge; BUSY/ACK werden nur im Ausgangslatch
  vorbereitet. Es gibt in M1 keinen Befehl zum Einschalten der Bustreiber.
- Keine UART-Ausgabe auf den Datenpins GP0/GP1; keine Nutzung freier GP27/GP28.
- USB-CDC-Diagnose mit `help`, `info`, `status`, begrenztem Zeilenpuffer und
  begrenzter Eingabearbeit pro Hauptschleife.
- Start wartet nicht auf ein angeschlossenes USB-Terminal. USB-Ausgaben haben
  einen kurzen Timeout; ein verpasster Starttext lässt sich mit `info` abrufen.
- 8-s-Watchdog; unerwartete Pinzustände führen zur erneuten Sperrung und zum
  Watchdog-Neustart. Externe 4,7-kΩ-Pulldowns sichern die Zeit vor Firmwarestart.
- Letzte 8192 Byte des 4-MiB-Flashs für spätere Profile reserviert:
  Flashoffsets `0x003FE000` und `0x003FF000`, XIP ab `0x103FE000`.
  M1 schreibt oder löscht dort nichts. Linker und UF2-Prüfung sichern die Grenze.
- Ein weiterer 4-KiB-Sektor direkt davor (`0x103FD000`–`0x103FDFFF`) bleibt für
  den RP2350-E10-Kompatibilitätsblock reserviert. Die UF2 setzt diesen Block
  gezielt auf `0x103FDF00`; die SDK-Standardadresse `0x10FFFF00` wird vermieden.
  Diese Trennung ist vor der späteren Profilspeicherung am Muster zu testen.

Core 0 bedient Diagnose und Watchdog; Core 1 ist noch unbenutzt. Es gibt keine
eigenen PIO-/DMA-Belegungen. SDK/TinyUSB verwenden ihre USB-Interrupt- und
Timermechanismen. Die Ressourcenaufteilung mit CYW43 folgt vor M2/M3.
Die Traffic-LED und Polyfuse bleiben Einträge in der Ideenliste.

## Bauen (Linux, Referenz Ubuntu 24.04)

Versionen und Commit-IDs stehen in [`dependencies.json`](dependencies.json).
Der SDK-Build wird auf Pico SDK 2.2.0 und dessen TinyUSB-Commit geprüft;
picotool 2.2.0 wird anhand einer festen Commit-ID bezogen.

```sh
sudo apt-get update
sudo apt-get install --no-install-recommends git build-essential python3 python3-venv \
  gcc-arm-none-eabi=15:13.2.rel1-2 \
  binutils-arm-none-eabi=2.42-1ubuntu1+23 \
  libnewlib-arm-none-eabi=4.4.0.20231231-2 \
  libnewlib-dev=4.4.0.20231231-2 \
  libstdc++-arm-none-eabi-dev=15:13.2.rel1-2+26
python3 -m venv .venv
. .venv/bin/activate
python -m pip install cmake==3.31.6 ninja==1.11.1.4
python tools/bootstrap_sdk.py
python tools/check_board_contract.py
python tests/run_host_tests.py
python -m unittest discover -s tests -p 'test_*.py'
cmake -S firmware -B build/m1 -G Ninja \
  -DPICO_SDK_PATH="$PWD/.deps/pico-sdk" \
  -DCMAKE_BUILD_TYPE=Release \
  -DFUNKOTTO_SOURCE_ID="$(git rev-parse --short=12 HEAD)"
cmake --build build/m1 --parallel
python tools/check_firmware_artifacts.py build/m1
```

Beim ersten Build werden SDK, TinyUSB und picotool benötigt; Netzwerkzugang ist
dafür erforderlich. Warnungen zu nicht initialisierten WLAN/Bluetooth-Submodulen
sind bei M1 erwartbar: Diese Bibliotheken werden noch nicht eingebunden.
In einer Umgebung ohne Zugriff auf `/proc` kann LeakSanitizer nicht arbeiten;
für die heapfreien C-Tests dort `ASAN_OPTIONS=detect_leaks=0` setzen.
AddressSanitizer und UndefinedBehaviorSanitizer bleiben dabei aktiv.

Ergebnisse: `funkotto_m1.uf2`, `funkotto_m1.elf`, `funkotto_m1.elf.map`,
`funkotto_m1.bin` und `manifest.json` mit Quellen-/Artefaktprüfsummen und
Werkzeugversionen. Die CI baut und prüft dieselben Ziele. SDK-Dateipfade werden
im Zielcode normalisiert; eine Zusage für beliebige andere Toolchainstände
oder bitidentische Debug-Dateien ergibt sich daraus nicht.

## Erster Test am Pico

1. Am einfachsten den Pico aus dem Trägersockel nehmen. Für den USB-Service im
   Sockel **DB25 und USB-C-Trägerversorgung abziehen**. USB-C am Träger ist nur
   Versorgung, nicht die Programmierschnittstelle.
2. BOOTSEL gedrückt halten und den eigenen USB-Port des Pico mit dem PC verbinden.
3. `funkotto_m1.uf2` auf das BOOTSEL-Laufwerk kopieren. Der Pico startet neu.
4. Den neuen seriellen USB-Port öffnen (z. B. 115200, 8N1, keine Flusssteuerung).
   Befehle mit Enter abschließen. Die eingestellte Baudrate ist bei USB-CDC
   kein physischer UART-Takt.
5. `info`, danach `status` eingeben. Erwartet: `stage=M1`, `bus_locked=1`,
   `output_mask=0x00700000`. Nur die drei Steuer-GPIOs 20/21/22 sind Ausgänge.
6. Pico stromlos machen, erneut verbinden und wieder `status` abfragen.

`safe_init_us` ist der interne Timerstand nach GPIO-Initialisierung. Er ist
**keine Messung Versorgung → Parallelport-Bereitschaft**; M1 bietet noch keine
Parallelport-Antwort. Ohne WLAN-Initialisierung leuchtet auch die Pico-WLAN-LED
nicht automatisch. Ein ausgeschaltetes Terminal verhindert den Firmwarestart nicht.

Messungen am Träger gemäß M0 bleiben offen. Der bisherige Softwaretest belegt
keine elektrischen Pegel, Reset-Pulsbreiten oder reale Laufzeit auf dem Pico.
