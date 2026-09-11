# TATARUS – Wie ein synthetisches Nervensystem ein Bild vergrößert

## Podcast-Fassung / technische Einordnung

**Stand:** 09. September 2026  
**Projekt:** TATARUS / IMAGINATIO  
**Thema:** Hochauflösende Bildrekonstruktion aus visueller Erinnerung und Motorik

---

## Kurzfassung für den Einstieg

TATARUS vergrößert ein Bild nicht auf klassische Weise.

Es nimmt nicht einfach ein kleines Rasterbild und berechnet zusätzliche Zwischenpixel mit Bicubic, Lanczos oder einer ähnlichen Interpolation.

Stattdessen wird das Bild zunächst von einem synthetischen, biologisch inspirierten Nervensystem wahrgenommen. Dabei entstehen neuronale Repräsentationen, visuelle Engramme und eine motorische Erinnerung daran, wie diese Wahrnehmung reproduziert werden kann.

Wenn TATARUS später eine größere Ausgabe erzeugen soll, wird nicht das kleine Bild hochgerechnet.

Der Raum wird größer.

Die gespeicherte visuelle Motorik wird auf einer neuen, größeren Leinwand erneut ausgeführt.

TATARUS vergrößert also nicht die Pixel seiner Erinnerung.

**TATARUS vergrößert den Raum, in dem es diese Erinnerung erneut ausdrückt.**

---

# 1. Warum dieser Unterschied wichtig ist

Bei einer klassischen Bildvergrößerung ist die Ausgangslage einfach:

```text
512 × 512 Pixel
        ↓
mathematische Interpolation
        ↓
3840 × 3840 Pixel
```

Das Verfahren kennt kein Gedächtnis.

Es kennt keine Wahrnehmung.

Es kennt keine motorische Handlung.

Es betrachtet lediglich vorhandene Bildpunkte und berechnet daraus neue Bildpunkte.

TATARUS arbeitet anders.

Die Verarbeitungskette lautet vereinfacht:

```text
visueller Reiz
      ↓
Wahrnehmung
      ↓
neuronale Repräsentation
      ↓
visuelles Engramm
      ↓
motorische Erinnerung
      ↓
erneute Ausführung auf größerer Leinwand
      ↓
hochaufgelöste Rekonstruktion
```

Damit ist die Vergrößerung nicht nur ein Bildverarbeitungsproblem.

Sie ist das Ergebnis eines Gedächtnisabrufs und einer erneuten motorischen Ausführung.

---

# 2. Was TATARUS beim Sehen eines Bildes macht

Ein Bild gelangt zunächst als visueller Reiz in das System.

Die aktuelle interne Arbeitsrepräsentation kann beispielsweise 512 × 512 Bildpunkte umfassen.

Diese 512 × 512 Pixel sind jedoch nicht einfach nur eine Datei, die später vergrößert werden soll.

Sie bilden den sensorischen Erfahrungsraum des visuellen Systems.

TATARUS verarbeitet dabei unter anderem:

- räumliche Anordnung,
- Farben und Pigmente,
- Helligkeitsverläufe,
- lokale Kanten,
- lokale Farbunterschiede,
- Oberflächenvariation,
- visuelle Struktur.

Das Entscheidende ist:

TATARUS speichert nicht nur, **wie das Bild aussieht**.

Das System lernt zusätzlich eine motorische Repräsentation davon, **wie diese Wahrnehmung reproduziert werden kann**.

---

# 3. Aus Wahrnehmung wird Erinnerung

Während des Lernens werden mehrere Ebenen miteinander verbunden:

```text
Wahrnehmung
   ↓
neuronale Aktivität
   ↓
Assemblies
   ↓
visuelles Engramm
   ↓
Motorprogramm
```

Ein visuelles Engramm ist dabei vereinfacht eine persistente Gedächtnisspur.

Die Motorik enthält eine Folge von Malereignissen.

Diese Ereignisse können Informationen tragen über:

