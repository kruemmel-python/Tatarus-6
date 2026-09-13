TATARUS ist kein Feature-Stack und keine Sammlung lose gekoppelter Komponenten.

Es ist ein **persistenter synthetischer Organismus**, in dem Nervensystem, Gewebe, Physiologie, Sinnesorgane, Gedächtnis, Prospektion, Motorik und optionale Cortex-Funktionen in einem gemeinsamen kausalen Zustand zusammenwirken.

Die Entwicklung folgt deshalb nicht dem Prinzip:

> **Feature hinzufügen → Fehler patchen → nächstes Feature hinzufügen**

sondern:

> **Zustände definieren → Kausalität herstellen → Grenzen erzwingen → Verhalten prüfen → System vereinfachen**

### 1. Zustände vor Features

TATARUS wird nicht primär als Sammlung von Funktionen entwickelt.

Entscheidend sind:

* welche Zustände existieren,
* welche Zustände gültig sind,
* welche Zustände niemals entstehen dürfen,
* welche Übergänge zwischen Zuständen erlaubt sind,
* welche messbaren Konsequenzen ein Übergang erzeugen muss.

Ein Feature ist nur dann sinnvoll, wenn es in diesen Zustandsraum integriert werden kann.

---

### 2. Kausalität vor Bequemlichkeit

Systemverhalten muss erklärbar bleiben.

Wenn sich Wahrnehmung, Gedächtnis, Physiologie oder Motorik verändern, soll nachvollziehbar sein, **welcher vorherige Zustand diese Veränderung verursacht hat**.

TATARUS bevorzugt deshalb kausale Kopplungen gegenüber versteckten Seiteneffekten und nicht nachvollziehbarer Automatisierung.

---

### 3. Gültige Zustände definieren

Ein robustes System muss wissen, welche Zustände zulässig sind.

Nicht alles, was technisch möglich wäre, ist deshalb auch architektonisch erlaubt.

Schnittstellen und Module sollen den erlaubten Zustandsraum möglichst klar ausdrücken.

---

### 4. Verbotene Zustände konstruktiv verhindern

Fehler werden nicht nur behandelt, nachdem sie aufgetreten sind.

Wenn eine Fehlerklasse strukturell verhindert werden kann, soll die Architektur so verändert werden, dass dieser Zustand gar nicht erst repräsentierbar oder erreichbar ist.

Das Ziel lautet nicht:

> **Fehler schneller reparieren.**

Sondern möglichst oft:

> **Die Ursache entfernen, durch die diese Fehlerklasse überhaupt entstehen konnte.**

---

### 5. Gemeinsamer kausaler Zustand

Neuronale Aktivität, Körperphysiologie, Sensorik, Gedächtnis, Motorik und Lernen werden nicht als vollständig unabhängige Systeme betrachtet.

Sie beeinflussen denselben Organismuszustand.

Diese Kopplung ist ein wesentliches Architekturprinzip von TATARUS.

---

### 6. Architektur vor Patch

Ein einzelner Fehler kann lokal behoben werden.

Wiederholt sich jedoch dieselbe Fehlerklasse, wird die zugrunde liegende Architektur hinterfragt.

```text
Fehler
  ↓
Ursache
  ↓
gemeinsames Muster
  ↓
verletzte Invariante
  ↓
Architekturänderung
```

Ein Patch darf ein Problem lösen.

Er darf aber nicht zum Ersatz für strukturelles Denken werden.

---

### 7. Determinismus vor Magie

TATARUS bevorzugt reproduzierbares Verhalten.

Persistenz, Snapshots und deterministische Tests gehören deshalb zum Kern des Systems.

Ein Verhalten, das nicht reproduziert, gemessen oder untersucht werden kann, ist nur eingeschränkt wissenschaftlich und technisch bewertbar.

---

### 8. Tests sind Beweisflächen

Tests sind keine nachträgliche Qualitätssicherung.

Sie gehören zur Architektur.

TATARUS besitzt getrennte Tests unter anderem für:

* Neurobiologie,
* Physiologie,
* Kreislauf,
* Herz,
* Nieren,
* Sinnesorgane,
* IMAGINATIO,
* Cortex,
* Organismus,
* kausale Validierung,
* Rendering,
* SDK und C-API.

Eine starke Behauptung über das System sollte nach Möglichkeit durch reproduzierbares Verhalten, Messung oder einen Test abgesichert werden.

---

### 9. Compilerwarnungen sind Fehler

TATARUS behandelt Compilerwarnungen bewusst als Buildfehler.

