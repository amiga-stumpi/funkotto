# FunkOtto W1/W2 – erste WLAN-Testfirmware

Version `0.1.0-w12`, Ziel Pico 2 W. Enthält WLAN-Initialisierung, Scan, ein
RAM-Profil und WPA2-Personal-Verbindung im 2,4-GHz-Netz (Länderkennung DE).
**Noch keine Netzwerkfunktion am Amiga und keine dauerhafte Speicherung.**
Empfangene Ethernet-Pakete werden in dieser Stufe gezählt und verworfen.

## Flashen und erster Test

1. Pico am besten aus dem Trägersockel nehmen. Für Service im Sockel DB25 und
   USB-C-Trägerversorgung trennen. Den eigenen USB-Anschluss des Pico verwenden.
2. BOOTSEL halten, USB einstecken, `funkotto_w12.uf2` auf das Laufwerk kopieren.
3. Seriellen USB-Port öffnen (z. B. 115200, 8N1, keine Flusssteuerung).
   Lokales Echo ausschalten, damit das Passwort beim Eingeben nicht angezeigt wird.
4. `info`, `status`, `wifi status` eingeben. Nach WLAN-Initialisierung erwartet:
   `wifi=UNCONFIGURED`, `mac_valid=1`, `bus_locked=1`, `output_mask=0x00700000`.
   Der Start wartet nicht auf ein Terminal. Falls das Banner verpasst wurde: `info`.
5. `wifi scan` eingeben. Nach `scan finished ...` die Liste mit `wifi results`
   anzeigen. Die Liste enthält höchstens 32 BSSIDs; `truncated=1` zeigt Überlauf.
6. `wifi set` eingeben. Zuerst auf die SSID-Abfrage antworten und Enter drücken,
   danach auf die Passwort-Abfrage antworten und Enter drücken. Keine Anführungszeichen
   ergänzen. Leerzeichen werden beibehalten. Auf `result=OK` warten.
7. `wifi connect` eingeben, danach `wifi status`. Erfolg ist `wifi=LINK_UP`;
   das bedeutet authentifizierte WLAN-Verbindung, nicht eine IP-Adresse des Pico.
8. `wifi stats` und `status` abfragen. Der Bus muss weiterhin gesperrt sein.

Bitte als ersten Test `info`, `status`, `wifi status` und `wifi stats` zurückmelden.
Das Passwort nicht mitliefern; für Diagnosezwecke wird es nicht benötigt.

## Befehle

| Befehl | Bedeutung |
| --- | --- |
| `help`, `info`, `status` | Hilfe, Quellkennung und sichere Adapter-GPIOs |
| `wifi status` | Zustand, letzter Fehler/SDK-Code, MAC, RSSI, Versuche, Linkwechsel |
| `wifi scan` | Scan starten, nur ohne laufenden Verbindungsauftrag/Link |
| `wifi results` | Aktuellen Scan-Schnappschuss anzeigen; auch nach Abschluss verfügbar |
| `wifi set` | SSID und Passwort getrennt abfragen; Profil im RAM ersetzen und Verbindung beenden |
| `wifi sethex` | SSID als 2–64 Hexziffern eingeben, danach normales Passwort; für beliebige SSID-Bytes |
| `wifi connect` | RAM-Profil verbinden; anschließend automatisch erneut versuchen |
| `wifi disconnect` | Verbindung/Scan abbrechen und automatische Versuche stoppen |
| `wifi stats` | Init-/Joinzeit, Core-1-Lebenszeichen, RX-Drops und PIO-/DMA-Belegung |
| `wifi save`, `wifi erase` | Noch nicht implementiert; verändern keinen Flash |

`QUEUED` bestätigt nur die Annahme. Danach meldet Core 1 `wifi command=... result=...`.
Ein weiterer Auftrag kann `BUSY` liefern. Scan während Verbindung ist absichtlich
nicht möglich: zuerst `wifi disconnect`, dann scannen und danach wieder verbinden.
Ein ausgefülltes RAM-Profil bleibt bei `disconnect` erhalten.

SSID: 1–32 Bytes; Passphrase: 8–63 druckbare ASCII-Zeichen. Kein WPA3-only,
Enterprise-WLAN oder 64-stelliger Hex-PSK in dieser Stufe. SSIDs in Ergebnislisten
werden sicher maskiert, beispielsweise `\x1b` statt eines Terminal-Steuerzeichens.
Scan-`auth` ist die rohe SDK-Scan-Kennung, keine von uns erfundene Klassifizierung.

