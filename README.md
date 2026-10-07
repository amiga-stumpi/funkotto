# FunkOtto

WLAN-Adapter für den Amiga-Parallelport auf Basis des Raspberry Pi Pico 2 W.
Ziel sind eine eigene Firmware, ein SANA-II-Treiber für 68000/AmigaOS 1.3 und
ein Amiga-Konfigurationsprogramm.

## Aktueller Stand

[Hardwarestand vom 07.10.2026](docs/results/2026-10-07-revb2.md):
Rev B2 mit U8 SN74LVC2G07DCKR (SC70-6), angepasstem Lötbild und
nativen DRC-/ERC-Prüfberichten. [Bisheriger Fortschritt](docs/results/2026-10-06-fortschritt.md).

Das Repository enthält zunächst den [Entwicklungsplan für Firmware v0.1](docs/firmware-v0.1-plan.md).
Es gibt noch keine funktionsfähige Firmware oder Treiber-Binaries.

Die erste Firmware soll Parallelport-Kommunikation, WLAN-Rohpakete und ein
dauerhaft gespeichertes WLAN-Profil mit automatischer Wiederverbindung bieten.
Firmwareinstallation und Updates erfolgen vorerst über USB/BOOTSEL am Pico.
Firmwareupdates über den Amiga sind zurückgestellt.

Die Hardwarebasis ist der separate KiCad-10-Entwurf `FunkOtto_Pico2W_RevB2`,
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
Kupferrouting blieben bei B1 unverändert. B2 ändert U8 auf DCKR/SC70-6 und
passt ausschließlich sein lokales Routing an; DRC und ERC bleiben fehlerfrei. Die frühere Rev-B-Prüfung war hinsichtlich
des nativen Schaltplanabgleichs unvollständig; Details und aktuelle Hardware-
Prüfsummen stehen im Firmwareplan.

## Ideen und spätere Erweiterungen

Die [Ideenliste](docs/ideenliste.md) sammelt fortlaufend Erweiterungswünsche.
Erster Eintrag: eine später nachrüstbare Traffic-LED, optional getrennt für TX/RX.