- Position,
- Pigmentfarbe,
- Intensität,
- Patch-Struktur,
- lokale räumliche Veränderung.

Damit erinnert sich TATARUS nicht nur an ein fertiges Resultat.

Das System besitzt eine gelernte Handlungsbeschreibung.

Vereinfacht gesagt:

> TATARUS speichert nicht nur: „So sah es aus.“

Sondern zusätzlich:

> „So kann ich es wieder erzeugen.“

---

# 4. Was beim Erinnern geschieht

Soll TATARUS ein bekanntes Bild erneut erzeugen, wird das passende visuelle Engramm reaktiviert.

Der Ablauf kann vereinfacht so dargestellt werden:

```text
Hinweisreiz
   ↓
Wiedererkennung
   ↓
passendes Engramm
   ↓
Reaktivierung des Motorprogramms
   ↓
erneutes Zeichnen
```

Genau an dieser Stelle unterscheidet sich TATARUS von einem klassischen Upscaler.

Ein Upscaler benötigt kein Gedächtnis.

TATARUS benötigt die zuvor gelernte visuell-motorische Erfahrung.

---

# 5. Was sich bei einer Vergrößerung ändert

Nehmen wir an, die ursprüngliche Wahrnehmung wurde in einem 512 × 512 großen sensorischen Arbeitsraum verarbeitet.

Nun soll TATARUS dieselbe Erinnerung auf einer 3840 × 3840 großen Fläche ausdrücken.

Der alte Ansatz wäre:

```text
512 × 512 Bild
      ↓
hochrechnen
      ↓
3840 × 3840 Bild
```

Der aktuelle TATARUS-Ansatz ist dagegen:

```text
512 × 512 Wahrnehmung
      ↓
gespeicherte visuelle Motorik
      ↓
neuer Zielraum 3840 × 3840
      ↓
Motorik direkt auf Zielraum ausführen
      ↓
3840 × 3840 Rekonstruktion
```

Das kleine 512er Bild wird dabei nicht als fertige Rasterquelle benutzt.

Stattdessen wird die erinnerte Handlung auf einen größeren motorischen Raum übertragen.

---

# 6. Die große Leinwand existiert von Anfang an

Wird beispielsweise 3840 × 3840 angefordert, erzeugt TATARUS direkt eine Zieloberfläche dieser Größe.

Das bedeutet:

```text
3840 × 3840
= 14.745.600 Bildpunkte
```

Bei RGB8 sind das:

```text
14.745.600 × 3
= 44.236.800 Farbbytes
```

Der hochauflösende Zielraum ist also nicht das Endprodukt einer nachträglichen Skalierung.

Er existiert bereits, bevor TATARUS beginnt, die Erinnerung dort auszudrücken.

---

# 7. Die Motorik wird in Zielkoordinaten übertragen

Die gespeicherten Malereignisse stammen aus dem gelernten visuellen Raum.

Für eine größere Ausgabe werden ihre Positionen in den größeren Zielraum transformiert.

Bei einem quadratischen Beispiel:

```text
512 → 3840
```

ergibt sich ein räumlicher Faktor von:

```text
3840 / 512 = 7,5
```

Entscheidend ist aber:

Dieser Faktor dient dazu, die Position und räumliche Ausdehnung einer motorischen Handlung in den größeren Raum zu übertragen.

Er dient nicht dazu, aus einem fertigen 512er Raster neue Pixel zu interpolieren.

Das ist ein fundamentaler Unterschied.

---

# 8. TATARUS malt im Zielraum

Die eigentliche Ausgabe entsteht über lokale Maloperationen.

Im aktuellen Renderer werden diese als Target-Space-Painting beziehungsweise Target Dabs ausgeführt.

Für eine solche Maloperation werden unter anderem bestimmt:

- Zielposition,
- Ausdehnung,
- Orientierung,
- Pigment,
- Intensität,
- lokale Kantenrichtung,
- lokale Farbvariation,
- Texturintensität.

