# FunkOtto – Ideenliste

Hier sammeln wir neue Einfälle und Erweiterungswünsche fortlaufend.
Ein Eintrag ist zunächst eine Idee; die Umsetzung wird später gesondert entschieden.
Neue Ideen erhalten eine fortlaufende ID und ein Datum. Bestehende Einträge werden
bei Bedarf ergänzt; ihr Status bleibt nachvollziehbar.

## IDEA-001 – Nachrüstbare Traffic-LED

- **Aufgenommen:** 07.10.2026
- **Status:** Vorgemerkt
- **Zeitpunkt:** Später, wenn die Grundfunktion zuverlässig läuft
- **Ziel:** Gesendete und empfangene Netzwerkpakete sichtbar anzeigen.

### Mögliche Umsetzung

Eine externe LED mit Vorwiderstand an einem freien Pico-GPIO anschließen.
Im geprüften Hardwarestand Rev B1 sind **GP27 / Modulpin 32** und
**GP28 / Modulpin 34** frei und im Trägersockel vorhanden.

Vorschlag für eine gemeinsame TX/RX-Anzeige:

- GP27 / Pin 32 über beispielsweise 1 kΩ zur Anode einer roten LED.
- LED-Kathode an GND, beispielsweise Modulpin 38.
- Nachrüstung über angelötete Leitungen; später optional eigener LED-Anschluss
  in einer neuen Boardrevision.
- Firmware steuert die Anzeige anhand der Paketaktivität.
- Kurze Ereignisse auf etwa 50 ms sichtbar verlängern, ohne blockierende
  Wartezeiten im Datenpfad; bei hoher Last die Anzeige zeitlich begrenzen.

**Variante:** Zwei getrennte LEDs für TX und RX an GP27 und GP28.

### Bei Umsetzung festzulegen

- Gemeinsame Traffic-Anzeige oder getrennte TX/RX-Anzeige.
- Welche tatsächlich übertragenen Pakete den Impuls auslösen.
- LED-Typ, Helligkeit und passender Vorwiderstand.
- Mechanischer Einbau und Anschluss.
- Erneute Prüfung, ob die GPIOs im dann aktuellen Hardware-/Firmwarestand
  weiterhin frei sind.

Die Pins sind mit diesem Eintrag noch nicht verbindlich reserviert.
Die Idee ist noch nicht in Hardware oder Firmware umgesetzt.
