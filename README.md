# WeightTracker for Pebble Time 2

**English** · [Deutsch](#deutsch)

A lightweight weight-tracking app for the **Pebble Time 2**. Record your weight directly on your watch, browse past measurements on a smooth chart, and customise which information appears around the selected point.

This project adapts the original **WeightTracker** by **Viktor Csala** for the Pebble Time 2 (`emery`, 200 × 228 px).

## English

### Features

- **On-watch weight tracking:** add, review, edit, and delete measurements.
- **Interactive weight chart:** smooth curve, centered selection marker, and animated button tabs.
- **Two horizontal-axis modes:** evenly spaced measurements or calendar days.
- **Configurable chart range:** 4–30 previous measurements or days to the left of the selected point (subject to available history).
- **Customisable labels:** independently choose **date**, **weight**, or **BMI** above and below the selected point.
- **BMI to one decimal place**, calculated using the height entered in the phone settings.
- **Weight units:** kilograms (kg), pounds (lb), and stone (st).
- **Touch ruler for entering weight:** scroll the ruler in 0.1-unit steps.
- **On-watch history:** stores up to 100 measurements; multiple readings on the same day are supported.
- **Deliberate deletion:** hold the delete button for approximately three seconds, with a growing red indicator and a short vibration when the measurement is deleted.

### Controls

| Screen | Button / gesture | Action |
| --- | --- | --- |
| Chart | **Right top (Up)** | Select a newer measurement |
| Chart | **Left (Back)** | Select an older measurement |
| Chart | **Right middle (Select)** | Edit the selected measurement |
| Chart | **Right bottom (Down / +)** | Add a measurement |
| New measurement | **Touch ruler** | Adjust weight |
| New measurement | **Right bottom (✓)** | Save |
| Edit measurement | **Touch ruler** | Adjust weight |
| Edit measurement | **Right middle (✓)** | Save changes |
| Edit measurement | **Hold right bottom (trash icon)** | Delete after ~3 seconds; release early to cancel |

### Phone settings

Open **WeightTracker → Settings** in the Pebble companion app:

- Choose kg, lb, or st.
- Enter your height in centimetres if you want BMI labels.
- Choose **Measurements** or **Calendar days** for the chart's horizontal axis.
- Set the range to **4–30** older measurements or days.
- Set **Above measurement point** and **Below measurement point** independently to **Date**, **Weight**, or **BMI**.

The default chart labels are **weight above** and **date below**. BMI is a screening indicator, not a medical diagnosis.

### Build and install

You need a Pebble SDK with **Pebble Time 2 / `emery`** support.

```bash
git clone git@github.com:marukitano/weighttracker-Pebble-time-2.git
cd weighttracker-Pebble-time-2
pebble build
pebble install --phone PHONE_IP_ADDRESS
```

Replace `PHONE_IP_ADDRESS` with the address of your phone's Pebble connection. The project currently targets `emery` only.

### Data and export

Measurements are stored locally by the watch app. **CSV/Excel export is not currently available.** The export feature has been deferred.

### Credits and licence

Based on the original **WeightTracker** by **Viktor Csala**. Pebble Time 2 adaptation and additional interface features by **marukitano**.

- [Original WeightTracker app listing](https://apps.getpebble.com/applications/54c1918d146d67002f000096)
- Licence: [GNU GPL v3 or later](LICENSE)

---

## Deutsch

Eine schlanke App zur Gewichtskontrolle für die **Pebble Time 2**. Erfasse dein Gewicht direkt auf der Uhr, blättere durch frühere Messungen und bestimme selbst, welche Informationen oberhalb und unterhalb des ausgewählten Messpunkts angezeigt werden.

Dieses Projekt ist eine Anpassung der ursprünglichen App **WeightTracker** von **Viktor Csala** für die Pebble Time 2 (`emery`, 200 × 228 Pixel).

### Funktionen

- **Gewicht direkt auf der Uhr verwalten:** Messwerte hinzufügen, ansehen, bearbeiten und löschen.
- **Interaktives Diagramm:** geglättete Gewichtskurve, mittiger Auswahlpunkt und animierte Tastenmarkierungen.
- **Zwei Modi für die horizontale Achse:** gleichmässig verteilte Messungen oder Kalendertage.
- **Einstellbarer Diagrammbereich:** 4–30 ältere Messungen oder Tage links vom ausgewählten Punkt (soweit vorhanden).
- **Frei wählbare Beschriftung:** oberhalb und unterhalb des Punkts unabhängig **Datum**, **Gewicht** oder **BMI** anzeigen.
- **BMI mit einer Nachkommastelle**, berechnet anhand der in den Handy-Einstellungen hinterlegten Körpergrösse.
- **Gewichtseinheiten:** Kilogramm (kg), Pfund (lb) und Stone (st).
- **Touch-Lineal zur Gewichtseingabe:** Gewicht in 0,1er-Schritten einstellen.
- **Messwertarchiv auf der Uhr:** bis zu 100 Einträge; mehrere Messungen pro Tag sind möglich.
- **Schutz vor versehentlichem Löschen:** Löschtaste ungefähr drei Sekunden gedrückt halten. Die rote Markierung wächst, nach dem Löschen vibriert die Uhr kurz.

### Bedienung

| Ansicht | Taste / Geste | Funktion |
| --- | --- | --- |
| Diagramm | **Rechts oben (Up)** | Neuere Messung auswählen |
| Diagramm | **Links (Back)** | Ältere Messung auswählen |
| Diagramm | **Rechts Mitte (Select)** | Ausgewählte Messung bearbeiten |
| Diagramm | **Rechts unten (Down / +)** | Neue Messung hinzufügen |
| Neue Messung | **Touch-Lineal** | Gewicht einstellen |
| Neue Messung | **Rechts unten (✓)** | Speichern |
| Messung bearbeiten | **Touch-Lineal** | Gewicht einstellen |
| Messung bearbeiten | **Rechts Mitte (✓)** | Änderung speichern |
| Messung bearbeiten | **Rechts unten (Papierkorb) halten** | Nach etwa 3 Sekunden löschen; vorzeitiges Loslassen bricht ab |

### Einstellungen auf dem Natel

Öffne **WeightTracker → Einstellungen** in der Pebble-App:

- Gewichtseinheit kg, lb oder st auswählen.
- Körpergrösse in Zentimetern eintragen, wenn du den BMI anzeigen möchtest.
- Als horizontale Achse **Messungen** oder **Kalendertage** auswählen.
- Den Diagrammbereich auf **4–30** ältere Messungen oder Tage einstellen.
- Für **oberhalb** und **unterhalb** des Messpunkts jeweils unabhängig **Datum**, **Gewicht** oder **BMI** auswählen.

Standardmässig steht **das Gewicht oben** und **das Datum unten**. Der BMI ist ein Orientierungswert und keine medizinische Diagnose.

### Kompilieren und installieren

Benötigt wird ein Pebble-SDK mit Unterstützung für die **Pebble Time 2 / `emery`**.

```bash
git clone git@github.com:marukitano/weighttracker-Pebble-time-2.git
cd weighttracker-Pebble-time-2
pebble build
pebble install --phone PHONE_IP_ADDRESS
```

Ersetze `PHONE_IP_ADDRESS` durch die Adresse der Pebble-Verbindung auf deinem Natel. Aktuell unterstützt das Projekt als Zielplattform nur `emery`.

### Daten und Export

Die Messwerte werden lokal von der App auf der Uhr gespeichert. Ein **CSV-/Excel-Export ist derzeit nicht verfügbar** und wurde vorerst zurückgestellt.

### Credits und Lizenz

Basierend auf **WeightTracker** von **Viktor Csala**. Anpassung an die Pebble Time 2 und zusätzliche Bedienoberfläche von **marukitano**.

- [Ursprünglicher WeightTracker-Eintrag](https://apps.getpebble.com/applications/54c1918d146d67002f000096)
- Lizenz: [GNU GPL v3 oder neuer](LICENSE)