Dadurch kann TATARUS unterschiedliche Regionen unterschiedlich behandeln.

Eine homogene Fläche besitzt andere Eigenschaften als eine harte Metallkante.

Eine Nebelfläche besitzt andere Eigenschaften als eine strukturierte Rüstung.

Eine Baumrinde besitzt andere lokale Variationen als ein weicher Hintergrund.

---

# 9. Wie TATARUS Kanten berücksichtigt

Innerhalb der gespeicherten Pigment-Patches werden lokale Helligkeitsunterschiede untersucht.

Daraus entstehen unter anderem horizontale und vertikale Gradienten.

Aus diesen Gradienten kann TATARUS ableiten:

- wie stark eine Kante ausgeprägt ist,
- in welche Richtung sie verläuft.

Die Maloperation im Zielraum kann dadurch anisotrop ausgerichtet werden.

Vereinfacht:

```text
gleichmäßige Fläche
→ weicher, eher symmetrischer Pigmentauftrag

deutliche Kante
→ gerichteter Pigmentauftrag entlang der Struktur
```

Damit wird die Zielraum-Rekonstruktion durch das beeinflusst, was TATARUS zuvor wahrgenommen hat.

---

# 10. Woher die zusätzlichen Details kommen

Bei einer Vergrößerung entstehen zusätzliche Bildpunkte.

Diese neuen Bildpunkte können nicht einfach die unbekannten Originalpixel sein.

Wenn TATARUS nur 512 × 512 gesehen hat, kann das System nicht wissen, wie ein hypothetisches ursprüngliches 3840er Bild an jeder Stelle exakt aussah.

TATARUS macht deshalb etwas anderes.

Es nutzt die während der Wahrnehmung gespeicherten lokalen Eigenschaften:

```text
Pigment
Farbvariation
Kantenstärke
Kantenrichtung
lokale Struktur
```

Daraus wird im größeren Zielraum zusätzliche Pigmentstruktur erzeugt.

Diese Struktur ist:

- rekonstruiert,
- deterministisch,
- an die gespeicherte lokale Erfahrung gekoppelt.

Sie ist **nicht** als wiederhergestellte unbekannte Originalinformation zu verstehen.

---

# 11. Warum die neuen Details nicht einfach zufälliges Rauschen sind

Der Renderer verwendet deterministische Strukturgeneratoren.

Das bedeutet:

```text
gleiches Engramm
+ gleiche Zielauflösung
+ gleiche Renderbedingungen
= reproduzierbares Ergebnis
```

Die Struktur wird außerdem nicht überall gleich stark hinzugefügt.

Ihre Intensität hängt von der lokalen Variation der gespeicherten Pigmente ab.

Eine nahezu gleichmäßige Region erhält daher eine andere Struktur als ein stark strukturiertes Objekt.

---

# 12. Die Rolle des Nervensystems und die Rolle des Renderers

Für eine korrekte Beschreibung von TATARUS muss zwischen mehreren Ebenen unterschieden werden.

## Das synthetische Nervensystem übernimmt:

- visuelle Wahrnehmung,
- neuronale Repräsentation,
- Lernen,
- Assembly-Bildung,
- Engrammbildung,
- Wiedererkennung,
- Gedächtnisabruf,
- Auswahl beziehungsweise Reaktivierung des Motorprogramms.

## Der motorisch-perzeptive Ausführungsapparat übernimmt:

- die Ausführung der erinnerten Malhandlungen,
- die Transformation in den gewählten Zielraum,
- die lokale Pigmentausgabe,
- die kantenabhängige Ausrichtung,
- die Rekonstruktion zusätzlicher Zielraumstruktur.

## Der Rasterpuffer ist nur die physische Ausgabeebene:

```text
Nervensystem
     ↓
Gedächtnis
     ↓
Motorik
     ↓
Leinwand
     ↓
RGB-Pixel
```

