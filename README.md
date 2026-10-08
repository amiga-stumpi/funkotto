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
Es gibt noch keine funktionsfähige Firmware oder Treiber-Binaries.

Die erste Firmware soll Parallelport-Kommunikation, WLAN-Rohpakete und ein
dauerhaft gespeichertes WLAN-Profil mit automatischer Wiederverbindung bieten.
Firmwareinstallation und Updates erfolgen vorerst über USB/BOOTSEL am Pico.
Firmwareupdates über den Amiga sind zurückgestellt.

Die Hardwarebasis ist der Nutzerstand vom 08.10.2026 unter [hardware/](hardware/),
auf Grundlage von Rev B2 und der zuvor bearbeiteten Rev-A-Datei. Ein zusätzlicher
Open-Drain-Resetpfad setzt den Pico bei Amiga-Reset oder fehlender Hostversorgung
über RUN zurück. Nach Reset startet die Firmware neu und verbindet WLAN erneut;
das gespeicherte Profil bleibt erhalten.

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
