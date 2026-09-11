# TATARUS Live - Nervensystem und Robotertraining

`START_TATARUS.bat` im Projektordner baut die benötigte Bibliothek, startet
den lokalen Trainingsdienst und öffnet die Oberfläche.

Die obere 3D-Ansicht zeigt das Nervensystem des Roboters in der unteren Karte.
Über **Gewebegröße** stehen 96, 384, 1.536 oder 6.144 Neuronen zur Verfügung.
Ein Wechsel setzt beide Ansichten und den gemeinsamen Trainingszustand zurück.
Vier neuronale Motorpopulationen wählen seine Bewegung. Sensoren, Neuheit,
Kollisionen und Belohnungen kehren in dieselbe TATARUS-Instanz zurück. Pause,
Geschwindigkeit und Neustart steuern beide Ansichten gemeinsam.

Die 3D-Navigation enthält zehn auswählbare Kartenplätze. **Karte bearbeiten**
pausiert den gemeinsamen Kreislauf und erlaubt, Hindernisse, freie Zellen,
Start und Ziel direkt per Klick in der 3D-Karte zu ändern. Jede Änderung wird
auf Lösbarkeit geprüft und im gewählten Kartenplatz dauerhaft gespeichert.
Beim Wechsel der Testkarte werden die kartenbezogenen Laufwerte neu begonnen;
das bereits gelernte Nervensystem und der Controller-Lernzustand bleiben jedoch
erhalten. Dadurch kann ein geladener Embodiment-Snapshot kontrolliert auf einer
anderen Karte getestet werden.

Erforderlich sind Python 3, CMake, die Visual Studio 2022 C++ Build Tools und
beim ersten Start eine Internetverbindung für das festgelegte Three.js-Modul. Der Starter
begrenzt den nativen Build auf zwei parallele Compilerprozesse, um den MSVC-Compiler-Heap
bei den Cortex-Quelldateien nicht zu überlasten.

Der lokale Dienst hört ausschließlich auf `127.0.0.1:8765`. Beendet wird er im
Starterfenster mit `Strg+C`.

Die Cortex-/IMAGINATIO-Bedienung ist im
[Cortex-UI-Handbuch](../../Docs/TATARUS_CORTEX_UI.md) und im
[integrierten UI-Handbuch](../../Docs/TATARUS_START_TATARUS_INTEGRATED_UI.md) erklärt.


## Integriertes IMAGINATIO V14 + Cortex Studio

`START_TATARUS.bat` baut die Shared-C-ABI explizit mit `TATARUS_BUILD_CORTEX=ON`.
Die Hauptoberfläche unter `http://127.0.0.1:8765/` bettet das aktuelle Studio aus
`tools/imaginatio_lab/` unter `/imaginatio-lab/` ein. Beide Ansichten verwenden
dieselben `/api/imaginatio/*`- und `/api/cortex/*`-Endpunkte und damit denselben
laufenden Organismus. Das alte 32×32-Dashboard-Zeichenlabor ist nicht mehr der
sichtbare Bedienpfad.

Das eingebettete Studio enthält außerdem dieselbe Schaltfläche
**Aktuelles Bild biologisch erkennen** wie das Standalone-Labor. Das aktuelle
RGB-Bild wird über Retina, Ganglienzellen, Sehnerv, V1/V2 und das gelernte
ventrale Kategoriegedächtnis ausgewertet. Beide Ansichten verwenden dafür
dieselbe C-ABI-Funktion und denselben Organismuszustand. Details stehen in
[`Docs/BIOLOGICAL_VISUAL_PATHWAY.md`](../../Docs/BIOLOGICAL_VISUAL_PATHWAY.md).
