# FunkOtto

WLAN-Adapter für den Amiga-Parallelport auf Basis des Raspberry Pi Pico 2 W.
Ziel sind eine eigene Firmware, ein SANA-II-Treiber für 68000/AmigaOS 1.3 und
ein Amiga-Konfigurationsprogramm.

## Aktueller Stand

[Fortschrittsbericht vom 06.10.2026](docs/results/2026-10-06-fortschritt.md):
Hardware Rev B1, native DRC-/ERC-Prüfberichte, Reset- und Versorgungsdetails
sowie nächste Umsetzungsschritte.

Das Repository enthält zunächst den [Entwicklungsplan für Firmware v0.1](docs/firmware-v0.1-plan.md).
Es gibt noch keine funktionsfähige Firmware oder Treiber-Binaries.

Die erste Firmware soll Parallelport-Kommunikation, WLAN-Rohpakete und ein
dauerhaft gespeichertes WLAN-Profil mit automatischer Wiederverbindung bieten.
Firmwareinstallation und Updates erfolgen vorerst über USB/BOOTSEL am Pico.
Firmwareupdates über den Amiga sind zurückgestellt.

Die Hardwarebasis ist der separate KiCad-10-Entwurf `FunkOtto_Pico2W_RevB1`,
auf Basis der vom Nutzer bearbeiteten Rev-A-Datei. Ein zusätzlicher
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
Kupferrouting bleiben unverändert. Die frühere Rev-B-Prüfung war hinsichtlich
des nativen Schaltplanabgleichs unvollständig; Details und aktuelle Hardware-
Prüfsummen stehen im Firmwareplan.
