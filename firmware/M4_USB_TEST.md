# M4: WLAN-Konfiguration über USB testen

Dieses Paket ist für den **einzelnen Pico 2 W am PC**, ohne Trägerplatine und
Amiga-Verbindung. Ziel `funkotto_m4_usb`, Version `0.1.0-m4-usb`. Alle
Parallelportleitungen bleiben im sicheren Zustand. W4-Profilformat und
Flashreservierung sind unverändert; die bisherige Textkonsole und der
Rohpakettest `wifi_diag.py` bleiben verfügbar. Keine Amiga-Firmwareupdates.

## Installation

Pico mit BOOTSEL an USB anschließen und `funkotto_m4_usb.uf2` kopieren.
Kein Flash-Nuke/Full-Erase verwenden. Die UF2 lässt die Profilsektoren aus;
der reale Profilerhalt beim Update ist Teil der Prüfung. Terminalprogramme
schließen, bevor das Python-Werkzeug den COM-Port öffnet.

Python 3.10 oder neuer, einmalig:

```text
python -m pip install pyserial==3.5
```

`tools/wifi_config.py` und `tools/wifi_diag.py` müssen zusammen im Ordner bleiben.
Die Beispiele verwenden den bisherigen COM6; bei Bedarf ändern.

## 1. Erst Status lesen (ohne Profiländerung)

```text
python tools/wifi_config.py --port COM6 --report m4-status.json status
```

Erwartet: `protocol=FOC1`, Version 1.0, Fähigkeiten 63 und ein Statusobjekt.
`passed=true` bedeutet erfolgreicher Befehlslauf, nicht automatisch WLAN-Link.
Bei erhaltenem W4-Profil sind `configured`, `stored`, `flash_profile` true;
Verbindungsaufbau kann noch laufen. Erneut `status` aufrufen und LINK_UP prüfen.
Bitte diese erste Ausgabe zurückmelden, bevor das Profil geändert wird.

## 2. Scan (unterbricht die Verbindung erst durch ausdrückliches Disconnect)

```text
python tools/wifi_config.py --port COM6 disconnect
python tools/wifi_config.py --port COM6 --report m4-scan.json scan
python tools/wifi_config.py --port COM6 connect
python tools/wifi_config.py --port COM6 status
```

SCAN darf bei aktivem Auto-Reconnect mit BUSY enden. Das Tool trennt daher nicht
heimlich die Verbindung. Scan liefert Generation, SSID, Kanal, RSSI und Authwert.
CONNECT/OK heißt, dass der Auftrag ausgeführt wurde; erst STATUS=LINK_UP belegt
eine WLAN-Verbindung. Falsches Passwort/fehlender AP bleiben in STATUS sichtbar.

## 3. Optional: neues RAM-Profil setzen und testen

```text
python tools/wifi_config.py --port COM6 set --ssid "Linux"
python tools/wifi_config.py --port COM6 connect
python tools/wifi_config.py --port COM6 status
```

Das Passwort wird interaktiv verdeckt abgefragt. Kein Passwortargument, keine
Passwortausgabe im JSON. Alternativ `set --ssid-hex 4c696e7578`; ohne SSID-Option
fragt das Tool auch die SSID ab. Text-SSID wird UTF-8-kodiert, maximal 32 Bytes.
SET trennt die bisherige Verbindung und verändert nur RAM. Das bisherige
Flashprofil bleibt bis SAVE/ERASE erhalten. Kein automatisches Speichern.

## 4. Bewusst speichern und Kaltstart prüfen

Nach erfolgreichem LINK_UP:

```text
python tools/wifi_config.py --port COM6 save
python tools/wifi_config.py --port COM6 status
```

Erwartet: erfolgreicher Abschluss, `stored=true`, `flash_profile=true`,
`storage_error=0`. SAVE kann WLAN kurz trennen; auf erneutes LINK_UP warten.
Danach Pico vollständig stromlos machen, wieder verbinden und STATUS lesen.
Automatischer Reconnect ohne erneute Eingabe wird getrennt als Hardwaretest
protokolliert, auch wenn er bereits mit der früheren W4-Version bestanden war.

## 5. Optional: absichtlich löschen

Entfernt die WLAN-Zugangsdaten und beendet die Verbindung:

```text
python tools/wifi_config.py --port COM6 erase --confirm
python tools/wifi_config.py --port COM6 status
```

Erwartet: UNCONFIGURED, `stored=false`, `flash_profile=false`; nach Kaltstart
weiterhin unkonfiguriert. Anschließend bei Bedarf SET/CONNECT/SAVE wiederholen.

## Fehler und Wiederholungen

Das Tool öffnet `config on` selbst, prüft HELLO/Fähigkeiten und schließt den Modus
über DTR. Es hält keine dauerhafte Verbindung und benötigt keine Terminaleingabe.
Nur identische USB-Anfragen werden automatisch wiederholt; die Firmware liefert
die gespeicherte Antwort. Bei Pico-Reset oder endgültigem Timeout ist das Ergebnis
eines Änderungsauftrags möglicherweise unbekannt. Keine automatische Wiederholung
in einer neuen Sitzung: zuerst STATUS lesen. Beim Abbruch kann ein bereits
angenommener Speicherauftrag trotzdem erfolgreich zu Ende laufen.

Eine Sitzung endet auch nach 60 s ohne Eingabe. Nach einem Verbindungsabbruch kurz
warten, bis ein laufender Auftrag abgeschlossen ist. Keine echten Passwörter in
USB-Mitschnitten aufzeichnen. USB-Verkehr und Flashprofil sind nicht verschlüsselt.

## Build und Grenzen

```sh
python tools/bootstrap_sdk.py
cmake -S firmware -B build/m4 -G Ninja -DPICO_SDK_PATH="$PWD/.deps/pico-sdk" -DCMAKE_BUILD_TYPE=Release -DFUNKOTTO_SOURCE_ID=development
cmake --build build/m4 --target funkotto_m4_usb
python tools/check_firmware_artifacts.py build/m4 --target funkotto_m4_usb
```

Festgelegte Buildwerkzeuge: `firmware/dependencies.json`. Protokoll:
`protocol/CONFIG.md`. Kein Amiga-Konfigurationsclient, keine SANA-II-Funktion,
keine M0-/M2-Hardwarefreigabe und keine vollständige M4-Abnahme in diesem Paket.
