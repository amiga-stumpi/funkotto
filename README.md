# FunkOtto

WLAN-Adapter für den Amiga-Parallelport auf Basis des Raspberry Pi Pico 2 W.
Ziel sind eine eigene Firmware, ein SANA-II-Treiber für 68000/AmigaOS 1.3 und
ein Amiga-Konfigurationsprogramm.

## Aktueller Stand

Das Repository enthält zunächst den [Entwicklungsplan für Firmware v0.1](docs/firmware-v0.1-plan.md).
Es gibt noch keine funktionsfähige Firmware oder Treiber-Binaries.

Die erste Firmware soll Parallelport-Kommunikation, WLAN-Rohpakete und ein
dauerhaft gespeichertes WLAN-Profil mit automatischer Wiederverbindung bieten.
Firmwareinstallation und Updates erfolgen vorerst über USB/BOOTSEL am Pico.
Firmwareupdates über den Amiga sind zurückgestellt.

Die Hardwarebasis ist der separate KiCad-Entwurf `AmiWiFi_Pico2W_RevA`.
Der Plan enthält einen offenen Prüfpunkt zum sicheren Wiederanlauf der
Bustreiber nach Amiga-Reset. Dieser muss vor aktiven Bustests geklärt werden.