Ctrl-C bricht eine begonnene Eingabe ab. Ebenso werden unvollständige Eingaben
bei USB-Terminaltrennung oder nach 60 s ohne Eingabe verworfen. Bei ungültiger
SSID/Passphrase `wifi set` erneut beginnen. Ein Eingabefehler ändert das bisherige
Profil nicht. Die Firmware sendet das Passwort nie zurück; lokales Terminal-Echo
kann trotzdem vom Terminal erzeugt werden.

## Verhalten und weitere Tests

Join-Frist zunächst 15 s nach dem Join-Aufruf. Danach Versuche im Abstand
1/2/4/8/16/30 s, anschließend höchstens alle 30 s. Initialisierung kann zusätzliche
Zeit brauchen. `BADAUTH` wird nur bei entsprechender SDK-Meldung angezeigt;
`TIMEOUT` ist kein eindeutiger Beweis für ein falsches Passwort.

Vor jedem neuen Versuch wird der Funkchip vollständig neu initialisiert. Das
verhindert Übernahme alter Verbindungsereignisse und ist zunächst auf eindeutiges
Verhalten ausgelegt; die zusätzliche Wiederverbindungszeit wird am Pico gemessen.
USB läuft auf Core 0 weiter. `sdk_busy=1` zeigt die Initialisierung an.

Nach erfolgreichem ersten Join prüfen:

- `wifi disconnect`: Zustand `DISCONNECTED`; auch nach 30 s kein neuer Versuch.
- `wifi connect`: erneuter Linkaufbau mit unverändertem RAM-Profil.
- USB-Terminal schließen/öffnen: Verbindung und RAM-Profil bleiben erhalten,
  sofern die Stromversorgung bestehen bleibt.
- Falsches Passwort und fehlendes Netz: Fehler/Wartezustand, USB weiter bedienbar.
- Bei einem passenden Testnetz AP aus/an: Linkverlust und Wiederverbindung prüfen.
- Pico stromlos/an: `UNCONFIGURED` ist in W1/W2 **richtig**, da das RAM-Profil verloren ist.

## Grenzen der bisherigen Prüfung

C-Hosttests verwenden ASan/UBSan. Zusätzlich läuft der tatsächliche Core-1-Service
gegen einen simulierten SDK-Treiber: Scanüberlauf/Abbruch/Timeout, Mailbox-Sperre,
Löschen temporärer Passwörter, Linkverlust, Wiederholung und Init-Fehlerbehandlung.
ELF/UF2-Prüfung kontrolliert eigene Callbacks, kein lwIP/UART und Flashgrenzen.
Diese Prüfungen ersetzen keinen echten Scan/Join oder Test der USB-Latenz.

Watchdog: Hardwarefrist 8 s. Core 0 füttert nur bei eigenem Fortschritt und einem
höchstens 5 s alten Lebenszeichen von Core 1; während ausgewiesener Initialisierung
höchstens 15 s. Bei Stillstand erfolgt der Reset bis zu etwa 8 s nach dem letzten
Füttern, nicht schon exakt nach 5/15 s. Fristen und echte Watchdog-Auslösung sind
am Pico zu prüfen. Ein Watchdog-Reset verliert in W1/W2 das RAM-Profil.

Core 0 hat 4 KiB reservierten Stack, Core 1 einen eigenen 8-KiB-Stack im SRAM.
PIO/DMA werden vom SDK dynamisch belegt; `pio_sm_mask` kodiert Bit `PIO*4+SM`,
`dma_mask` Bit `Kanal`. Das sind aktuelle Claim-Masken, keine Zusage freier
Ressourcen für spätere Module. High-Water-Messungen der Stacks folgen am Muster.

M1 bleibt als separates Buildziel erhalten. SDK, CYW43 und Funkfirmware sind
festgelegt; Details stehen in `dependencies.json` und im Buildmanifest. Warnungen
über fehlendes lwIP/Bluetooth sind beim absichtlichen Build ohne diese Module
bekannt. Die elektrische M0-Abnahme bleibt Voraussetzung für aktive Amiga-Bustests.
