# FunkOtto

WLAN-Adapter für den Amiga-Parallelport auf Basis des Raspberry Pi Pico 2 W.
Ziel sind eine eigene Firmware, ein SANA-II-Treiber für 68000/AmigaOS 1.3 und
ein Amiga-Konfigurationsprogramm.

## Aktueller Stand

**Aktuelle Hardwarebasis seit 08.10.2026:** Die KiCad-Dateien unter
[hardware/](hardware/) entsprechen bytegenau dem vom Nutzer bearbeiteten
`AmiWiFi_Pico2W.zip`. Dieser Stand ersetzt das zuvor importierte Rev-B2-Board.
[Importnachweis und Prüfsummen](docs/results/2026-10-08-nutzer-kicad-import.md).

Die vorhandenen Vorschauen, Bestückungsdaten und DRC-/ERC-Berichte beziehen sich
auf den vorherigen Rev-B2-Stand. Für diesen Dateiimport wurden sie nicht neu
erzeugt; die frühere DRC-Freigängigkeit ist damit kein Prüfnachweis für das
geänderte Board.

[Vorheriger Hardwarestand vom 07.10.2026](docs/results/2026-10-07-revb2.md):
Rev B2 mit U8 SN74LVC2G07DCKR (SC70-6), angepasstem Lötbild und
nativen DRC-/ERC-Prüfberichten. [Bisheriger Fortschritt](docs/results/2026-10-06-fortschritt.md).

Das Repository enthält den [Entwicklungsplan für Firmware v0.1](docs/firmware-v0.1-plan.md)
und das vollständige KiCad-Projekt mit lokalen Bibliotheken, Stückliste und Prüfberichten.
**Die erste Pico-Firmware M1 ist implementiert und als UF2 gebaut:** sichere
GPIO-Zustände, USB-Diagnose (`help`, `info`, `status`) und Watchdog.
[Build- und Testanleitung](firmware/README.md) ·
[Prüfergebnisse M1](docs/results/2026-10-08-firmware-m1.md).
**W1/W2 ist jetzt als WLAN-Testfirmware implementiert:** Scan, RAM-Konfiguration,
WPA2-Verbindung und Wiederverbindung. [Bedienung und Testablauf](firmware/WLAN_TEST.md).
Scan und erster WPA2-Join wurden am 08.10.2026 auf dem Pico bestätigt
(`LINK_UP`, erster Versuch, Join 2679 ms). Manuelles Wiederverbinden und automatische Erholung
nach AP-Ausfall sind ebenfalls bestätigt. Dauerlauf und weitere Fehlerfälle
bleiben offen. **W3 ist implementiert:** Ethernet-Rohtransport mit festen Puffern,
USB-Testprotokoll und PC-Werkzeug für ARP/ICMP. [W3-Testanleitung](firmware/W3_TEST.md) ·
[W3-Prüfstand](docs/results/2026-10-09-w3.md). Die realen ARP/ICMP-Tests sind bestanden:
12/12 und anschließend 1.000/1.000 Echo-Antworten einschließlich MTU 1500,
keine Verluste. Der 1.000er-Lauf ist anhand von JSON und PCAP geprüft.
AP-Ausfall, Wiederverbindung und erneuter Pakettest sind vom Nutzer bestätigt.
Langzeitbetrieb und die Ursache einzelner Latenzausreißer bleiben offen.
**W4 ist implementiert:** dauerhaftes Profil, `wifi save`/`wifi erase` und
Autoconnect nach Stromverlust. [W4-Anleitung](firmware/W4_TEST.md) ·
[W4-Prüfstand](docs/results/2026-10-09-w4.md). Ein erster realer Status bestätigt
das gespeicherte Profil und `LINK_UP`; Kaltstart und Löschtest bleiben zu bestätigen.
Parallelport-Kommunikation und Amiga-Treiber folgen.
USB-Diagnose und Kaltstart ohne geöffnetes Terminal wurden am 08.10.2026 vom
Nutzer auf einem realen Pico bestätigt. Die elektrische M0-Abnahme bleibt offen.
Nächster Schritt: [WLAN-Entwicklungsplan](docs/wlan-entwicklungsplan.md).

Die erste Firmware soll Parallelport-Kommunikation, WLAN-Rohpakete und ein
dauerhaft gespeichertes WLAN-Profil mit automatischer Wiederverbindung bieten.
Firmwareinstallation und Updates erfolgen vorerst über USB/BOOTSEL am Pico.
Firmwareupdates über den Amiga sind zurückgestellt.

Die Hardwarebasis ist der Nutzerstand vom 08.10.2026 unter [hardware/](hardware/),
auf Grundlage von Rev B2 und der zuvor bearbeiteten Rev-A-Datei. Ein zusätzlicher
Open-Drain-Resetpfad setzt den Pico bei Amiga-Reset oder fehlender Hostversorgung
über RUN zurück. Für v0.1 ist vorgesehen: Nach Reset startet die Firmware neu und verbindet WLAN
erneut; das gespeicherte Profil bleibt erhalten. M1 enthält diese WLAN-Funktion noch nicht.

Für USB/BOOTSEL im Sockel DB25 und USB-C-Trägerversorgung trennen und den
eigenen USB-Anschluss des Pico verwenden; alternativ den Pico herausnehmen.

Die Resetvariante ist in Schaltplan und Board umgesetzt. Pulsbreiten, Pegel,
Versorgungsfolgen und sichere Bustreiberfreigabe müssen vor aktiven Bustests
am Muster gemessen werden (M0 im Plan). Es gibt noch keine Fertigungsfreigabe.


Rev B1 bereinigt die KiCad-Netznamen, Schaltplan-/Footprintattribute,
Bibliothekskopien und Beschriftungen. DRC **mit Schaltplanvergleich** sowie ERC
melden in KiCad 10.0.6 jeweils 0 Fehler und 0 Warnungen. Bauteilpositionen und
Kupferrouting blieben bei B1 unverändert. B2 ändert U8 auf DCKR/SC70-6 und
passte ausschließlich sein lokales Routing an; die damaligen DRC- und ERC-Läufe waren fehlerfrei. Die frühere Rev-B-Prüfung war hinsichtlich
des nativen Schaltplanabgleichs unvollständig; Details und damalige Hardware-
Prüfsummen stehen im Firmwareplan. Aktuelle Prüfsummen stehen im oben verlinkten Importnachweis.

## Ideen und spätere Erweiterungen

Die [Ideenliste](docs/ideenliste.md) sammelt fortlaufend Erweiterungswünsche.
Erster Eintrag: eine später nachrüstbare Traffic-LED, optional getrennt für TX/RX.


## KiCad-Projekt öffnen

Repository herunterladen und `hardware/AmiWiFi.kicad_pro` mit KiCad 10 öffnen.
Die Bibliotheken in `hardware/` gehören zum Projekt.

- [Hinweise zum Hardwarestand Rev B2](docs/KICAD_REV_B2.txt)
- [Stückliste](docs/BOM.csv) und [Pinbelegung](docs/PINOUT.csv)
- [Vorschauen](preview/) und [Prüfberichte](validation/)

Am 08.10.2026 durch die KiCad-Dateien aus dem Nutzerarchiv ersetzt.
Die Dateien wurden beim Import nicht bearbeitet. Elektrische Musterprüfung
und Fertigungsfreigabe bleiben offen.
