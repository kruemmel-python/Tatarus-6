window.TATARUS_SEARCH_INDEX = [
  {
    "title": "Dokumentationsprüfung",
    "url": "build-report.html",
    "group": "Dokumentation",
    "text": "Dokumentationsprüfung · TATARUS T TATARUS UNIFIED DOCUMENTATION Integritätsprüfung Dokumentationsprüfung Automatische Prüfung des Dokumentationssatzes gegen zentrale Verträge des veröffentlichten Quellcodes. PASS 62 HTML-Dateien · 2371 lokale Referenzen geprüft · 16 Quellpfade geprüft Code-Verträge Prüfung Status Quelle warnings-as-errors PASS CMakeLists.txt optional Cortex build switch PASS CMakeLists.txt no persisted source raster PASS src/tatarus_imaginatio.cpp teacher trace transient PASS src/tatarus_imaginatio.cpp working canvas not persisted PASS src/tatarus_imaginatio.cpp self motor memory persisted in engram PASS src/tatarus_imaginatio.cpp target-space renderer avoids raster interpolation PASS src/tatarus_imaginatio.cpp bilateral kidneys are owned by organism PASS modules/tatarus_organism/tatarus_organism.hpp ocular pupil telemetry PASS include/tatarus/ocular_system.hpp vestibular nerve events PASS include/tatarus/vestibular_system.hpp optic nerve events PASS include/tatarus/visual_pathway.hpp Cortex has no direct motor authority PASS config/cortex_hybrid.json Cortex has no direct reward authority PASS config/cortex_hybrid.json Interne Referenzen Keine gebrochenen internen Links oder Assets gefunden. Dokumentationskonsistenz Keine nummerierte Projektmarke, keine Implementierungs-/Änderungschronik und keine bekannten veralteten IMAGINATIO-Aussagen gefunden. Publikationsprinzip Der Dokumentationssatz beschreibt ausschließlich den veröffentlichten Code als aktuellen Systemzustand. Technische Bezeichner werden nur dort genannt, wo sie Bestandteil einer API oder eines Laufzeitvertrags sind."
  },
  {
    "title": "TATARUS",
    "url": "index.html",
    "group": "Dokumentation",
    "text": "TATARUS · Dokumentation T TATARUS UNIFIED DOCUMENTATION Synthetic Organism & Embodied Intelligence SDK TATARUS Ein persistenter synthetischer Organismus, in dem Nervensystem, Gewebe, Körperorgane, Sinneswahrnehmung, Gedächtnis, Prospektion und Handlung in einem gemeinsamen kausalen Zustand arbeiten. Aktueller Projektstand Dokumentation direkt gegen den veröffentlichten Quellcode geprüft · GPL-3.0 Vom Reiz zur Erfahrung TATARUS behandelt Umwelt- und Bildsignale nicht nur als Daten, die transformiert werden. Reize verändern einen laufenden synthetischen Organismus: Augen-/Vestibularsystem, Nervensystem, Körperphysiologie, Engramme und Motorik wirken zusammen. Handbücher Projekt Orientierung und Systemgrenze System Gesamtarchitektur Handbuch Kopplungen und Laufmodell Organismus Herz, Lunge, Kreislauf, Nieren, Interozeption IMAGINATIO Wahrnehmung, Self-Imprint und visuelles Gedächtnis Cortex Optionale LLM-/Executive-Schicht unter Safety-Grenzen UI Live-Bedienung und Telemetrie API C++ und C ABI Integration Build und Host-Einbettung Validierung Tests, Kausalität, Determinismus Benchmark Referenzmessdaten Einordnung Wissenschaftliche Grenzen System-Wiki Wiki öffnen Architektur, Organe, Neurobiologie, IMAGINATIO, Cortex, Persistenz, Tests und Forschungsgrenzen. Sitemap Alle Dokumentationsseiten auf einen Blick. Dokumentationsprüfung Integrität, Linkprüfung und Codeabgleich. Suche"
  },
  {
    "title": "API-Referenz",
    "url": "manuals/api-referenz.html",
    "group": "Handbuch",
    "text": "API-Referenz · TATARUS Zum Inhalt Technische Dokumentation API-Referenz Öffentliche C++- und C-ABI-Schnittstellen des aktuellen Projekts für Mind, Organismus, IMAGINATIO, Kartografie und Cortex. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 C++: RobotMind RobotMind mind; auto report = mind.observe(experience); mind.beginAction(action); mind.endAction(outcome); auto prediction = mind.predict(); auto horizon = mind.predictHorizon(depth); auto physiology = mind.physiology(); auto motor = mind.motor(); mind.saveSnapshot(directory); Zusätzliche APIs decken Explorer-Frames, Scannerintegration, Identitätsbeobachtung, Kontext, Throughput, Kartenverwaltung, Ruhe, Lernschalter und experimentelle Interventionen ab. C++: SyntheticOrganism SyntheticOrganism organism; auto result = organism.stepWithLoad(experience, atmosphere, mechanicalLoad, dt); auto body = organism.telemetry(); auto interoception = organism.interoception(); organism.hemorrhage(volumeMl); organism.setRenalFunction(leftFraction, rightFraction); Zugriffsfunktionen liefern Mind, Kreislauf, Herz, Lunge, beide Nieren und die getrennten linken/rechten Niereninstanzen. C++: VisualImagination VisualImagination visual(sharedMind, config); auto learned = visual.learnToTrace(reference, label, teacherActions); auto recalled = visual.drawFromMemory(cue); visual.addCategoryExample(category, cue); auto fusion = visual.fuseCategories(categories); auto target = visual.renderLastTargetSpace(width, height); visual.saveSnapshot(directory); Weitere APIs umfassen Symbolassoziation, freie Zeichnung, Komposition, Pose-Lernen, Szenenwahrnehmung, Relationsszenen, gelernte Übergänge, Zukunftsimagination, Quellauflösungsmetadaten und JSON/Trace-Ausgabe. C++: Cortex CortexOrchestrator cortex(config); cortex.observe(...); cortex.re"
  },
  {
    "title": "Skalierungsbenchmark",
    "url": "manuals/benchmark.html",
    "group": "Handbuch",
    "text": "Skalierungsbenchmark · TATARUS Zum Inhalt Technische Dokumentation Skalierungsbenchmark Im Repository enthaltene Messdaten zur Rechenlast verschiedener Neuronenzahlen und zur zusätzlichen Telemetriekosten des synthetischen Organismus. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Datensatz data/TATARUS_BENCHMARK_DATEN.json enthält reproduzierbar archivierte Messwerte eines optimierten Windows-x64-Laufs mit Seed 7411, 10 Sekunden Messdauer, 0,05 Sekunden externem dt und fünf Warmup-Schritten. Die Werte sind eine Referenzmessung des enthaltenen Codes auf der damaligen Messmaschine, keine universelle Hardwaregarantie. Ausgewählte Messwerte Profil Neuronen Modus externe Schritte/s neuronale Schritte/s Realtime-Faktor compact 96 core 171,32 6.852,90 8,566 compact 96 telemetry 46,76 1.870,24 2,338 standard 384 core 27,08 1.083,20 1,354 standard 384 telemetry 5,67 226,86 0,284 large 1.536 core 4,83 193,02 0,241 research 6.144 core 1,01 40,52 0,051 Was die Zahlen zeigen Die Rechenlast steigt mit Nervensystemgröße und aktiver Telemetrie deutlich. Die Daten dienen deshalb vor allem der Kapazitätsplanung: UI/Telemetry sollte nicht mit Core-Throughput gleichgesetzt werden. Organphysiologie bleibt im Messlauf aktiv und liefert gleichzeitig MAP, SaO₂, Herzzeitvolumen und GFR. Interpretation Der Datensatz ist eine konkrete Messprobe des veröffentlichten Projekts. Werte hängen von Hardware, Compiler, Build-Konfiguration, Telemetrieumfang und Laufparametern ab und sind daher keine allgemeine Performancegarantie. Dokumentationsstart System-Wiki Repository"
  },
  {
    "title": "Hybrid Cortex & Executive",
    "url": "manuals/cortex.html",
    "group": "Handbuch",
    "text": "Hybrid Cortex & Executive · TATARUS Zum Inhalt Technische Dokumentation Hybrid Cortex & Executive Optionale lokale Sprachmodellschicht für Beratung, Imagination und längerfristige Planung – unter einer harten Autoritätsgrenze zum TATARUS-Kern. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Rolle des Cortex Der Cortex ist eine optionale Zusatzschicht. Er konsumiert abstrahierte TATARUS-Zustände und kann Strategien, Informationsanforderungen, Imagination oder Pläne vorschlagen. Das Nervensystem und der Organismus bleiben die ausführende und lernende Basis. Betriebsmodi CortexMode unterstützt Disabled, ObserveOnly, Advisor, Imagination und Hybrid. Die Ausführung kann Off, Live, Record oder Replay sein. Aufgaben umfassen Zustandsanalyse, Planung, Imagination, Hybridplanung, Dream und ExecutivePlan. Trigger Anfragen können explizit oder aufgrund von Neuheit, Prediction Error, niedriger Motor-Confidence, kombinierter Unsicherheit, REM, Zielwechsel oder metakognitivem Fallback entstehen. Der Arbiter entscheidet, ob eine Modellantwort überhaupt benutzt werden darf. Sicherheitsvertrag Direkte Befugnis Status Reward schreiben deaktiviert Motorsteuerung übernehmen deaktiviert Körperphysiologie mutieren deaktiviert Neuronen direkt verändern deaktiviert Synapsen direkt verändern deaktiviert Der Cortex kann also beraten, aber nicht die kausale Lern- und Handlungskette umgehen. Ein Executive-Plan wird erst durch reale ActionOutcome -Rückmeldungen fortgeschrieben. Executive-Funktionen CortexOrchestrator verwaltet persistente Ziele, Working-Memory-Einträge, mehrschrittige Pläne, Planstatus, Metakognition und autonome Zyklen. Record/Replay ermöglicht reproduzierbare Entscheidungsfolgen. Schlafzyklen können Dream-/Imagination-Aufgaben nutzen; Teacher-Transfer ist an tatsächlic"
  },
  {
    "title": "Explorer-Kartografie",
    "url": "manuals/explorer-kartografie.html",
    "group": "Handbuch",
    "text": "Explorer-Kartografie · TATARUS Zum Inhalt Technische Dokumentation Explorer-Kartografie Persistente räumliche Erfahrung, Scannerfusion und Umweltkarte für Rover-/Robotikexperimente. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Explorer-Pfad RobotMind::observeExplorer verbindet Erfahrung und Zielrichtung mit räumlichem Lernen. Scannerframes werden separat über integrateScan in die Umweltkarte aufgenommen. Karte und Nervensystem Die Kartografie ist kein Ersatz für Place-/Action-Engramme. Sie ist die explizite räumliche Umweltrepräsentation, während der Mind gleichzeitig Assemblies, Aktionen, Rewards und prospektive Übergänge lernt. Persistenz Environment Maps sind Teil des persistenten Systemzustands und können pro EnvironmentId verwaltet und gelöscht werden. Die C ABI stellt JSON-Ausgabe und Clear-Funktionen bereit. Integrationsregel Der Host liefert reale oder simulierte Sensordaten. TATARUS wählt Motoraktivität; die Umwelt führt sie aus und meldet das Ergebnis zurück. Dadurch bleibt Erfolg an eine beobachtbare Konsequenz gebunden. Dokumentationsstart System-Wiki Repository"
  },
  {
    "title": "Gewebewachstum und Mechanik",
    "url": "manuals/gewebewachstum.html",
    "group": "Handbuch",
    "text": "Gewebewachstum und Mechanik · TATARUS Zum Inhalt Technische Dokumentation Gewebewachstum und Mechanik Räumliche Zellstruktur, Glia, Kapillaren, Myelin und mechanische Kopplungen des neuronalen Gewebes. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 3D-Geometrie Das Tissue-Modul ordnet Neuronen in räumliche Regionen, Schichten und Hemisphären ein. Geometrische Distanz beeinflusst Konnektivität, Axonlänge und Verzögerung. Glia und Gefäße Glia-, Kapillar- und Versorgungszustände begleiten die neuronale Topologie. Myelinisierung verändert Leitungseigenschaften; mikrogliale und metabolische Zustände sind Teil der Langzeitdynamik. Mechanik Das Gewebe führt mechanische Größen als erklärende physikalische Kopplung. Diese Größen sind kein Finite-Elemente-Modell eines realen Gehirns, sondern Bestandteil der synthetischen Gewebedynamik. Skalierung Die Konfiguration erlaubt unterschiedliche Neuronenzahlen. Performance und Detailgrad werden getrennt von den biologisch inspirierten Regeln betrachtet; die Tests decken sowohl kleine deterministische Konfigurationen als auch skalierte Zustände ab. Dokumentationsstart System-Wiki Repository"
  },
  {
    "title": "IMAGINATIO",
    "url": "manuals/imaginatio.html",
    "group": "Handbuch",
    "text": "IMAGINATIO · TATARUS Zum Inhalt Technische Dokumentation IMAGINATIO Visuelle Wahrnehmung, eigenes Nachzeichnen, Self-Imprint, Engramm, Recall, Kategorien, Relationen, Prospektion und direktes Zielraum-Rendering. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Grundprinzip IMAGINATIO behandelt ein Bild nicht primär als Datei, die transformiert werden soll, sondern als visuellen Reiz, der den Zustand des synthetischen Organismus verändert. Die Lernreferenz ist während der Demonstration sichtbar. TATARUS verarbeitet sie über Augen-/Blicksystem und VisualPathway, führt eine eigene serielle Malhandlung aus und nimmt anschließend das eigene fertige Ergebnis erneut wahr. Biologisch inspirierter Sehpfad OcularSystem koppelt Luminanz an Pupillendurchmesser und Netzhautadaptation und führt gaze/saccade-Zustände. VestibularSystem liefert Dreh-, Gravitations- und VOR-Signale. VisualPathway modelliert RGB-Photorezeptoren, ON/OFF-centre-surround und farbopponente Ganglionantworten, begrenzte Sehnervereignisse, V1-Orientierungszellen, V2-/Objektteilmerkmale und ventrale Aktivierung. Lernzyklus und Self-Imprint Referenz sichtbar → Ocular/Vestibular → Retina → Sehnerv → V1/V2 → ventraler Objektpfad → serielle TATARUS-Malhandlung → eigener fertiger Canvas → erneute Wahrnehmung des eigenen Ergebnisses → Self-Assembly + perceptuelles Engramm + Self-Motor-Engramm → persistenter Nervensystemzustand Der Teacher-Pigment-Patch ist eine transiente Demonstrationshilfe. Die persistente visuelle Erinnerung enthält keinen separaten Quellraster und keinen persistenten Teacher-Trace. Der Working Canvas wird ebenfalls nicht mitgespeichert. Was im Engramm bleibt Ein VisualEngram kombiniert Fingerprint, Inhalts-Hash, Shape-Signatur, Objektteil-/Featuremodell, Region- und Stroke-Tokens, Assembl"
  },
  {
    "title": "Integrationsleitfaden",
    "url": "manuals/integration.html",
    "group": "Handbuch",
    "text": "Integrationsleitfaden · TATARUS Zum Inhalt Technische Dokumentation Integrationsleitfaden Build, Einbettung und Host-Integration des TATARUS-Kerns, synthetischen Organismus, IMAGINATIO und optionalen Cortex. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Build-Anforderungen CMake benötigt mindestens 3.22, der Quellcode C++20. Relevante Optionen sind TATARUS_BUILD_TESTS , TATARUS_BUILD_EXAMPLES , TATARUS_BUILD_SHARED und TATARUS_BUILD_CORTEX . cmake -S . -B build -DTATARUS_BUILD_TESTS=ON -DTATARUS_BUILD_SHARED=ON -DTATARUS_BUILD_CORTEX=ON cmake --build build --config Release ctest --test-dir build -C Release --output-on-failure Logische Targets Die zentralen CMake-Targets heißen tatarus_neurobiology , tatarus_organism , tatarus_sdk , optional tatarus_cortex und bei Shared-Build tatarus_c . Für Integration sollten diese logischen Targets und die öffentlichen Header verwendet werden. Robotik Ein Host bildet Sensorwerte auf Experience oder Explorer-Frames ab, liest MotorTelemetry, führt die Aktion außerhalb von TATARUS aus und schließt sie mit ActionOutcome . Scannerframes können parallel in die Kartografie integriert werden. Organismus Wenn Körperphysiologie Teil des Experiments sein soll, wird statt eines isolierten RobotMind ein SyntheticOrganism verwendet. Atmosphäre, Belastung und dt werden pro Schritt geliefert; die Organinstanz führt Mind und Körper gemeinsam fort. IMAGINATIO VisualImagination kann einen vorhandenen RobotMind referenzieren. Dadurch entstehen visuelle Erfahrungen im selben Nervensystemzustand. Eingangsbilddaten sind Lernreize; persistente Zustände werden über die Snapshot-API geschrieben. Cortex Der lokale Cortex wird optional kompiliert. Hostprogramme dürfen ihn als Berater/Planer verwenden, sollten aber die Arbiter-Grenze nicht umgehen. "
  },
  {
    "title": "Synthetischer Organismus",
    "url": "manuals/organismus.html",
    "group": "Handbuch",
    "text": "Synthetischer Organismus · TATARUS Zum Inhalt Technische Dokumentation Synthetischer Organismus Herz, Kreislauf, Lunge, zwei Nieren, Hormonsignale, Interozeption und ihre Kopplung mit dem persistenten Nervensystem. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Der Körper ist Teil des Rechenzustands SyntheticOrganism besitzt RobotMind , Circulation , Heart , Lung , BilateralKidneys und ein Conservation Ledger. Der Organismus-Schritt führt Körper und Nervensystem in einer festen Kopplung fort. Herz Das Herzmodell enthält rechten und linken Vorhof, rechten und linken Ventrikel sowie Trikuspidal-, Pulmonal-, Mitral- und Aortenklappe. Die Erregungsleitung modelliert SA-Knoten, Vorhöfe, AV-Knoten, Purkinje-System und ventrikuläres Myokard. Intrazelluläres und sarkoplasmatisches Calcium, aktive Spannung und Kontraktilität koppeln Elektrophysiologie an Mechanik. Sympathikus/Parasympathikus und extrazelluläres K⁺/Ca²⁺ modulieren die Dynamik. Lunge Die Lunge führt Atemfrequenz, Atemzugvolumen, Totraum, funktionelle Residualkapazität, Compliance und Atemwegswiderstand. Alveolärer O₂-/CO₂-Austausch und Diffusionskapazität koppeln Atmosphäre und Blut. Zentrale und periphere Chemorezeptoren reagieren auf PaCO₂, PaO₂ und pH. Kreislauf Der Kreislauf enthält arterielle, venöse, pulmonalarterielle, pulmonalkapilläre, pulmonalvenöse, zerebrale, renale, koronare und periphere Kompartimente. Transportiert werden unter anderem O₂, CO₂, HCO₃⁻, Glukose, Na⁺, K⁺, ionisiertes Ca²⁺, Cl⁻, Harnstoff, Laktat und Plasmaprotein. Regionale Austauschfunktionen koppeln Gehirn, Herz, Niere, Lunge und Peripherie. Zwei Nieren BilateralKidneys besitzt eine linke und eine rechte Kidney -Instanz. Jede führt Bowman-Raum, proximalen Tubulus, absteigende und aufsteigende Henle-Schleife, distalen Tubu"
  },
  {
    "title": "Projektübersicht",
    "url": "manuals/projektuebersicht.html",
    "group": "Handbuch",
    "text": "Projektübersicht · TATARUS Zum Inhalt Technische Dokumentation Projektübersicht TATARUS als persistenter synthetischer Organismus: Nervensystem, Körperphysiologie, Wahrnehmung, Gedächtnis, Handlung, IMAGINATIO und optionaler Cortex in einem gekoppelten Laufzustand. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Was TATARUS ist TATARUS ist kein einzelnes neuronales Modell und kein Bildfilter. Das Projekt koppelt ein persistentes Nervensystem mit räumlichem Gewebe, neuronaler Physiologie, einem synthetischen Körper, Sensorik, Motorik, Gedächtnis, Prospektion und Werkzeugen für Robotik und visuelle Imagination. Alle diese Teile teilen einen laufenden Zustand und beeinflussen sich über definierte Schnittstellen. Leitidee: Ein äußerer Reiz verändert den Zustand des synthetischen Organismus. Lernen bedeutet deshalb nicht nur Parameteranpassung eines isolierten Funktionsblocks, sondern eine persistente Zustandsänderung, aus der spätere Wahrnehmung und Handlung hervorgehen. Der gekoppelte Organismus SyntheticOrganism besitzt genau einen RobotMind sowie Kreislauf, Herz, Lunge und ein Paar unabhängig modellierter Nieren. Autonome Regulation und Interozeption führen Körperzustände zurück in den Nervenzustand. Damit kann dieselbe Erfahrung unter unterschiedlicher Sauerstoffversorgung, Kreislauflast, Elektrolytlage oder metabolischer Belastung einen anderen internen Kontext erzeugen. Bereich Realer Codegegenstand Aufgabe Nervensystem RobotMind , Neural Network, Tissue, Physiology Spikes, Synapsen, Assemblies, Plastizität, Gewebe, Ionen, ATP, Schlaf Körper SyntheticOrganism Geschlossener Gehirn-Körper-Schritt Organe Heart, Lung, Circulation, BilateralKidneys Perfusion, Gaswechsel, Kreislauf, Filtration und Homöostase Sehen OcularSystem, VestibularSystem, VisualPathway Pup"
  },
  {
    "title": "Systemhandbuch",
    "url": "manuals/systemhandbuch.html",
    "group": "Handbuch",
    "text": "Systemhandbuch · TATARUS Zum Inhalt Technische Dokumentation Systemhandbuch Detaillierte Beschreibung der Zustandskopplung von Nervensystem, Körper, Sinnesorganen, Gedächtnis, Motorik und optionalem Cortex. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 1. Ausführungsmodell Ein TATARUS-Lauf ist zustandsorientiert. RobotMind verarbeitet Erfahrungen, hält neuronale, physiologische, prospektive, motorische und räumliche Zustände und kann diesen Zustand snapshotten. SyntheticOrganism erweitert denselben Mind um Körperphysiologie; er legt keinen zweiten Mind daneben. 2. Nervensystem und Gewebe Das Neural-Network-Modul führt spikende Dynamik mit Dendriten, Rezeptoren, Synapsen, Verzögerungen, Plastizität, Assemblies und Motorpopulationen. Das Tissue-Modul gibt den Zellen eine räumliche 3D-Struktur und koppelt Glia, Kapillaren, Myelin und mechanische Größen. Physiology führt Ionen, ATP, Pumpen, Neuromodulatoren, molekulare Plastizität und Schlafhomöostase. 3. Körperphysiologie Herz, Kreislauf, Lunge und zwei Nieren werden als gekoppelte Kompartimente fortgeschrieben. Sauerstoff, Kohlendioxid, pH, Glukose, Elektrolyte, Osmolarität, Blutdruck, Perfusion und Hormonsignale bilden einen gemeinsamen Körperkontext. Interozeption kondensiert diese Größen zu Signalen, die dem Nervensystem wieder zur Verfügung stehen. 4. Visuelle Wahrnehmung Das OcularSystem regelt Pupillendurchmesser, Hell-/Dunkeladaptation und Blickrichtung. Das VestibularSystem liefert Bewegungs- und VOR-Signale. Der VisualPathway transformiert RGB-Signale in Photorezeptorantworten, ON/OFF- und farbopponente Ganglionaktivität, begrenzte Sehnervereignisse, V1-Orientierungsmerkmale, V2-/Objektteile und ventrale Aktivierungen. 5. IMAGINATIO als sensomotorische Erfahrung Beim demonstrierten Lernen ist die Ref"
  },
  {
    "title": "Systemübersicht",
    "url": "manuals/systemuebersicht.html",
    "group": "Handbuch",
    "text": "Systemübersicht · TATARUS Zum Inhalt Technische Dokumentation Systemübersicht Architekturkarte des aktuellen TATARUS-Projekts mit Nervensystem, Gewebe, Physiologie, Organen, visueller Sensorik, IMAGINATIO, Kartografie und optionalem Hybrid Cortex. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Architektur in einem Satz TATARUS verbindet Sensorik → Nervensystem → Körperzustand → Gedächtnis → Motorik → Umweltkonsequenz zu einem persistenten geschlossenen Kreislauf. Die Übersicht zeigt die tatsächlichen Hauptmodule und ihre gerichteten Kopplungen. Kernmodule Modul Zustand / Funktion Öffentliche Schnittstelle Neural Network Neuronen, Dendriten, Rezeptoren, Synapsen, Axone, Assemblies, Motorpopulationen RobotMind Tissue 3D-Zellgeometrie, Glia, Kapillaren, Myelin, Mechanik über RobotMind-Telemetrie Physiology Na⁺, K⁺, Ca²⁺, Cl⁻, ATP, Pumpen, Neuromodulatoren, CREB/Protein, Schlaf PhysiologyTelemetry Synthetic Organism Herz, Kreislauf, Lunge, zwei Nieren, Autonomik, Interozeption SyntheticOrganism Visual Pathway Ocular/Vestibular, Retina, Sehnerv, V1/V2, ventraler Strom, Objekthypothesen OcularSystem , VestibularSystem , VisualPathway IMAGINATIO visuelle Erfahrung, Self-Imprint, Engramme, Recall, Kategorien, Szenen, Prospektion VisualImagination Cartography Scannerfusion und persistente Umweltkarte RobotMind::integrateScan Hybrid Cortex optionale lokale LLM-Beratung und Executive-Funktionen unter Arbiter-Kontrolle CortexOrchestrator Wichtige Systemgrenze Der optionale Cortex ersetzt den TATARUS-Kern nicht. Er erhält abstrahierte Zustände und darf Strategien, Informationsanforderungen, Imagination oder Pläne vorschlagen. Direkte Belohnungsmanipulation, direkte Motorsteuerung sowie direkte Mutation von Physiologie, Neuronen oder Synapsen sind im Sicherheitsvertrag de"
  },
  {
    "title": "IMAGINATIO UI · Bedienungsanleitung",
    "url": "manuals/tatarus_imaginatio_ui_bedienungsanleitung.html",
    "group": "Handbuch",
    "text": "IMAGINATIO UI · Bedienungsanleitung · TATARUS Zum Inhalt Technische Dokumentation IMAGINATIO UI · Bedienungsanleitung Praxisanleitung für Wahrnehmung, Training, Gedächtnisabruf, Symbolik, Kategorien, Szenen, Zukunft und Zielraum-Export. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 1. Bild laden Im Bereich „Farbreferenz / Cue“ wird eine RGB-Referenz geladen. Die Referenz ist der sensorische Reiz für die Lernphase. Bildanpassung steuert, wie die externe Quelle in den Arbeitsraum überführt wird. 2. Sehen & Nachzeichnen TATARUS erhält die Referenz sichtbar, verarbeitet sie über OcularSystem und VisualPathway und führt die Malspur seriell aus. Nach Abschluss wird der eigene fertige Canvas erneut wahrgenommen und als Self-Imprint mit dem Engramm gekoppelt. 3. Aus dem Gedächtnis Nach Entfernen/Verbergen der Referenz wird das gelernte Engramm reaktiviert. Die UI zeigt dabei keinen Quellraster als Recall-Quelle. Entscheidend sind Engramm, Assembly und Self-Motor-Gedächtnis. 4. Symbol und Komposition Symbole können an gelernte visuelle Zustände gebunden werden. Mehrere Symbole lassen sich über compose zu einer neuen Malaufgabe kombinieren. 5. Kategorie-Fusion Kategoriebeispiele aktualisieren laufende Shape-/Feature-Statistiken. Die Kategorie enthält keine Sammlung persistenter Vollbild-Rasteranker. Fusion kombiniert die gelernten strukturellen Faktoren und erzeugt eine neue Zielinstanz. 6. Relationsszene Objektinstanzen besitzen Kategorie, Pose und räumliche Parameter. Relationen umfassen links/rechts, über/unter, vor/hinter, innerhalb/enthält, berührend, überlappend, nah/fern, Blickbezug und Verbindung. 7. Zukunft Gelernte Szenenaktionen und Übergänge werden in prospektive Folgezustände überführt. Unterstützte Aktionsarten umfassen Move, Kick, Push, Pull, Fall, Ris"
  },
  {
    "title": "UI-Handbuch",
    "url": "manuals/ui-handbuch.html",
    "group": "Handbuch",
    "text": "UI-Handbuch · TATARUS Zum Inhalt Technische Dokumentation UI-Handbuch Bedienung der Live-Oberfläche für Nervensystem, synthetischen Organismus, Explorer/Kartografie, IMAGINATIO und Hybrid Cortex. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Startpunkte START_TATARUS.bat startet die integrierte Live-Oberfläche. START_IMAGINATIO_LAB.bat startet das visuelle Labor separat. Die Oberfläche ist Beobachtungs- und Bedienebene; sie ersetzt nicht die C++-Laufzeit. Organismusansicht Die Organansichten stellen Gehirn, Gesamtorganismus, Herz, Lunge und Nieren als erklärende Live-Geometrien dar. Telemetrie zeigt unter anderem Puls, MAP, SaO₂, Herzzeitvolumen, Atemfrequenz, GFR, Urinausscheidung, ATP und Distress. Die 3D-Darstellung ist keine anatomische Mikrosimulation. Neuronale Ansicht Neuronale Aktivität, Assemblies, Synapsen, Myelin, Dendriten-Spikes, ATP/Schlafdruck, Motorpopulationen und Prospektion werden aus dem aktuellen Laufzustand gelesen. Darstellungsoptionen dürfen den Simulationszustand nicht verändern. IMAGINATIO Die sieben Arbeitsbereiche führen vom Nachzeichnen über Recall, Symbol, Komposition und Kategorie-Fusion bis zu Relationsszene und Zukunft. Bei sichtbarer Referenz zeigt der erste Modus die demonstrierte Lernhandlung. Recall arbeitet ohne sichtbare Referenz aus Engrammen. Die Zielauflösung des Export-Renderers kann unabhängig von der internen Arbeitsauflösung gewählt werden. Hybrid Cortex Die Cortex-Sektion zeigt LM-Status, Modus, Ziel/Thought, Priorität, Metakognition, Executive-Ziel und letzte Entscheidung. Die Schaltflächen lösen Analyse, Imagination, Hybrid- oder Executive-Anfragen aus. Die Arbiter- und Safety-Grenzen bleiben unabhängig von der UI aktiv. Kartografie Explorer-Scanframes werden in die persistente Umweltkarte integriert. Die Kar"
  },
  {
    "title": "Validierung",
    "url": "manuals/validierung.html",
    "group": "Handbuch",
    "text": "Validierung · TATARUS Zum Inhalt Technische Dokumentation Validierung Teststrategie für deterministische Fortsetzung, Kausalität, Organe, visuelle Verarbeitung, IMAGINATIO, Cortex und Integrationsgrenzen. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Build-Qualitätsgate Compilerwarnungen sind Buildfehler. Die CMake-Konfiguration erzwingt diese Regel global; die Kern-Targets aktivieren zusätzlich strenge Warnstufen für MSVC beziehungsweise GCC/Clang. Registrierte Tests Bei aktiviertem Shared Build, Cortex und verfügbarer Python-Laufzeit registriert CMake 17 Testprogramme bzw. Integrationsprüfungen. Bereich Registrierter Test SDK / RobotMind tatarus_sdk_tests IMAGINATIO tatarus_imaginatio_tests Sinnesorgane tatarus_sensory_organs_tests Objekt-Cortex tatarus_object_cortex_tests Zielraum-Render tatarus_native_render_tests Neurobiologie tatarus_neurobiology_tests Physiologie tatarus_physiology_tests Kreislauf tatarus_circulation_tests Herz tatarus_heart_tests Niere tatarus_kidney_tests Kausalkette tatarus_causal_validation_tests Cortex tatarus_cortex_tests Cortex Executive / Metakognition tatarus_cortex_phase12_15_tests Cortex UI Bridge tatarus_cortex_ui_bridge_tests Organismus C ABI tatarus_organism_tests Kartografie C ABI tatarus_cartography_c_tests Embodiment-Liveintegration tatarus_embodiment_tests Kausale Prüfungen Die Testbasis umfasst unter anderem Blutverlust → Blutvolumen/MAP/Sympathikus/Renin, Hypoxie → PaO₂/Chemorezeptor/Atemantwort, Hyperkapnie → PaCO₂/pH/Ventilation, renale Hypoperfusion → GFR/Macula-densa/Renin und mechanische Last → O₂-Bedarf/Ventilation/Herzlast. Weitere Tests prüfen Marsatmosphäre, Körperversorgung des Nervensystems und Bilanzierung. Snapshot-Prüfungen Tests prüfen deterministische Fortsetzung nach Restore. IMAGINATIO prüft Farb"
  },
  {
    "title": "Wissenschaftliche Einordnung & Verantwortung",
    "url": "manuals/vision-verantwortung.html",
    "group": "Handbuch",
    "text": "Wissenschaftliche Einordnung & Verantwortung · TATARUS Zum Inhalt Technische Dokumentation Wissenschaftliche Einordnung & Verantwortung Was TATARUS behauptet, was der Code tatsächlich zeigt und welche biologischen, medizinischen und KI-bezogenen Grenzen ausdrücklich bestehen. Aktueller Projektstand Code-geprüfte Dokumentation GPL-3.0 Synthetischer Organismus als Funktionsarchitektur TATARUS verwendet biologische Organisationsprinzipien: Organsysteme, Homöostase, Interozeption, sensorische Transduktion, neuronale Aktivität, Plastizität, Gedächtnis, Prospektion und Handlung sind als rückgekoppelter Softwareorganismus angeordnet. „Synthetisch biologisch“ beschreibt diese funktionale Architektur; es bedeutet nicht, dass C++-Zustände biologisches Gewebe wären. Messbare Zustände statt mystischer Grenze Auch biologische Organismen tragen Information in messbaren physikalischen Zuständen. Bei TATARUS sind diese Zustände digital repräsentiert. Die wissenschaftlich relevante Frage ist daher nicht „Zahl oder keine Zahl“, sondern welche Zustände existieren, wie sie gekoppelt sind und welche kausalen Konsequenzen sie haben. Bildgedächtnis IMAGINATIO konserviert die Eingangsdatei nicht als separate Bildkopie im Snapshot. Der visuelle Reiz verändert perceptuelle und motorische Engrammzustände; Recall entsteht aus diesem Zustand. Visuelle Ähnlichkeit zum Lernreiz ist gewollt und sagt allein nichts darüber aus, ob ein Ergebnis durch simples Resizing entstanden ist. Keine unzulässigen Behauptungen Keine Behauptung eines menschlichen Bewusstseins. Keine Behauptung vollständiger biologischer Gleichheit. Keine medizinische Diagnostik oder Therapie. Keine Behauptung, synthetisierte 4K-Details seien die unbekannte hochauflösende Originalwahrheit. Keine Behauptung, ein Forensikvergleich könne "
  },
  {
    "title": "Sitemap",
    "url": "site-map.html",
    "group": "Dokumentation",
    "text": "Sitemap · TATARUS T TATARUS UNIFIED DOCUMENTATION Dokumentationsstruktur Sitemap Alle Seiten des aktuellen veröffentlichten Dokumentationssatzes. Handbücher Projekt Handbuch System Handbuch Handbuch Handbuch Organismus Handbuch IMAGINATIO Handbuch Cortex Handbuch UI Handbuch API Handbuch Integration Handbuch Validierung Handbuch Benchmark Handbuch Einordnung Handbuch Architektur Systemarchitektur Wiki SyntheticOrganism Wiki RobotMind Wiki Neural Network Wiki Tissue und Glia Wiki Physiology Wiki Herz, Kreislauf, Lunge, Nieren Wiki Interozeption und Autonomik Wiki Prospektion Wiki Persistentes Gedächtnis Wiki neural_direct Wiki Embodiment und Robotertraining Wiki Explorer-Kartografie Wiki IMAGINATIO Grundidee Wiki Wahrnehmung Wiki Lernen und Self-Imprint Wiki Recall, Symbol, Komposition Wiki Kategorie-Fusion Wiki 512×512-Fototest Wiki Zielraum-Rendering Wiki Relationsszene und Zukunft Wiki Cortex Hybrid Cortex & Executive Wiki Betrieb & Technik Persistenz und Snapshots Wiki Kausallabor und Interventionen Wiki Determinismus und Validierung Wiki Live-UI und Telemetrie Wiki API und Integration Wiki Sensor-Aktions-Zyklus Wiki Motorik und Safety Wiki Schlaf und Konsolidierung Wiki Gewebewachstum und Mechanik Wiki Identitätsengramme Wiki Skalierung und Throughput Wiki Technische Belege im Code Wiki Forschung & Einordnung Grundidee und Systemgrenze Wiki Wissenschaftliche Position Wiki Forschungsgrenzen und offene Fragen Wiki Grenzen und Sicherheit Wiki TATARUS als Forschungsprogramm Wiki Glossar Wiki"
  },
  {
    "title": "API und Integration",
    "url": "wiki/API-und-Integration.html",
    "group": "Wiki",
    "text": "API und Integration · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code API und Integration Öffentliche Integrationsgrenzen sind die C++-Header unter include/tatarus , der Organismus unter modules/tatarus_organism und die C ABI include/tatarus/c_api.h . Grundsatz Hostanwendungen sollen Erfahrungen einspeisen, Telemetrie lesen und Aktionen/Outcomes über definierte APIs schließen. Interne Container und Snapshot-Binärformate sind keine Host-ABI. Module RobotMind, SyntheticOrganism, VisualImagination und CortexOrchestrator können gezielt eingebettet werden. Der Cortex ist optional; der Kern funktioniert ohne lokales Sprachmodell."
  },
  {
    "title": "Hybrid Cortex & Executive",
    "url": "wiki/Cortex-Hybrid-Executive.html",
    "group": "Wiki",
    "text": "Hybrid Cortex & Executive · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Hybrid Cortex & Executive Der optionale Cortex verbindet TATARUS mit einem lokalen Sprachmodell, ohne die Autorität des Nervensystems zu ersetzen. Modi Disabled, ObserveOnly, Advisor, Imagination und Hybrid; Ausführung Off, Live, Record oder Replay. Arbiter und Safety Der Arbiter bewertet Modellantworten gegen Zustand, Fähigkeiten und Sicherheitsvertrag. Direkter Reward, direkte Motorsteuerung sowie direkte Mutation von Physiologie, Neuronen und Synapsen sind deaktiviert. Executive Ziele, Working Memory, mehrschrittige Pläne, Metakognition und autonome Zyklen sind persistent verwaltbar. Planfortschritt wird durch reale ActionOutcome -Ereignisse geerdet. Imagination und Schlaf Der Cortex kann IMAGINATIO-Direktiven vorschlagen und in Schlafzyklen Dream-Aufgaben anstoßen. Die resultierenden Zustände bleiben den TATARUS-Grenzen unterworfen."
  },
  {
    "title": "Determinismus und Validierung",
    "url": "wiki/Determinismus-und-Validierung.html",
    "group": "Wiki",
    "text": "Determinismus und Validierung · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Determinismus und Validierung Determinismus ist eine Architekturvorgabe: gleiche Startzustände, Seeds und Eingaben sollen reproduzierbare Zustandsfolgen liefern. Snapshots Tests prüfen exakte Fortsetzung nach Save/Load für Nervensystem und gekoppelte Systeme. Warnings as Errors Compilerwarnungen sind Fehler. Das Projekt aktiviert Warning-as-Error im Buildsystem und strenge Warnstufen für die Kern-Targets. Testbereiche SDK, IMAGINATIO, Sinnesorgane, Objektpfad, Zielraum-Render, Neurobiologie, Physiology, Kreislauf, Herz, Nieren, Kausalvalidierung, Cortex, Organismus/C-ABI, Kartografie und Embodiment sind durch registrierte Tests abgedeckt."
  },
  {
    "title": "Embodiment und Robotertraining",
    "url": "wiki/Embodiment-und-Robotertraining.html",
    "group": "Wiki",
    "text": "Embodiment und Robotertraining · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Embodiment und Robotertraining Embodiment bedeutet in TATARUS, dass Entscheidungen Konsequenzen in einer externen Welt haben und diese Konsequenzen als neue Erfahrung zurückkehren. Vertrag Experience → RobotMind → MotorTelemetry → ActionEvent externe Welt führt aus ActionOutcome → RobotMind → Plastizität / Gedächtnis Das verhindert, dass ein interner Plan sich selbst als Erfolg bestätigen kann. Mit Organismus Über SyntheticOrganism wird dieselbe Schleife um Atmosphäre, mechanische Last, Herz/Kreislauf/Lunge/Nieren und Interozeption ergänzt."
  },
  {
    "title": "Forschungsgrenzen und offene Fragen",
    "url": "wiki/Entwicklungshorizont.html",
    "group": "Wiki",
    "text": "Forschungsgrenzen und offene Fragen · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Forschungsgrenzen und offene Fragen Diese Seite bündelt offene Forschungsfragen, die sich aus dem veröffentlichten System und seinen messbaren Zuständen ergeben. Offene Fragen Wie weit skaliert persistente Plastizität bei deutlich größeren Netzen? Wie stabil bleiben Engramme bei langer autonomer Laufzeit? Wie stark dürfen Top-down-Priors werden, ohne Bottom-up-Wahrnehmung zu verdrängen? Welche Organ- und Interozeptionskopplungen verändern Lernverhalten kausal und reproduzierbar? Wie lässt sich visuelle Rekonstruktion objektiv von klassischen Transformationspipelines abgrenzen? Wie können Cortex-Vorschläge weiter genutzt werden, ohne Motor-/Reward-Hoheit zu verwischen? Messprinzip Neue Experimente sollten mit deterministischen Seeds, klaren Interventionen, Snapshots und expliziten Gegenkontrollen formuliert werden."
  },
  {
    "title": "Explorer-Kartografie",
    "url": "wiki/Explorer-Kartografie.html",
    "group": "Wiki",
    "text": "Explorer-Kartografie · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Explorer-Kartografie Explorer erweitert den Sensor-Aktions-Zyklus um räumliche Zielinformation und Scannerframes. integrateScan aktualisiert eine persistente Umweltkarte. Rollen Neuronale Place-/Action-Erfahrung und explizite Kartografie sind getrennte, aber gekoppelte Repräsentationen. Die Karte kann vom Host visualisiert werden; die Motorentscheidung bleibt im TATARUS-Lernkreis."
  },
  {
    "title": "TATARUS als Forschungsprogramm",
    "url": "wiki/Forschungsprogramm.html",
    "group": "Wiki",
    "text": "TATARUS als Forschungsprogramm · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code TATARUS als Forschungsprogramm TATARUS dient als gemeinsame Plattform für Experimente, die normalerweise getrennt untersucht werden: spikende Plastizität, räumliches Gedächtnis, Körperhomöostase, visuelle Wahrnehmung, sensomotorisches Lernen, Identität, Prospektion und autoritätsbegrenzte Sprachmodellberatung. Methodik Die bevorzugte Einheit ist der geschlossene Versuch: definierter Startzustand → definierter Reiz/Intervention → beobachtbare Zustandsänderung → Handlung → messbare Konsequenz → Snapshot/Replay/Validierung."
  },
  {
    "title": "Gewebewachstum und Mechanik",
    "url": "wiki/Gewebewachstum-und-Mechanik.html",
    "group": "Wiki",
    "text": "Gewebewachstum und Mechanik · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Gewebewachstum und Mechanik Räumliche Zellgeometrie, Glia, Gefäße, Myelin und mechanische Zustände bilden die physische Schicht des synthetischen Nervengewebes. Kausalität Distanz beeinflusst Verbindungen und Laufzeiten; Versorgung und Energie koppeln Aktivität an Gewebe. Die Mechanik ist eine abstrahierte synthetische Gewebedynamik, kein anatomisches FEM-Modell."
  },
  {
    "title": "Glossar",
    "url": "wiki/Glossar.html",
    "group": "Wiki",
    "text": "Glossar · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Glossar Kernbegriffe Begriff Bedeutung im Projekt Assembly gemeinsam aktive neuronale Population, die als Zustands-/Gedächtnisträger dienen kann Engramm persistenter gelernter Zustand, der spätere Wiedererkennung oder Handlung ermöglicht Self-Imprint erneute Wahrnehmung des von TATARUS selbst erzeugten fertigen Canvas und Bindung an Self-/Motorzustände Self-Motor-Gedächtnis im VisualEngramm persistierte eigene Malhandlung des Systems Interozeption verdichtete Wahrnehmung des inneren Körperzustands Prospektion gelernte Erwartung zeitlicher Nachfolgezustände VisualPathway Retina/Sehnerv/V1/V2-/Objekt-/Ventral-Verarbeitung OcularSystem Pupille, Adaptation, Blick und Sakkadensteuerung VOR vestibulo-okulärer Reflexsignalpfad zur Blickstabilisierung Kategorieengramm laufende Shape-/Feature-Statistik mit Assemblies; kein Vollbild-Rasteranker-Speicher CortexArbiter Prüfinstanz zwischen optionalem Sprachmodell und TATARUS-Aktions-/Safety-Grenze ActionOutcome extern gemeldete Konsequenz einer ausgeführten Aktion Begriff „synthetischer Organismus“ Bezeichnet die gekoppelte Softwarearchitektur aus Nervensystem, Körperorganen, Sensorik, Homöostase, Gedächtnis und Handlung. Der Begriff behauptet kein biologisches Gewebe."
  },
  {
    "title": "Grenzen und Sicherheit",
    "url": "wiki/Grenzen-und-Sicherheit.html",
    "group": "Wiki",
    "text": "Grenzen und Sicherheit · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Grenzen und Sicherheit TATARUS ist Forschungssoftware. Körpermodelle sind keine medizinische Software, IMAGINATIO ist kein Beweis menschlicher Vorstellungskraft und der Cortex ist kein autonomer Ersatz für die Kernsteuerung. Cortex Safety Direkter Reward, direkte Motorsteuerung, direkte Physiologiemutation sowie direkter Neuronen-/Synapsenzugriff sind deaktiviert. Experimentelle Interventionen Damage-, Blutungs-, Infusions- und Atmosphärenexperimente gelten nur innerhalb der Simulation. Ergebnisse dürfen nicht als medizinische Handlungsanweisung interpretiert werden."
  },
  {
    "title": "Grundidee und Systemgrenze",
    "url": "wiki/Grundidee-und-Systemgrenze.html",
    "group": "Wiki",
    "text": "Grundidee und Systemgrenze · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Grundidee und Systemgrenze Die Forschungsfrage lautet: Kann ein synthetischer Softwareorganismus so organisiert werden, dass Wahrnehmung, Körperzustand, Nervensystem, Lernen, Gedächtnis und Handlung einen gemeinsamen kausalen Kreislauf bilden? Was innerhalb der Grenze liegt Neurale Dynamik, Gewebe, Physiology, Organphysiologie, Sinnesorgane, Interozeption, Prospektion, Gedächtnis, Motorik, IMAGINATIO und optional autoritätsbegrenzte Cortex-Beratung. Was außerhalb liegt Biologisches Gewebe, klinisch validierte Humanphysiologie, Bewusstseinsnachweis und die Behauptung, dass synthetische Zustände identisch mit denen eines Menschen sind."
  },
  {
    "title": "Herz, Kreislauf, Lunge und Nieren",
    "url": "wiki/Herz-Kreislauf-Lunge-Nieren.html",
    "group": "Wiki",
    "text": "Herz, Kreislauf, Lunge und Nieren · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Herz, Kreislauf, Lunge und Nieren Die Organe sind keine UI-Dekoration, sondern dynamische Module im Organismus-Schritt. Herz Vier Kammern und vier Klappen koppeln Erregungsleitung, Calcium, aktive Spannung und Blutfluss. SA-/AV-Knoten und Purkinje-Zustände bilden die elektrische Sequenz. Lunge Atemmechanik, alveolärer O₂/CO₂-Austausch und Chemorezeptorsteuerung koppeln Atmosphäre an Blutgase. Kreislauf Systemische und regionale Kompartimente transportieren Gase, Glukose, Elektrolyte und Metabolite; Endokrinsignale schließen die Homöostaseschleife. Zwei Nieren Linke und rechte Niere werden separat fortgeschrieben. Nephronsegmente, GFR, Rückresorption, Exkretion, Renin, Aldosteron und Vasopressin koppeln Volumen- und Elektrolythaushalt."
  },
  {
    "title": "IMAGINATIO – 512×512-Fototest",
    "url": "wiki/IMAGINATIO-512-Fototest.html",
    "group": "Wiki",
    "text": "IMAGINATIO – 512×512-Fototest · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code IMAGINATIO – 512×512-Fototest Der 512×512-RGB-Test ist ein praktisches Experiment für visuelle Engramme: komplexe Bilder werden als Lernreize angeboten, selbst nachgezeichnet, per Self-Imprint konsolidiert und anschließend ohne sichtbare Referenz abgerufen. Was geprüft wird visuelle Ähnlichkeit des Recalls, Persistenz nach Snapshot/Restore, korrekte Referenzfreiheit beim Recall, Zielraum-Rendering in höherer Auflösung. Was nicht behauptet wird Der Test beweist keine biologische Gleichheit des Gedächtnisses. Er zeigt, dass der implementierte synthetische Wahrnehmungs-/Motor-/Engrammpfad komplexe RGB-Erfahrungen persistent rekonstruieren kann."
  },
  {
    "title": "IMAGINATIO – Grundidee",
    "url": "wiki/IMAGINATIO-Grundidee.html",
    "group": "Wiki",
    "text": "IMAGINATIO – Grundidee · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code IMAGINATIO – Grundidee IMAGINATIO macht eine Zeichenfläche zur Umwelt eines sensomotorischen TATARUS-Laufs. Ein Bild ist zunächst ein visueller Reiz, kein zu transformierendes Dateiformat. Lernen statt Resize TATARUS sieht, führt eine Malhandlung aus, nimmt das eigene Ergebnis erneut wahr und manifestiert diese Erfahrung als Engramm. Später wird aus diesem Zustand erinnert und gezeichnet. Ziel Eine gute Erinnerung darf dem Lernreiz visuell sehr ähnlich sein. Entscheidend ist, dass der Recall aus dem persistenten TATARUS-Zustand entsteht und nicht durch erneutes Laden einer separaten Originalbildkopie."
  },
  {
    "title": "IMAGINATIO – Zielraum-Rendering",
    "url": "wiki/IMAGINATIO-Hochaufloesende-Quellen.html",
    "group": "Wiki",
    "text": "IMAGINATIO – Zielraum-Rendering · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code IMAGINATIO – Zielraum-Rendering Quellauflösung und Zielauflösung werden getrennt behandelt. SourceResolutionMemory merkt Breite, Höhe und Beobachtungszahl, nicht das Quellbild. Direkter Zielraum renderLastTargetSpace akzeptiert Zielachsen von 8 bis 4096 Pixeln bei maximal 16.777.216 Zielpixeln. Der interne Working Canvas wird nicht als Raster-Resize-Quelle verwendet. Geometrie und Pigment Motorpositionen werden normalisiert auf den Zielraum abgebildet. Gelernte Pigment- und Kantenvariation steuert zusätzliche deterministische Feinstruktur, wenn das Ziel größer als der Arbeitsraum ist. Wahrheitsgrenze Synthetisierte Hochfrequenzdetails sind eine TATARUS-Rekonstruktion. Sie werden nicht als wiedergewonnene unbekannte Details eines nie beobachteten hochauflösenden Originals bezeichnet."
  },
  {
    "title": "IMAGINATIO – Kategorie-Fusion",
    "url": "wiki/IMAGINATIO-Kategorie-Fusion.html",
    "group": "Wiki",
    "text": "IMAGINATIO – Kategorie-Fusion · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code IMAGINATIO – Kategorie-Fusion Eine Kategorie ist kein Ordner mit gespeicherten Bildern. Sie akkumuliert laufende Statistiken über ShapeSignature und ObjectPartModel sowie zugeordnete Assemblies. Laufende Statistik Mittelwerte und Varianzakkumulatoren halten Form- und Featureverteilung. Es gibt keinen persistenten Vollbild-Rasteranker-Pool. Fusion drawFromCategory und fuseCategories erzeugen Zielinstanzen aus strukturellen Faktoren. Das System kann dadurch Kategorieeigenschaften kombinieren, ohne vollständige Trainingsbilder übereinanderzulegen."
  },
  {
    "title": "IMAGINATIO – Lernen und Self-Imprint",
    "url": "wiki/IMAGINATIO-Lernen.html",
    "group": "Wiki",
    "text": "IMAGINATIO – Lernen und Self-Imprint · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code IMAGINATIO – Lernen und Self-Imprint Beim demonstrierten Lernen ist die Referenz transient sichtbar. Die Malaktionen werden seriell durch den Aktuator ausgeführt. Self-Imprint Nach erfolgreicher Malhandlung wird nicht einfach die Referenz als Gedächtnis übernommen. TATARUS betrachtet den eigenen fertigen Canvas erneut über OcularSystem und VisualPathway und bindet daraus Self-Fingerprint, Self-Shape, Self-Feature-Modell, Self-Assembly und Self-Motor-Gedächtnis. Persistenzgrenze Quellraster, Teacher-Patch-Trace und Working Canvas werden nicht als persistentes visuelles Gedächtnis geschrieben. Das Self-Motor-Gedächtnis gehört dagegen zum Engramm."
  },
  {
    "title": "IMAGINATIO – Recall, Symbol und Komposition",
    "url": "wiki/IMAGINATIO-Recall-Symbol-Komposition.html",
    "group": "Wiki",
    "text": "IMAGINATIO – Recall, Symbol und Komposition · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code IMAGINATIO – Recall, Symbol und Komposition Recall reaktiviert ein gelerntes VisualEngramm anhand eines Cues. Die Referenz muss dabei nicht sichtbar sein. Symbol associateSymbol bindet ein Symbol an eine visuelle Erinnerung; drawFromSymbol löst die entsprechende Malhandlung aus. Komposition compose kann mehrere Symbole zu einer kombinierten Zielaufgabe zusammenführen. Freies Zeichnen arbeitet ebenfalls aus bekannten Engrammzuständen statt aus einem externen Quellbild."
  },
  {
    "title": "IMAGINATIO – Relationsszene und Prospektion",
    "url": "wiki/IMAGINATIO-Relationsszene-und-Prospektion.html",
    "group": "Wiki",
    "text": "IMAGINATIO – Relationsszene und Prospektion · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code IMAGINATIO – Relationsszene und Prospektion IMAGINATIO kann mehrere Objektinstanzen mit Kategorie, Pose, Position, Skalierung, Rotation und Tiefe als Szene beschreiben. Relationen Unterstützt werden LeftOf, RightOf, Above, Below, InFrontOf, Behind, Inside, Contains, Touching, Overlapping, Near, Far, LookingAt und ConnectedTo. Aktionen und Zukunft Gelernte Szenenaktionen umfassen Move, Kick, Push, Pull, Fall, Rise, Approach, Depart und Custom. Übergänge können in mehrere prospektive Szenenzustände fortgeschrieben werden."
  },
  {
    "title": "IMAGINATIO – Wahrnehmung",
    "url": "wiki/IMAGINATIO-Wahrnehmung.html",
    "group": "Wiki",
    "text": "IMAGINATIO – Wahrnehmung · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code IMAGINATIO – Wahrnehmung Der visuelle Pfad beginnt vor dem „neuronalen Bildfeature“: OcularSystem und VestibularSystem erzeugen einen zustandsabhängigen retinalen Input. Auge und Blick Pupillendurchmesser, Luminanz, Hell-/Dunkeladaptation, gaze yaw/pitch und Sakkadenziele werden geführt. VOR-Geschwindigkeit aus dem Vestibularsystem kann die Augenbewegung kompensieren. Retina bis Objektpfad Photorezeptor-RGB/Luminanz → ON/OFF- und farbopponente Ganglionantworten → begrenzte Sehnervereignisse → V1-Orientierungszellen → V2-/Objektteile → ventrale Aktivierung und Objektkandidaten. Top-down Ein begrenzter Top-down-Prior kann erwartete Objektteile modulieren; seine Verstärkung ist konfiguriert und darf den Bottom-up-Reiz nicht unbegrenzt überschreiben."
  },
  {
    "title": "Identitätsengramme",
    "url": "wiki/Identitaetsengramme.html",
    "group": "Wiki",
    "text": "Identitätsengramme · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Identitätsengramme Identity verarbeitet persistente Identitätsbeobachtungen unabhängig von IMAGINATIO-Kategorien. observeIdentity erzeugt eine Entscheidung; confirmLastIdentity liefert nachträgliches Feedback. Prinzip Identität wird als wiederkehrende Erfahrung im Mind gespeichert, nicht als UI-Label allein. Die genaue Sensorrepräsentation liegt in der Identity-Schnittstelle und den persistenten Engrammen."
  },
  {
    "title": "Interozeption und Autonomik",
    "url": "wiki/Interozeption-und-Autonomik.html",
    "group": "Wiki",
    "text": "Interozeption und Autonomik · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Interozeption und Autonomik Interozeption ist die innere Sensorik des synthetischen Organismus. Sie bündelt Kreislauf-, Gas-, Stoffwechsel-, Elektrolyt- und Organstresszustände. Signale Arterieller/venöser Druck, Herzlast, Herzfrequenz, PaO₂, PaCO₂, pH, Glukose, Natrium, Kalium, Calcium, Osmolarität, Temperatur, renaler Stress, Hypoxie, Hyperkapnie, viszeraler und metabolischer Stress werden geführt. Autonome Rückkopplung Sympathische/parasympathische Töne und Barorezeptor-Afferenz reagieren auf den Körperzustand und beeinflussen die Organdynamik. Das Ergebnis fließt wieder in den Mind."
  },
  {
    "title": "Kausallabor und Interventionen",
    "url": "wiki/Kausallabor-und-Interventionen.html",
    "group": "Wiki",
    "text": "Kausallabor und Interventionen · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Kausallabor und Interventionen Das Kausallabor verändert definierte Zustände und beobachtet die Richtung der Folgereaktionen. Beispiele Blutverlust → Blutvolumen/MAP → autonome Antwort → Renin, Hypoxie → PaO₂ → Chemorezeptoren → Ventilation/Distress, Hyperkapnie → PaCO₂/pH → Atemantwort, renale Hypoperfusion → GFR/Macula densa → Renin, mechanische Last → metabolische Nachfrage → Ventilation/Herzlast. Zweck Die Interventionen dienen reproduzierbaren Kausaltests des synthetischen Organismus, nicht medizinischen Vorhersagen für Menschen."
  },
  {
    "title": "Live-UI und Telemetrie",
    "url": "wiki/Live-UI-und-Telemetrie.html",
    "group": "Wiki",
    "text": "Live-UI und Telemetrie · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Live-UI und Telemetrie Die Live-UI beobachtet Nervensystem, Organismus, Kartografie, IMAGINATIO und Cortex, ohne die Kernlogik zu ersetzen. Telemetrie Live-Zustände umfassen neuronale Aktivität, Assemblies, Synapsen, Myelin, ATP/Schlafdruck, Motorik, MAP, SaO₂, Herzzeitvolumen, Atemfrequenz, GFR, Interozeption und Throughput. Darstellung vs. Simulation Rendering und Simulationsschritt sind getrennt. Visuelle Färbung, Kamerazustand oder Anzeige-Lod sollen den Lernzustand nicht verändern. Cortex UI Status, Modus, Ziel, Metakognition, letzte Entscheidung und Executive-Plan werden als Beobachtungs-/Steueroberfläche angezeigt; Safety bleibt im Backend erzwungen."
  },
  {
    "title": "Motorik und Safety",
    "url": "wiki/Motorik-und-Safety.html",
    "group": "Wiki",
    "text": "Motorik und Safety · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Motorik und Safety Motorik wird aus TATARUS-Populationen und Aktuatorzuständen abgeleitet. Externe Aktionen werden durch den Host ausgeführt. Action Outcome Erfolg und Konsequenz werden erst über ActionOutcome zurückgeführt. Das ist die zentrale Erdung gegen selbstbestätigte Pläne. Cortex-Grenze Der optionale Cortex darf keine Motoraktion direkt ausführen. Er schlägt Strategien oder Pläne vor; Arbiter und Host behalten die Ausführungsgrenze."
  },
  {
    "title": "neural_direct",
    "url": "wiki/Neural-Direct.html",
    "group": "Wiki",
    "text": "neural_direct · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code neural_direct neural_direct bezeichnet den direkten Zugriff auf den TATARUS-Nervensystempfad ohne eine externe generative Modellschicht. Sensoren, neuronale Dynamik, Gedächtnis und Motorik bleiben vollständig im Kernsystem. Einsatz Der Pfad eignet sich für deterministische Tests, Robotik, Kausalexperimente und als Referenz gegenüber dem optionalen Cortex. Grenze „Direkt“ bedeutet nicht ungeprüften Zugriff auf interne Speicherstrukturen. Öffentliche C++-/C-Schnittstellen und definierte Interventionen bleiben die Systemgrenze."
  },
  {
    "title": "TATARUS Neural Network",
    "url": "wiki/Neural-Network.html",
    "group": "Wiki",
    "text": "TATARUS Neural Network · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code TATARUS Neural Network Das Neural-Network-Modul bildet spikende neuronale Dynamik mit Dendriten, Rezeptoren, Synapsen, Axonen, Leitungsverzögerungen, Eligibility-Spuren, Plastizität, Assemblies und Motorpopulationen ab. Motorpopulationen Vier Richtungs-Populationen liefern Nord/Ost/Süd/West-Aktivität; zusätzliche Telemetrie beschreibt Bewegung, Aufmerksamkeit, Vokalisation und Confidence. Biologische Einordnung Das Modell ist biologisch inspiriert und mechanistisch, nicht biophysikalisch vollständig. Entscheidend ist die kausale Kopplung zu Gewebe, Physiologie, Körper und Erfahrung."
  },
  {
    "title": "Persistentes Gedächtnis",
    "url": "wiki/Persistentes-Gedaechtnis.html",
    "group": "Wiki",
    "text": "Persistentes Gedächtnis · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Persistentes Gedächtnis TATARUS besitzt mehrere persistente Gedächtnisebenen: Assemblies und Synapsen, räumliche Erfahrung, Identitätsengramme, Prospektion sowie visuelle, kategoriale, Objekt-, Pose-, Relations-, Aktions- und Szenenengramme. Visuelles Engramm Ein VisualEngram enthält perceptuelle Abstraktionen und Self-Imprint-Zustände einschließlich Self-Motor-Gedächtnis. Es ist Bestandteil des Nervensystemzustands und keine separate Originalbilddatei. Snapshot Snapshots sichern die persistenten Zustände. Transiente Sensorquellen und laufende Arbeitsdarstellungen gehören nicht automatisch zum Gedächtnis."
  },
  {
    "title": "Persistenz und Snapshots",
    "url": "wiki/Persistenz-und-Snapshots.html",
    "group": "Wiki",
    "text": "Persistenz und Snapshots · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Persistenz und Snapshots Snapshots sind Zustandsfortsetzungen, keine Exportarchive der Sensorquellen. RobotMind und Organismus Mind und Organismus sichern neuronale, physiologische, räumliche und gekoppelte Körperzustände über ihre Snapshot-APIs. IMAGINATIO Der IMAGINATIO-Snapshot schreibt Mind-Zustand und visuelle Engramme. Er speichert Visual-, Kategorie-, Objekt-, Pose-, Relations-, Aktions-, Szenen- und Auflösungszustände sowie Self-Imprint/Self-Motor-Memory. Nicht geschrieben werden ein separater Source-Raster, der Teacher-Trace oder die lebende Arbeitsleinwand. Restore Nach Restore soll der persistierte Zustand deterministisch weiterlaufen. Entsprechende Tests prüfen exakte Fortsetzung und Engrammabruf."
  },
  {
    "title": "TATARUS Physiology",
    "url": "wiki/Physiology.html",
    "group": "Wiki",
    "text": "TATARUS Physiology · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code TATARUS Physiology Die neuronale Physiologie führt Ionen- und Energiezustände statt eines zustandslosen Aktivierungsgraphen. Geführte Größen Na⁺, K⁺, Ca²⁺, Cl⁻, ATP, Pumpenaktivität, Kanalgradienten, Neuromodulatoren, CREB/Protein und Schlafhomöostase beeinflussen neuronale Erregbarkeit und Lernen. Körperkopplung Im SyntheticOrganism werden Körperversorgung und Interozeption an den Mind zurückgeführt. Dadurch kann physiologischer Stress den neuronalen Kontext verändern."
  },
  {
    "title": "Prospektion",
    "url": "wiki/Prospektion.html",
    "group": "Wiki",
    "text": "Prospektion · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Prospektion Prospektion lernt zeitliche Nachfolger von Assemblies und schätzt erwartete Aktivität, Timing, Vertrautheit und Prediction Error. Horizon RobotMind::predict() und predictHorizon(depth) stellen die prospektive Sicht bereit. IMAGINATIO besitzt zusätzlich Szenenübergänge für visuelle Zukunftszustände. Abgrenzung Prospektion ist keine externe Weltmodell-LLM-Funktion. Sie gehört zum persistenten TATARUS-Zustand."
  },
  {
    "title": "RobotMind",
    "url": "wiki/RobotMind.html",
    "group": "Wiki",
    "text": "RobotMind · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code RobotMind RobotMind ist die persistente Schnittstelle zum TATARUS-Nervensystem. Er nimmt Experience entgegen, integriert Explorer- und Identity-Signale, erzeugt Vorhersagen und stellt Kognition, Biologie, Physiologie, Prospektion und Motorik bereit. Action-Lifecycle beginAction , endAction und endEpisode trennen interne Aktionswahl von realer Konsequenz. Lernen kann explizit aktiviert/deaktiviert werden. Persistenz saveSnapshot und loadSnapshot sichern den Mind für deterministische Fortsetzung. Kartografie, Throughput und experimentelle Interventionen sind Teil der öffentlichen Integrationsoberfläche."
  },
  {
    "title": "Schlaf und Konsolidierung",
    "url": "wiki/Schlaf-und-Konsolidierung.html",
    "group": "Wiki",
    "text": "Schlaf und Konsolidierung · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Schlaf und Konsolidierung Schlafdruck und Schlafphasen sind Teil der neuronalen Physiologie. Ruhe- und Schlafprozesse wirken auf Homöostase und Gedächtniskonsolidierung. RobotMind rest(ticks) erlaubt gezielte Ruhephasen. Molekulare Plastizität, CREB/Protein und Energiezustand bleiben Teil des Prozesses. Cortex Dream Der optionale Cortex kann in geeigneten Schlafzyklen Dream-/Imagination-Aufgaben anstoßen. Das ist eine beratende Zusatzfunktion und ersetzt nicht die TATARUS-Konsolidierung."
  },
  {
    "title": "Der Sensor-Aktions-Zyklus",
    "url": "wiki/Sensor-Aktions-Zyklus.html",
    "group": "Wiki",
    "text": "Der Sensor-Aktions-Zyklus · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Der Sensor-Aktions-Zyklus Der Sensor-Aktions-Zyklus ist die gemeinsame Klammer für Robotik und IMAGINATIO. Sensorischer Zustand → neuronale Integration → Motorentscheidung → reale/Canvas-Handlung → messbare Konsequenz → neuer sensorischer Zustand → Lernen Warum das wichtig ist Der Organismus lernt nicht aus einem reinen Zielwert, sondern aus den Folgen seiner Handlung. Bei IMAGINATIO ist die veränderte Leinwand selbst wieder ein sensorischer Zustand."
  },
  {
    "title": "Skalierung und Throughput",
    "url": "wiki/Skalierung-und-Throughput.html",
    "group": "Wiki",
    "text": "Skalierung und Throughput · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Skalierung und Throughput Neuronenzahl, Synapsenzahl, Organphysiologie und Telemetriedichte bestimmen die Rechenlast. Throughput-Counter sind explizit lesbar und rücksetzbar. Messregel Core-Throughput und Live-/Telemetry-Throughput dürfen nicht gleichgesetzt werden. Die mitgelieferten Referenzdaten zeigen den Zusatzaufwand der Telemetrie. Buildprofile Konfigurationen können von kleinen deterministischen Testnetzen bis zu deutlich größeren Forschungsnetzen reichen; die öffentliche Konfiguration validiert die zulässigen Grenzen."
  },
  {
    "title": "SyntheticOrganism",
    "url": "wiki/SyntheticOrganism.html",
    "group": "Wiki",
    "text": "SyntheticOrganism · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code SyntheticOrganism SyntheticOrganism ist die Gehirn-Körper-Hülle des Projekts. Er besitzt RobotMind , Kreislauf, Herz, Lunge, zwei Nieren und ein Conservation Ledger. Schrittlogik step und stepWithLoad führen Erfahrung, Atmosphäre, mechanische Last und dt zusammen. Explorer-Varianten koppeln dieselbe Körperphysiologie an räumliche Exploration. Autonomik und Interozeption Sympathische und parasympathische Töne, Barorezeptor-Afferenz, Gas- und Stoffwechselzustände werden in Interozeption überführt und stehen dem Nervensystem als Körperkontext zur Verfügung. Interventionen Infusion, Blutverlust, adrenerge Stimulation, getrennte Nierenfunktion und experimentelle neuronale Schäden sind explizite Kausalwerkzeuge."
  },
  {
    "title": "Systemarchitektur",
    "url": "wiki/Systemarchitektur.html",
    "group": "Wiki",
    "text": "Systemarchitektur · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Systemarchitektur TATARUS ist als geschlossener Kreislauf aus Umwelt, Sinnesorganen, Nervensystem, Gewebe, Körperphysiologie, Gedächtnis, Prospektion und Handlung aufgebaut. Die Module sind im Code getrennt, ihr Laufzustand ist kausal gekoppelt. Hauptfluss Umwelt → Sinnesorgane → RobotMind ↔ Körperphysiologie → Motorik → Handlung → Umwelt ↕ Gedächtnis / Prospektion Für visuelle Aufgaben ersetzt ein Canvas die äußere Robotikumgebung, ohne das Grundprinzip zu ändern. Keine doppelte Intelligenz VisualImagination kann an einen existierenden RobotMind gebunden werden. SyntheticOrganism besitzt ebenfalls genau einen Mind. Der optionale Cortex ist Berater/Planer und erhält keine direkte Motor- oder Reward-Hoheit. Repository-Belege include/tatarus/robot_mind.hpp modules/tatarus_organism/tatarus_organism.hpp include/tatarus/imaginatio.hpp include/tatarus/cortex.hpp"
  },
  {
    "title": "Technische Belege im Code",
    "url": "wiki/Technische-Belege-im-Code.html",
    "group": "Wiki",
    "text": "Technische Belege im Code · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Technische Belege im Code Diese Seite verweist auf die Stellen, an denen die dokumentierten Aussagen im veröffentlichten Quellcode nachvollzogen werden können. Aussage Codebeleg Mind / Action-Lifecycle include/tatarus/robot_mind.hpp Gesamtorganismus modules/tatarus_organism/tatarus_organism.hpp Herz modules/tatarus_organism/tatarus_heart.hpp/.cpp Lunge modules/tatarus_organism/tatarus_lung.hpp/.cpp Nieren modules/tatarus_organism/tatarus_kidney.hpp/.cpp Kreislauf modules/tatarus_organism/tatarus_circulation.hpp/.cpp Auge/Blick include/tatarus/ocular_system.hpp Vestibular/VOR include/tatarus/vestibular_system.hpp Retina/Sehnerv/V1/V2/Object Cortex include/tatarus/visual_pathway.hpp , modules/tatarus_neurobiology/tatarus_visual_pathway.cpp IMAGINATIO include/tatarus/imaginatio.hpp , src/tatarus_imaginatio.cpp Cortex include/tatarus/cortex.hpp , modules/tatarus_cortex C ABI include/tatarus/c_api.h Tests tests/ , Registrierungen in CMakeLists.txt Dokumentationsregel Diese Dokumentation beschreibt den veröffentlichten Quellcode als alleinige technische Referenz. Aussagen sollen auf öffentliche Header, Implementierungen, Konfigurationen oder Tests zurückführbar sein."
  },
  {
    "title": "Tissue und Glia",
    "url": "wiki/Tissue-und-Glia.html",
    "group": "Wiki",
    "text": "Tissue und Glia · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Tissue und Glia Tissue gibt dem Nervensystem räumliche 3D-Geometrie. Zellpositionen, Regionen, Schichten und Hemisphären wirken auf Distanz, Leitung und Konnektivität. Versorgung Glia, Kapillaren und metabolische Versorgung koppeln neuronale Aktivität an Gewebezustände. Myelin beeinflusst Leitungscharakteristika; Gewebemechanik ergänzt die räumliche Dynamik. Beleg Die Implementierung liegt in src/tatarus_tissue.cpp ; Tests prüfen Gewebemechanik, Myelinisierung, Mikroglia und skalierte Konfigurationen."
  },
  {
    "title": "Wissenschaftliche Position",
    "url": "wiki/Wissenschaftliche-Position.html",
    "group": "Wiki",
    "text": "Wissenschaftliche Position · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Aktueller Code Wissenschaftliche Position TATARUS ist ein biologisch inspiriertes, mechanistisch-phänomenologisches Softwaresystem. Es untersucht Kopplungsprinzipien eines synthetischen Organismus. Biologische Analogie Biologische Zustände sind physikalisch messbar und können numerisch beschrieben werden; synthetische Zustände sind direkt digital repräsentiert. Daraus folgt weder Gleichheit noch Bedeutungslosigkeit: wissenschaftlich relevant sind Organisation, Dynamik und Kausalität. Prüfbarkeit Die Projektbehauptungen sollen an Code, Snapshots, Telemetrie und Tests zurückführbar sein. Über die implementierten Invarianten hinausgehende biologische Aussagen werden als Hypothese oder Analogie gekennzeichnet."
  },
  {
    "title": "TATARUS – System-Wiki",
    "url": "wiki/index.html",
    "group": "Wiki",
    "text": "TATARUS – System-Wiki · TATARUS Wiki T TATARUS UNIFIED DOCUMENTATION System-Wiki Code-geprüft TATARUS – System-Wiki Das Wiki beschreibt ausschließlich den veröffentlichten TATARUS-Projektstand: einen gekoppelten synthetischen Organismus mit Nervensystem, Körperphysiologie, Sinnesorganen, Gedächtnis, IMAGINATIO, Kartografie und optionalem Hybrid Cortex. Dokumentationsprinzip: Keine Entwicklungschronik. Jede Seite beschreibt, was der aktuelle Code tut und wo die Systemgrenzen liegen. Architektur Systemarchitektur Code-geprüfte Seite zum veröffentlichten System. SyntheticOrganism Code-geprüfte Seite zum veröffentlichten System. RobotMind Code-geprüfte Seite zum veröffentlichten System. Neural Network Code-geprüfte Seite zum veröffentlichten System. Tissue und Glia Code-geprüfte Seite zum veröffentlichten System. Physiology Code-geprüfte Seite zum veröffentlichten System. Herz, Kreislauf, Lunge, Nieren Code-geprüfte Seite zum veröffentlichten System. Interozeption und Autonomik Code-geprüfte Seite zum veröffentlichten System. Prospektion Code-geprüfte Seite zum veröffentlichten System. Persistentes Gedächtnis Code-geprüfte Seite zum veröffentlichten System. neural_direct Code-geprüfte Seite zum veröffentlichten System. Embodiment und Robotertraining Code-geprüfte Seite zum veröffentlichten System. Explorer-Kartografie Code-geprüfte Seite zum veröffentlichten System. IMAGINATIO Grundidee Code-geprüfte Seite zum veröffentlichten System. Wahrnehmung Code-geprüfte Seite zum veröffentlichten System. Lernen und Self-Imprint Code-geprüfte Seite zum veröffentlichten System. Recall, Symbol, Komposition Code-geprüfte Seite zum veröffentlichten System. Kategorie-Fusion Code-geprüfte Seite zum veröffentlichten System. 512×512-Fototest Code-geprüfte Seite zum veröffentlichten System. Zi"
  }
];
