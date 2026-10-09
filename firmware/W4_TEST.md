# FunkOtto W4 – WLAN-Profil speichern und automatisch verbinden

Firmware `0.1.0-w4`, Ziel `funkotto_w4`, Pico 2 W. W4 ergänzt den geprüften
W3-Rohdatenpfad um ein dauerhaftes WLAN-Profil. Ein erster realer Status bestätigt
das gespeicherte Profil und `LINK_UP`; Kaltstart und Löschtest sind noch offen
([Prüfstand](../docs/results/2026-10-09-w4.md)). Der Amiga-Bus bleibt gesperrt.

## 1. Flashen und einmalig konfigurieren

Pico herausnehmen oder **DB25 und USB-C-Trägerversorgung abziehen**, bevor sein
eigener USB-Port verwendet wird. BOOTSEL halten, Pico am PC anschließen und
`funkotto_w4.uf2` auf `RP2350` kopieren. Beim ersten Wechsel von W3 geht dessen
bisheriges RAM-Profil verloren; SSID und Passwort einmal neu eingeben.

Terminal öffnen (115200, keine Flusssteuerung, lokales Echo aus):

```text
info
status
wifi set
```

SSID und Passwort bei den jeweiligen Aufforderungen eingeben. Dann:

```text
wifi connect
wifi status
```

Auf `wifi=LINK_UP` warten. `status` muss `bus_locked=1` und
`output_mask=0x00700000` anzeigen. Jetzt ausdrücklich speichern:

```text
wifi save
```

`QUEUED` bedeutet nur angenommen. Auf die nachfolgende Meldung
`wifi command=... result=OK` warten. WLAN wird zum Speichern angehalten und bei
vorher aktivem Verbindungswunsch anschließend neu verbunden. USB kann während
der einzelnen Flashoperationen kurz pausieren. Danach `wifi status` prüfen:

```text
configured=1 stored=1 auto_retry=1
storage=PROFILE storage_error=0 flash_profile=1 flash_ready=1
```

Der Link kann unmittelbar nach dem Speichern noch `CONNECTING` sein;
anschließend muss er wieder `LINK_UP` erreichen. Ein zweites `wifi save` mit
identischen Daten schreibt den Flash nicht erneut, unterbricht in dieser
Version aber ebenfalls kurz den WLAN-Dienst.

## 2. Kaltstart ohne Terminal

1. Terminal schließen, Pico vollständig stromlos machen und wieder anschließen.
2. Zunächst kein Terminal öffnen; die Firmware darf darauf nicht warten.
3. Nach einigen Sekunden Terminal öffnen und `info`, `status`, `wifi status`
   und `wifi stats` ausführen.
4. Erwartet: neue kurze Uptime, gespeichertes Profil, `stored=1`,
   `auto_retry=1` und automatisch `LINK_UP`, ohne `wifi set`/`wifi connect`.
5. Terminal schließen, USB angeschlossen lassen und den bekannten Rohdatentest
   erneut ausführen; zum Beispiel mit den bisher verwendeten LAN-Adressen:

```text
python tools/wifi_diag.py --port COM6 --source-ip 192.168.25.234/24 --target-ip 192.168.25.1 --count 1000 --pcap w4-1000.pcap --report w4-1000.json
```

Port und Adressen bei geändertem Aufbau anpassen; die Test-IP muss weiterhin
frei sein. Das Werkzeug bleibt der W3-Protokolltest und nennt deshalb im JSON
`stage=W3`, auch wenn die Firmware W4 ist. `info` identifiziert den Firmwarestand.

Die Kaltstartprüfung zunächst dreimal wiederholen und die Statusausgaben
zurückmelden. Für die weitergehende Abnahme sind 100 Kaltstarts und anschließend
Tests mit fehlendem AP sowie AP aus/an vorgesehen. Ein fehlender AP soll zu
begrenzten Wiederholungen führen; USB-Konfiguration bleibt erreichbar.

## 3. Bedeutung von RAM-Profil und gespeichertem Profil