Der Renderer ist damit nicht „das Gehirn“.

Er ist der Effektor, über den sich die gelernte visuelle Erinnerung ausdrückt.

---

# 13. Der biologische Vergleich

Eine anschauliche Analogie wäre:

Ein Mensch lernt eine Zeichnung auf einem kleinen Blatt Papier.

Später bekommt er eine wesentlich größere Leinwand.

Er nimmt nicht das kleine Blatt und zieht es fotografisch auseinander.

Er erinnert sich an:

- Formen,
- Positionen,
- Proportionen,
- Linien,
- Farben,
- Strukturen,

und führt seine Zeichenhandlung in einem größeren motorischen Raum erneut aus.

Natürlich ist TATARUS kein biologisches Gehirn aus Nervengewebe.

Es ist ein synthetisches, biologisch inspiriertes Nervensystem.

Aber genau diese Trennung zwischen:

```text
Wahrnehmung
Gedächtnis
Motorik
Ausführung
```

ist für die Architektur entscheidend.

---

# 14. Was der aktuelle Code ausdrücklich nicht mehr macht

Der frühere Renderpfad verwendete noch ein rekonstruiertes Niedrigauflösungsbild als Zwischenstufe und berechnete daraus die größere Ausgabe.

Im aktuellen Target-Space-Renderer wurde diese Strecke entfernt.

Der aktuelle Pfad besitzt:

- keine finale Catmull-Rom-Vergrößerung,
- keine Bicubic-Vergrößerung,
- kein rekonstruiertes 512er Zwischenbild als Rasterquelle für die große Ausgabe.

Die Motorik wird direkt im Zielraum ausgeführt.

---

# 15. Was der Code weiterhin aus dem 512er Raum übernimmt

Das bedeutet nicht, dass 512 × 512 überhaupt keine Rolle mehr spielt.

Die ursprüngliche Wahrnehmung und die gespeicherten Pigment-Patches stammen weiterhin aus diesem sensorischen Arbeitsraum.

Auch die gespeicherten Motorpositionen gehen aus dieser Erfahrung hervor.

Die korrekte Aussage lautet deshalb:

> Das 512er Raster ist weiterhin der sensorische Informations- und Lernraum.

Aber:

> Es ist nicht mehr das fertige Bild, das anschließend hochskaliert wird.

Dieser Unterschied ist entscheidend.

---

# 16. Messbarer Unterschied zum klassischen Upscaling

Der Architekturwechsel lässt sich auch im erzeugten Bild messen.

Bei einem früheren TATARUS-Render lag die Übereinstimmung mit einem klassischen Bicubic-Upscale extrem hoch.

Beispiel früherer Renderer:

```text
Korrelation ≈ 0,999968
PSNR ≈ 52,9 dB
≈ 99 % der Pixel maximal 1 RGB-Stufe von Bicubic entfernt
```

Das Ergebnis war mathematisch nahezu identisch mit klassischem Upscaling.

Nach dem neuen Target-Space-Renderer ergab der Test am 3840 × 3840 Bild:

```text
Korrelation zu Bicubic ≈ 0,99513
PSNR ≈ 33,44 dB
nur ≈ 49 % der Pixel maximal 1 RGB-Stufe entfernt
```

Das ist eine deutliche Veränderung.

---

# 17. Gleichzeitig bleibt die visuelle Identität erhalten

Ein Renderer könnte sich natürlich auch deshalb von Bicubic unterscheiden, weil er das Bild zerstört.

Deshalb wurde das neue 3840 × 3840 Ergebnis wieder auf 512 × 512 zurückgeführt und mit dem Original verglichen.

Die Korrelation zum Original lag weiterhin bei ungefähr:

```text
0,99755
```

Das bedeutet:

Die konkrete hochauflösende Pixelstruktur verändert sich deutlich.

Die visuelle Identität der Szene bleibt jedoch sehr stark erhalten.

Genau das ist für TATARUS entscheidend.

---

