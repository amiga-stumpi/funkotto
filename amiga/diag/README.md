# FunkOttoDiag M2

Eigenständiges CLI-Diagnoseprogramm für 68000 und AmigaOS 1.3 (V34).
Kein SANA-II-Treiber. Start von Workbench wird ohne Portzugriff beendet und die
Workbench-Nachricht beantwortet. Befehle und Laborablauf: [M2_TEST](../../firmware/M2_TEST.md).

```text
Stack 8192
FunkOttoDiag SELFTEST
FunkOttoDiag RUN M0-VERIFIED 64
```

Der erste Befehl greift nicht auf die CIA zu. Der zweite ist der aktive
Labortest und setzt M0 sowie explizit aktivierte M2-Firmware voraus.

## Reproduzierbarer Build

Ubuntu 24.04: `gcc-13-m68k-linux-gnu` 13.3.0-6ubuntu2~24.04cross1,
`binutils-m68k-linux-gnu` 2.42-4ubuntu2.10. Keine fremde Amiga-SDK- oder
Kickstart-Distribution. Der Crosscompiler erzeugt ausschließlich freistehenden
68000-Code (`-m68000`, `-msoft-float`, ohne libc/CRT/Linux-Aufrufe).

```sh
python3 tools/build_amiga_diag.py --source-id <quellstand>
python3 -m pip install unicorn==2.1.4
python3 tests/run_m2_amiga_emulation.py build/amiga-m2/FunkOttoDiag
```

`--cc`, `--prefix`, `--output` erlauben abweichende Werkzeugpfade. Der Linker
erstellt eine ELF-Zwischenstufe mit Relokationen. Der geprüfte Konverter erzeugt
einen Amiga-CODE-Hunk mit RELOC32. Unbekannte Relokationen und externe Symbole
werden abgelehnt. Das ELF ist kein Amiga-Programm; auszuführen ist ausschließlich
`FunkOttoDiag`. Ein Manifest nennt Compiler, Quellstand und Prüfsumme.

Die Bibliotheksstubs übersetzen die C-Aufrufkonvention in Amiga-Registerargumente,
einschließlich D0→A0 bei Zeigerrückgaben. Nur V34-taugliche Aufrufe; kein
`ReadArgs`, `Printf`, `ReadEClock` oder sonstiges V36-API. Kurze Disable/Enable-
Abschnitte schützen einzelne CIA-Registerfolgen. Wartephasen bleiben unterbrechbar.

Die Emulation lädt die tatsächliche HUNK-Datei an einer anderen Basisadresse,
wendet ihre Relokationen an, nutzt ein 68000-CPU-Modell, prüft ungerade Wortzugriffe,
ABI und Fehlerpfade. OS-Vektoren und CIA/Adapter sind simuliert; das ist kein
vollständiger OS-1.3-/FS-UAE-Nachweis und keine elektrische Hardwareabnahme.