Für MSVC werden unter anderem `/W4 /WX /permissive-` verwendet; auf anderen unterstützten Compilern werden Warnungen ebenfalls als Fehler behandelt.

```text
warning ≠ kosmetische Information

warning = möglicher Bruch einer Annahme
```

Ein Release soll warning-clean sein.

---

### 10. Capability-Grenzen statt Allmacht

Eine intelligente Komponente darf nicht automatisch jede Ebene des Systems verändern.

Der optionale Cortex bleibt deshalb durch Arbiter- und Capability-Grenzen von direkter Manipulation zentraler Motor-, Reward-, Physiologie-, Neuronen- und Synapsenzustände getrennt.

Intelligenz ohne Grenzen ist keine gute Architektur.

---

### 11. Wahrnehmung ist ein Prozess

Wahrnehmung wird nicht auf ein extern erzeugtes Label reduziert.

Die visuelle Verarbeitung umfasst in TATARUS unter anderem Photorezeptoren, retinale Verarbeitung, ON-/OFF- und Farbopponenz, Sehnerv, V1/V2, Objektteile und Kategoriegedächtnis.

Das Ziel ist nicht nur:

> **Was ist auf dem Bild?**

sondern:

> **Wie entsteht aus einem Reiz ein interner Zustand?**

---

### 12. Lernen muss den Organismus verändern

Lernen ist nicht nur eine temporär bessere Ausgabe.

Ein Lernvorgang soll interne Zustände verändern und persistierbare Spuren hinterlassen.

Beim visuellen Lernen verarbeitet TATARUS einen Reiz, führt eine eigene Malhandlung aus, nimmt das Ergebnis erneut wahr und erzeugt daraus visuelle, semantische und motorische Engrammzustände.

---

### 13. Konsolidierung gehört zum Lernen

Lernen endet nicht mit dem ersten erfolgreichen Verhalten.

Reaktivierung, Schlafzustände, Konsolidierung und strukturelle Stabilisierung gehören zum selben Prozess.

Erfahrung soll nicht nur gespeichert werden.

Sie soll langfristig **Struktur verändern**.

---

### 14. Leistung beginnt in der Architektur

Latenz, Speicherverbrauch, Durchsatz und Skalierbarkeit sollen nicht erst nach Fertigstellung betrachtet werden.

Eine Architektur, die fundamentale Ressourcenprobleme erzeugt, wird nicht durch spätere Optimierung automatisch gut.

Performance ist deshalb keine kosmetische Endphase.

Sie ist Teil des Entwurfs.

---

### 15. Weniger Code kann mehr System bedeuten

Codeproduktion ist keine Qualitätsmetrik.

Wenn eine bessere Architektur 10.000 Zeilen durch 3.000 klarere Zeilen ersetzen kann, ist das kein Verlust.

Es ist Fortschritt.

```text
mehr Code
≠
mehr Funktion

weniger unnötige Zustände
+
weniger Abhängigkeiten
+
klarere Invarianten
=
besseres System
```

---

### 16. Funktionale Kohärenz vor biologischer Dekoration

TATARUS verwendet biologische Prinzipien als Architektur- und Forschungsinspiration.

Eine biologische Bezeichnung allein macht einen Mechanismus jedoch nicht biologisch korrekt.

Jede Analogie muss funktional begründbar, technisch nachvollziehbar und soweit möglich validierbar sein.

---

### 17. Freies Denken verlangt technische Disziplin

TATARUS soll neue Ansätze ermöglichen.

Das bedeutet jedoch nicht Beliebigkeit.

Im Gegenteil:

> **Je freier der Entwurf, desto härter müssen Tests, Invarianten, Messbarkeit und Systemgrenzen sein.**

---

## Kurzform

```text
Denke in Zuständen, nicht in Features.

Erkläre Verhalten kausal.

Definiere gültige Zustände.

Verhindere verbotene Zustände konstruktiv.

Behebe Fehlerklassen statt nur Fehlerinstanzen.

Bevorzuge Architektur gegenüber Patchserien.

Fordere Determinismus und Reproduzierbarkeit.

Teste Behauptungen.

Begrenze Macht durch Capabilities.

Lass Lernen den inneren Zustand verändern.

Optimiere das System, nicht die Menge des erzeugten Codes.

Entferne unnötige Komplexität.
```

### Grundsatz

> **TATARUS wird nicht entwickelt, um möglichst viel Code zu erzeugen.
> TATARUS wird entwickelt, um mit möglichst klaren Zuständen, Regeln und kausalen Mechanismen komplexes Verhalten entstehen zu lassen.**