# 18. Hochfrequente Struktur im neuen Zielraum

Beim Vergleich des neuen TATARUS-Renders mit einem Bicubic-Upscale zeigte sich ebenfalls mehr hochfrequente Struktur.

Gemessene Werte:

| Strukturmaß | TATARUS | Bicubic |
|---|---:|---:|
| Hochfrequenzanteil | 0,260 | 0,148 |
| sehr hoher Frequenzanteil | 0,109 | 0,034 |
| mittlerer Gradient | 4,80 | 4,50 |
| Laplace-Varianz | 137,41 | 63,18 |

Das bedeutet:

Die zusätzliche Zielraumstruktur lässt sich quantitativ nachweisen.

Das Ergebnis besteht nicht einfach nur aus interpolierten Zwischenwerten des 512er Rasters.

---

# 19. Was TATARUS nicht behauptet

Für eine wissenschaftlich saubere Darstellung ist diese Grenze wichtig.

TATARUS behauptet nicht:

> „Ich weiß, wie die verlorenen 4K-Pixel wirklich aussahen.“

Das wäre nicht möglich.

TATARUS macht vielmehr:

> „Ich habe diese visuelle Struktur wahrgenommen und gelernt.  
> Wenn ich sie auf einer größeren Leinwand erneut ausdrücke, rekonstruiere ich zusätzliche lokale Struktur aus meiner gespeicherten Erfahrung.“

Das ist ein wesentlicher Unterschied.

---

# 20. Offizielle technische Einordnung

Die fachlich passendste Bezeichnung für den aktuellen Vorgang lautet:

## Deterministic Motor-Trace Target-Space Reconstruction

Auf Deutsch:

## Deterministische motorikbasierte Zielraum-Rekonstruktion

Diese Bezeichnung beschreibt drei zentrale Eigenschaften:

### 1. Deterministisch

Die gleiche Erinnerung und die gleichen Renderbedingungen erzeugen reproduzierbare Ergebnisse.

### 2. Motorikbasiert

Die Ausgabe entsteht aus einer gespeicherten motorischen Repräsentation und nicht aus einem fertig gespeicherten hochzuskalierenden Bild.

### 3. Zielraum-Rekonstruktion

Die Malhandlungen werden direkt auf der neu angelegten Zieloberfläche ausgeführt.

---

# 21. Was TATARUS bei einer Bildvergrößerung wirklich macht

Die vollständige Verarbeitung lässt sich so zusammenfassen:

```text
                VISUELLER REIZ
                      │
                      ▼
             VISUELLE WAHRNEHMUNG
                      │
                      ▼
           NEURONALE REPRÄSENTATION
                      │
                      ▼
                  ASSEMBLIES
                      │
                      ▼
              VISUELLES ENGRAMM
                      │
                      ▼
            MOTORISCHE ERINNERUNG
                      │
                      ▼
            GEDÄCHTNISREAKTIVIERUNG
                      │
                      ▼
       TRANSFORMATION IN DEN ZIELRAUM
                      │
                      ▼
      DIREKTE MOTORISCHE MALAUSFÜHRUNG
                      │
          ┌───────────┼────────────┐
          ▼           ▼            ▼
       Pigment      Kanten       Textur
          │           │            │
          └───────────┼────────────┘
                      ▼
       HOCHAUFLÖSENDE REKONSTRUKTION
```

---

# 22. Die zentrale Aussage für den Podcast

Wenn man TATARUS in einem einzigen Satz erklären möchte:

> **TATARUS vergrößert nicht das Bild, sondern den Raum, in dem seine gespeicherte visuelle Erinnerung erneut motorisch ausgedrückt wird.**

Oder noch direkter:

> **TATARUS skaliert keine Pixel hoch. Es erinnert sich an den Aufbau der Wahrnehmung und zeichnet diese Erinnerung auf einer größeren Leinwand neu.**

---

# 23. Wichtige wissenschaftliche Einschränkung

