# TATARUS IMAGINATIO · Standalone-Farblabor

Start unter Windows:

```text
START_IMAGINATIO_LAB.bat
```

Die Oberfläche läuft standardmäßig unter `http://127.0.0.1:8766/`. Sie nutzt
den Server des Live-Monitors und dessen C-ABI, startet den Organismus aber mit
`--imaginatio-only`. Dadurch gibt es keinen parallel fortschreitenden
Roboter-Trainingslauf.

## Farbbilder lernen

**Fotos / Bilder laden** akzeptiert die vom Browser unterstützten
Bilddateien, darunter PNG, JPEG, WebP und SVG. IMAGINATIO bildet das Motiv als
512×512-RGB24-Sinneseindruck ab. Dabei stehen formatfüllendes Zuschneiden oder
vollständiges Einpassen und drei Importprofile zur Verfügung:

- **Foto** glättet beim Skalieren;
- **Grafik** bewahrt harte Pixel- und Vektorkanten;
- **Text / Scan** erhöht den Kontrast und verwendet einen weißen Hintergrund.

Bei Mehrfachauswahl trainiert **Alle Bilder in einem Lauf lernen** jede Datei
als eigenes visuell-motorisches Engramm im selben persistenten Nervensystem.
Der Dateiname dient als Konzeptname. Jeder RGB-Pixelauftrag bleibt eine
serielle Aktion; neuronale Rückkopplung erfolgt bei dichten Bildern an
Motor-Chunk-Grenzen, damit ein kompletter Fotolauf praktikabel bleibt.
Der integrierte Labororganismus hält bis zu 64 visuelle Engramme und verwendet
für dichte Motorfolgen einen Feedback-Stride von 256. Größere Stapel weist die
Oberfläche vor dem Lernen verständlich zurück.

Galerieartefakte werden unter `imaginatio_output/gallery/` gespeichert. Ein
reiner Bildexport enthält sRGB-PNG, RGB-PPM, Luminanz-PGM, Metadaten und
Prüfsummenmanifest. Das
Forschungsarchiv enthält zusätzlich die vollständige JSONL-Aktionsspur und
einen wiederherstellbaren Snapshot von synthetischem Organismus, Nervensystem
und IMAGINATIO-Engrammen.

Das Labor lernt die farbige 512×512-RGB24-Repräsentation exemplarisch. Es kopiert beim
Abruf keine gespeicherten Zielpixel auf die Leinwand, sondern spielt das
gelernte Cursor-/Pigmentprogramm ohne sichtbare Vorlage erneut ab.

## Neue Ziel-Leinwand bis 4K

Der Größenexport verwendet kein hochskaliertes 512×512-Zwischenbild und keine
bilineare, bikubische oder andere Rasterinterpolation. TATARUS überträgt die
erinnerten Motorpositionen in normierten Koordinaten direkt auf eine frisch
angelegte Ziel-Leinwand. Dort werden richtungsabhängige Pigmentstriche gemalt.
Ihre Form folgt den im V14-Engramm abgeleiteten Region-/Stroke-, Farb- und
Kantenunterschieden; ihre feine Pigmentstruktur wird deterministisch in der
Zielauflösung synthetisiert.

Die zusätzliche Feinstruktur ist damit tatsächlich neu erzeugt, aber sie darf
nicht als Wiederherstellung unbekannter Details des Originalfotos bezeichnet
werden. Aus einem 512×512-Sinneseindruck kann kein System wissen, welche
Poren, Haare oder Fasern im ursprünglichen Motiv außerhalb dieser Information
wirklich vorhanden waren. Metadaten ab Artefaktschema v9 weisen deshalb
getrennt aus: Arbeitsauflösung, direkte Ziel-Leinwand-Ausführung,
Interpolationsfreiheit, synthetisierte Hochfrequenzdetails und deren Status als
Inferenz. Ältere Galerieexporte bleiben sichtbar und werden als Raster- oder
Interpolationsverfahren markiert.

## Biologische Bilderkennung

**Aktuelles Bild biologisch erkennen** führt die aktuelle Vorlage durch die
allgemeine IMAGINATIO-V14-Sehbahn: RGB-Photorezeptoren, retinale ON-/OFF- und
Farbopponenzzellen, Sehnerv, V1-Kanten-/Eckzellen, objektadaptive V2-Teilfelder
und ventrales Kategoriegedächtnis. Erwartete Kategorien aus dem Kategoriefeld
dürfen die vorhandene Bildevidenz nur begrenzt verstärken. Die Ergebniszeile
zeigt deshalb Bildscore und Top-down-Anteil getrennt.

Architektur und ehrliche Holdout-Messung:

- [`Docs/BIOLOGICAL_VISUAL_PATHWAY.md`](../../Docs/BIOLOGICAL_VISUAL_PATHWAY.md)
- [`Docs/IMAGINATIO_BIOLOGICAL_VISION_EVALUATION_2026-09-08.md`](../../Docs/IMAGINATIO_BIOLOGICAL_VISION_EVALUATION_2026-09-08.md)

Eine vollständige, bebilderte Anleitung für alle sieben Arbeitsmodi befindet
sich unter
[`Docs/manuals/tatarus_imaginatio_ui_bedienungsanleitung.html`](../../Docs/manuals/tatarus_imaginatio_ui_bedienungsanleitung.html).

Der aktuelle Praxistest mit 39 komplexen 512×512-Bildern ist unter
[`Docs/IMAGINATIO_512_EVALUATION_2026-09-08.md`](../../Docs/IMAGINATIO_512_EVALUATION_2026-09-08.md)
dokumentiert.

## Hybrid Cortex direkt im Farblabor

Das Farblabor besitzt jetzt rechts ein **Hybrid Cortex · Executive**-Panel. Der
Starter baut die C-ABI mit `TATARUS_BUILD_CORTEX=ON`; der Live-Monitor lädt
`config/cortex_hybrid.json` automatisch. Mit einem in LM Studio auf Port 1234
geladenen Instruct-Modell können Ziele, Beratung, semantische IMAGINATIO-Anfragen,
Executive-Pläne, Working Memory und der autonome Cortex-Zyklus direkt aus derselben
Weboberfläche bedient werden.

Während der Cortex arbeitet, bleiben weitere Cortex-Anfragen gesperrt. Der native Build ist
auf zwei parallele Compilerprozesse begrenzt. Die Standardkonfiguration erlaubt 60 Sekunden
für eine lokale Antwort und maximal 768 Ausgabetokens.

Vollständige Bedienung: [`Docs/TATARUS_CORTEX_UI.md`](../../Docs/TATARUS_CORTEX_UI.md).