| Befehl / Status | Verhalten |
| --- | --- |
| `wifi set` / `wifi sethex` | Ändert nur das RAM-Profil und trennt WLAN. Kein automatisches Speichern. |
| `wifi save` | Speichert das aktuelle gültige RAM-Profil; dieses verbindet beim nächsten Start automatisch. |
| `wifi disconnect` | Stoppt die Verbindung für den laufenden Betrieb. Das Flashprofil bleibt für den nächsten Start erhalten. |
| `stored=1` | Das aktuelle RAM-Profil entspricht dem gespeicherten Profil. |
| `stored=0 flash_profile=1` | Ein Flashprofil existiert, entspricht aber nicht dem aktuellen RAM-Profil. Ein Neustart lädt den Flashstand. |
| `flash_ready=1` | SDK-Sicherheitsinitialisierung erfolgreich; DMA-/Busruhe und Kernfreigabe werden vor jedem Schreiben zusätzlich geprüft. |
| `storage_seq` | Sequenznummer des ausgewählten Flashdatensatzes. |

`wifi save` ohne RAM-Profil liefert `NO_PROFILE`. Während eines Scans liefern
Speichern/Löschen `BUSY`; Scan beenden oder mit `wifi disconnect` abbrechen.
Bei laufendem Befehl startet `raw on` keine neue binäre Testsitzung.
Zugangsdaten stehen unverschlüsselt im Flash; CRC schützt vor beschädigten
Datensätzen, nicht vor Auslesen. Passwort und Flashinhalt werden nicht ausgegeben.

## 4. Profil löschen

Im normalen Terminal:

```text
wifi erase
```

Auf `result=OK` warten. Das löscht RAM-Profil und Verbindungswunsch, schreibt
zuerst einen Löschdatensatz und entfernt danach den alten Sektorinhalt.
Erwartet: `UNCONFIGURED`, `configured=0 stored=0 auto_retry=0`,
`storage=DELETED storage_error=0 flash_profile=0`. Auf einem bereits vollständig
leeren Flash darf `storage=EMPTY` bleiben. Nach Strom aus/an weiterhin kein
Profil und kein automatischer Verbindungsaufbau. Anschließend wieder per
`wifi set`, `wifi connect`, `wifi save` einrichten.

Eine Unterbrechung **vor** dem gültigen Commit kann den vorherigen Stand erhalten;
ein nicht bestätigtes Löschen darf nicht als abgeschlossen gelten. Ist der
Löschdatensatz gültig gespeichert, wird das alte Profil nicht mehr verwendet.
Eine unterbrochene Bereinigung wird beim nächsten Start vor WLAN-Nutzung
fortgesetzt. `result=OK` wird erst nach erfolgreicher Bereinigung ausgegeben.
Das ist logisches Löschen und Sektorlöschen, kein forensisch zertifiziertes Löschen.

## 5. Fehlerstatus und Updates

| `storage` | Bedeutung |
| --- | --- |
| `EMPTY` | Beide Sektoren gelöscht; kein Profil. |
| `PROFILE` | Gültiges Profil vorhanden. |
| `DELETED` | Gültiger Löschdatensatz; kein Autoconnect. Bei Fehlerstatus ist Bereinigung eventuell noch offen. |
| `CORRUPT` | Kein gültiger Datensatz, aber Flash nicht leer; kein Autoconnect. Explizit neu speichern oder löschen. |
| `INCOMPATIBLE` | Neuester gültiger Datensatz hat unbekanntes Format; kein Autoconnect und kein Überschreiben durch `wifi save`. `wifi erase` verwirft ihn ausdrücklich. |
| `AMBIGUOUS` | Sequenzen nicht eindeutig; kein Autoconnect. Explizit `wifi erase` ausführen und neu konfigurieren. |

`storage_error`: 0 OK, 1 Flash-/SDK-Aufruf fehlgeschlagen, 2 Rückleseprüfung
fehlgeschlagen, 3 ungültige Eingabe/Format, 4 sichere Flashfreigabe nicht verfügbar.
Bei `FLASH_ERROR` den Vorgang nicht als erfolgreich betrachten: Ein alter oder
bereits neu geschriebener Stand kann erhalten sein. Status prüfen und gezielt
wiederholen; nicht wiederholt neu flashen. Ein bestätigtes Löschen darf keinen
alten WLAN-Schlüssel nach dem Neustart wieder aktivieren.

Normale **FunkOtto-UF2-Updates** schreiben nicht in die Profilsektoren. Das
Buildwerkzeug prüft jeden UF2-Block. Ein vollständiges Flash-Erase oder fremde
Firmware kann Profile trotzdem löschen. W3/M1 können das W4-Profil nicht nutzen,
berühren seine reservierten Sektoren bei unseren normalen UF2-Dateien aber nicht.

[Speicherformat und Sicherungsablauf](PROFILE_FORMAT.md) ·
[W3-Pakettest](W3_TEST.md) · [W4-Prüfbericht](../docs/results/2026-10-09-w4.md)