Der Begriff „neuronales Gehirn“ sollte bei öffentlicher Kommunikation präzise verwendet werden.

TATARUS ist kein biologisches Gehirn aus Nervengewebe.

Es ist ein:

> **synthetisches, biologisch inspiriertes Nervensystem mit neuronalen, gedächtnisbasierten und motorischen Mechanismen.**

Das ändert jedoch nichts an der zentralen Architektur:

Die hochauflösende Ausgabe ist an:

- Wahrnehmung,
- Gedächtnis,
- Engramme,
- neuronale Reaktivierung,
- Motorik

gekoppelt.

Der Renderer ist nur das Werkzeug, mit dem diese Erinnerung auf einer physischen Pixeloberfläche sichtbar wird.

---

# 24. Fazit

Der aktuelle TATARUS-Ansatz ist nicht sinnvoll als gewöhnliches Bild-Upscaling zu beschreiben.

Die Architektur verbindet:

- synthetische visuelle Wahrnehmung,
- neuronale Repräsentation,
- persistentes visuelles Gedächtnis,
- Engrammabruf,
- motorische Reproduktion,
- direkte Zielraum-Ausführung,
- kanten- und pigmentabhängige Rekonstruktion.

Die Vergrößerung ist damit nicht nur eine Transformation eines Rasterbildes.

Sie ist der erneute Ausdruck einer gespeicherten visuellen Erfahrung in einem größeren motorischen Raum.

Der wichtigste Satz bleibt deshalb:

> **TATARUS vergrößert nicht die Pixel seiner Erinnerung.  
> Es vergrößert den Raum, in dem es diese Erinnerung erneut ausdrückt.**

---

## Kurzer Moderationstext

**Moderator:**

„Wenn man hört, dass TATARUS ein Bild von 512 mal 512 auf 3840 mal 3840 bringt, könnte man zunächst an einen gewöhnlichen KI-Upscaler denken. Genau das passiert hier aber nicht.

Das Bild wird zuvor von einem synthetischen Nervensystem wahrgenommen und als visuelle und motorische Erfahrung gespeichert. TATARUS erinnert sich also nicht nur daran, wie etwas aussah, sondern auch daran, wie diese Wahrnehmung reproduziert werden kann.

Für die große Ausgabe wird dann keine kleine fertige Grafik hochgerechnet. Stattdessen erhält das gespeicherte Motorprogramm eine neue, größere Leinwand und führt die erinnerte Zeichenhandlung dort erneut aus.

Dabei entstehen zusätzliche lokale Strukturen aus den zuvor gelernten Pigment-, Kanten- und Oberflächeneigenschaften.

TATARUS behauptet nicht, verlorene Originalpixel wiederzufinden. Es erzeugt eine neue hochauflösende Darstellung seiner eigenen gespeicherten visuellen Erfahrung.

Der entscheidende Unterschied lautet deshalb: TATARUS vergrößert nicht das Bild – es vergrößert den Raum, in dem es seine Erinnerung ausdrückt.“  

---

## Technische Kerndaten des geprüften Beispiels

- Eingang: **512 × 512**
- TATARUS-Ausgabe: **3840 × 3840**
- Ausgabepixel: **14.745.600**
- klassisches finales Raster-Upscaling im neuen Target-Space-Pfad: **entfernt**
- direkte Ausführung auf Zielcanvas: **ja**
- deterministische Zielraumstruktur: **ja**
- tatsächliche unbekannte Originaldetails wiederhergestellt: **nein**
- visuelle Identität zum Original: **sehr hoch**
- Abweichung von klassischem Bicubic deutlich höher als im alten Renderer: **ja**

---

## Ein-Satz-Version für Podcast, Trailer oder Beschreibung

> **TATARUS vergrößert kein gespeichertes Rasterbild – sein synthetisches Nervensystem reaktiviert eine gelernte visuell-motorische Erinnerung und führt sie direkt auf einer größeren Leinwand erneut aus.**
