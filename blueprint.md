Ja. Ich würde es als **eigenständige neue TATARUS-Schicht** spezifizieren, nicht als LM-Studio-Hack im Rover. Der Cortex muss derselbe sein, egal ob TATARUS gerade navigiert, zeichnet, erinnert, komponiert oder später eine andere Umwelt bekommt.

Dein aktueller Stand eignet sich dafür sehr gut: `Experience` ist bereits multimodal, `RobotMind` besitzt Prospektion und Persistenz, `CognitiveBridge` existiert bereits als begrenzter Top-down-Kanal und IMAGINATIO benutzt bereits dasselbe `RobotMind`.   

Hier ist der Blueprint, so wie ich ihn tatsächlich implementieren würde.

# TATARUS HYBRID CORTEX

## Implementierungs-Blueprint für TATARUS + lokales 2-Bit-LLM + Rover + IMAGINATIO

**Zielarchitektur:**
TATARUS bleibt der persistente Organismus.
Das lokale LLM wird zu einem zuschaltbaren abstrakten Cortex.
IMAGINATIO wird zum internen Vorstellungs- und Experimentierraum.
Der Cortex darf denken, vorschlagen, erinnern und hypothetische Strategien erzeugen – aber nicht den Organismus ersetzen.

---

# 1. Ziel

Das System soll am Ende aus vier klar getrennten Ebenen bestehen:

```text
┌──────────────────────────────────────────────────────────────┐
│                     LOCAL CORTEX LLM                         │
│                                                              │
│ Sprache · Abstraktion · Planung · Hypothesen · Semantik      │
│                                                              │
│ LM Studio · kleines 2-Bit-Instruct-Modell                    │
└──────────────────────────────┬───────────────────────────────┘
                               │
                     strukturierte Vorschläge
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│                  TATARUS CORTEX LAYER                        │
│                                                              │
│ Trigger Engine                                               │
│ Context Builder                                              │
│ Strategy Arbiter                                             │
│ Imagination Director                                        │
│ Learning Transfer                                           │
│ Replay / Provenance                                          │
└──────────────────────────────┬───────────────────────────────┘
                               │
                   begrenzter Cognitive Bridge
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│                    SYNTHETIC ORGANISM                        │
│                                                              │
│ RobotMind                                                    │
│ Persistent Nervous System                                    │
│ Spatial Memory                                               │
│ Prospective Memory                                           │
│ Physiology                                                   │
│ Heart · Lung · Kidney · Circulation                          │
│ Interoception                                                │
│ Homeostasis                                                  │
└─────────────────┬──────────────────────────┬─────────────────┘
                  │                          │
                  │                          │
        ┌─────────▼─────────┐      ┌────────▼─────────┐
        │    REAL WORLD     │      │    IMAGINATIO    │
        │                   │      │                  │
        │ Rover             │      │ Recall           │
        │ Drone             │      │ Composition      │
        │ Robot             │      │ Free Drawing     │
        │ Simulation        │      │ Counterfactuals  │
        └───────────────────┘      └──────────────────┘
```

Der entscheidende Satz lautet:

> **Das LLM erzeugt kognitive Kandidaten. TATARUS entscheidet, erlebt die Konsequenz und lernt daraus.**

---

# 2. Was ausdrücklich NICHT gebaut wird

Das folgende System wäre falsch:

```text
Sensors
   ↓
LLM
   ↓
Movement
```

Dann wäre TATARUS nur noch ein Sensoradapter.

Ebenfalls falsch:

```text
TATARUS telemetry
   ↓
Prompt
   ↓
LLM says MOVE EAST
   ↓
motor = EAST
```

Das LLM darf keine Motorpopulation überschreiben.

Ebenso verboten:

```text
LLM → reward
LLM → ATP
LLM → heart rate
LLM → synapse weight
LLM → neuron state
LLM → snapshot mutation
```

Der Cortex bekommt insbesondere **keinen direkten Schreibzugriff auf Neuronen, Synapsen, Eligibility Traces oder Physiologie**.

---

# 3. Bestehende Architektur, die erhalten bleibt

TATARUS besitzt bereits die erforderlichen Basiskomponenten.

`Experience` führt bereits zusammen:

* Vision
* Audio
* Touch
* räumlichen Kontext
* Text
* IMU
* Umwelt
* Körperzustand
* Entity-Kontext
* Aktion
* Reward

Das ist bereits der passende organismische Input-Bus.

`RobotMind` besitzt außerdem:

* aktuelle Assembly
* Vorhersagen
* Prediction Error
* Novelty
* biologische Telemetrie
* physiologische Telemetrie
* Prospektion
* Motoraktivität

Der bestehende `CognitiveBridge` aggregiert den Nervenzustand bereits zu einem begrenzten kognitiven Zustand:

```cpp
struct CognitiveState {
    std::uint64_t step;
    std::vector<CognitiveRepresentation> activeRepresentations;
    std::vector<MemoryRecall> recalledStates;

    double novelty;
    double salience;
    double energyNeed;
    double activityNeed;
    double predictionError;
    double confidence;

    std::uint64_t functionalFingerprint;
};
```

Damit existiert bereits die richtige Abstraktionsgrenze.

---

# 4. Wichtigste Architekturentscheidung

## Der Cortex darf NICHT Bestandteil von RobotMind werden

Abhängigkeit:

```text
TATARUS Cortex
      ↓
RobotMind
```

Nicht:

```text
RobotMind
      ↓
LM Studio
```

`RobotMind` darf niemals wissen, ob sein Top-down-Signal von:

* einem LLM
* einem Menschen
* einem Test
* einem Planner
* einem anderen TATARUS

stammt.

Dadurch bleibt der Nervensystemkern sauber und unabhängig.

---

# 5. Neue Projektstruktur

Neu anlegen:

```text
include/
└── tatarus/
    ├── cortex.hpp
    ├── cortex_types.hpp
    ├── cognitive_cue.hpp
    └── cortex_config.hpp

modules/
└── tatarus_cortex/
    ├── cortex_orchestrator.cpp
    ├── cortex_orchestrator.hpp
    │
    ├── cortex_trigger.cpp
    ├── cortex_trigger.hpp
    │
    ├── cortex_context.cpp
    ├── cortex_context.hpp
    │
    ├── cortex_arbiter.cpp
    ├── cortex_arbiter.hpp
    │
    ├── cortex_learning.cpp
    ├── cortex_learning.hpp
    │
    ├── cortex_replay.cpp
    ├── cortex_replay.hpp
    │
    ├── cortex_worker.cpp
    ├── cortex_worker.hpp
    │
    ├── cortex_json.cpp
    ├── cortex_json.hpp
    │
    ├── lm_studio_client.cpp
    ├── lm_studio_client.hpp
    │
    ├── lm_transport.hpp
    ├── lm_transport_winhttp.cpp
    │
    ├── rover_cortex_adapter.cpp
    ├── rover_cortex_adapter.hpp
    │
    ├── imaginatio_cortex_adapter.cpp
    └── imaginatio_cortex_adapter.hpp

tests/
├── tatarus_cortex_contract_tests.cpp
├── tatarus_cortex_trigger_tests.cpp
├── tatarus_cortex_replay_tests.cpp
├── tatarus_cortex_rover_tests.cpp
├── tatarus_cortex_imaginatio_tests.cpp
└── tatarus_cortex_learning_tests.cpp

tools/
└── cortex_probe/
    └── main.cpp

config/
└── cortex.json

Docs/
└── TATARUS_HYBRID_CORTEX.md
```

---

# 6. Core-Datentypen

## 6.1 CortexMode

```cpp
enum class CortexMode : std::uint8_t {
    Disabled,
    ObserveOnly,
    Advisor,
    Imagination,
    Hybrid
};
```

Bedeutung:

### Disabled

LLM vollständig entfernt.

TATARUS muss sich exakt wie heute verhalten.

### ObserveOnly

LLM sieht den Zustand und erzeugt Analysen.

Kein Einfluss auf TATARUS.

### Advisor

LLM darf Strategien vorschlagen.

TATARUS entscheidet.

### Imagination

LLM darf IMAGINATIO Aufgaben und Hypothesen vorschlagen.

### Hybrid

Rover, Weltinteraktion und IMAGINATIO dürfen gemeinsam verwendet werden.

---

# 7. CortexRequest

Das LLM bekommt niemals einen gigantischen Snapshot.

Es bekommt eine stark kondensierte kognitive Momentaufnahme.

```cpp
struct CortexRequest {
    std::uint64_t requestId = 0;
    std::uint64_t organismStep = 0;
    std::uint64_t stateFingerprint = 0;

    CortexTaskKind task{};
    CortexTriggerReason trigger{};

    std::string goal;

    CortexNeuralState neural;
    CortexPhysiologyState physiology;
    CortexSpatialState spatial;
    CortexImaginationState imagination;

    std::vector<CortexRecentOutcome> recentOutcomes;
    std::vector<CortexCapability> availableCapabilities;
};
```

---

# 8. CortexNeuralState

Nur aggregierte Werte:

```cpp
struct CortexNeuralState {
    std::uint64_t assemblyId = 0;
    std::uint64_t predictedAssemblyId = 0;

    double predictionConfidence = 0.0;
    double predictionError = 0.0;
    double novelty = 0.0;
    double sequenceFamiliarity = 0.0;

    double motorConfidence = 0.0;
    double salience = 0.0;
    double energyNeed = 0.0;

    std::vector<CortexRepresentation> activeRepresentations;
};
```

Keine:

```text
synapse[17482].weight
neuron[823].membrane
eligibility[...]
```

---

# 9. CortexPhysiologyState

Hier wird das Hybridmodell sehr interessant.

```cpp
struct CortexPhysiologyState {
    double atp = 1.0;

    double heartRateBpm = 0.0;
    double mapMmHg = 0.0;
    double cardiacOutputLMin = 0.0;

    double oxygenSaturation = 1.0;
    double respirationRate = 0.0;

    double gfrMlMin = 0.0;

    double visceralDistress = 0.0;
    double sympatheticTone = 0.0;

    double brainOxygen = 1.0;
    double brainGlucose = 1.0;

    SleepPhase sleepPhase = SleepPhase::Wake;
};
```

Damit kann das Sprachmodell abstrahieren:

```text
Das Ziel ist erreichbar,
aber der physiologische Zustand ist aktuell schlecht.
```

Es darf daraus jedoch nicht direkt:

```text
REST NOW
```

erzwingen.

Es kann lediglich eine Strategie erzeugen.

---

# 10. CortexSpatialState

Für Rover:

```cpp
struct CortexSpatialState {
    std::uint64_t environmentId = 0;

    bool mapAvailable = false;

    double localNovelty = 0.0;
    double frontierRatio = 0.0;

    std::array<double, 6> obstacleProximity{};

    double targetDistance = 0.0;
    double targetBearing = 0.0;

    bool knownRouteAvailable = false;
    double knownRouteConfidence = 0.0;

    bool loopDetected = false;
    std::uint32_t repeatedPlaceCount = 0;
};
```

---

# 11. CortexImaginationState

IMAGINATIO bekommt dieselbe Bedeutung wie die reale Außenwelt.

```cpp
struct CortexImaginationState {
    bool available = false;

    ImaginationStage stage{};

    std::size_t visualEngrams = 0;
    std::size_t symbolEngrams = 0;

    double lastSimilarity = 0.0;
    double lastNovelty = 0.0;

    std::vector<std::string> knownSymbols;
};
```

---

# 12. CortexResponse

Das LLM liefert keine freie Handlung.

Es liefert Strategiekandidaten.

```cpp
struct CortexResponse {
    std::uint64_t requestId = 0;
    std::uint64_t sourceFingerprint = 0;

    std::vector<CortexStrategy> strategies;

    std::vector<CortexInformationRequest> informationRequests;

    std::optional<ImaginationDirective> imagination;

    std::string summary;
};
```

---

# 13. CortexStrategy

```cpp
struct CortexStrategy {
    std::string id;
    CortexStrategyKind kind{};

    std::string rationale;

    double estimatedRisk = 0.0;
    double estimatedBenefit = 0.0;
    double confidence = 0.0;

    std::vector<std::string> requiredCapabilities;
};
```

Beispiele:

```text
FOLLOW_KNOWN_ROUTE
EXPLORE_FRONTIER
REQUEST_SCAN
RECALL_ROUTE
WAIT_AND_OBSERVE
APPROACH_TARGET
RETREAT
SEARCH_ALTERNATIVE
USE_IMAGINATION
COMPOSE_MEMORY
QUERY_SEMANTIC_MEMORY
```

---

# 14. Kein Vertrauen in LLM-Zahlen

Wenn das LLM sagt:

```json
{
  "estimated_risk": 0.12
}
```

ist das lediglich:

```text
LLM opinion = 0.12
```

nicht:

```text
actual risk = 0.12
```

Der `CortexArbiter` berechnet seine eigene Bewertung.

---

# 15. CortexArbiter

Neue Klasse:

```cpp
class CortexArbiter {
public:
    CortexDecision evaluate(
        const CortexRequest& state,
        const CortexResponse& proposal,
        const RobotMind& mind,
        const SyntheticOrganism& organism);
};
```

Er kombiniert:

```text
LLM proposal
+
TATARUS prospective memory
+
spatial memory
+
physiology
+
past outcomes
+
current goal
```

Ergebnis:

```text
ACCEPT
REJECT
DEFER
REQUEST_MORE_INFORMATION
SEND_TO_IMAGINATION
```

---

# 16. Ganz entscheidend: LM und Nervensystem laufen asynchron

Der Organismus darf niemals auf das LLM warten.

Falsch:

```text
TATARUS tick
 ↓
HTTP request
 ↓
wait 2 seconds
 ↓
continue physiology
```

Richtig:

```text
TATARUS THREAD
──────────────────────────────────────────────→

step
step
step
step
      └── CortexRequest
              │
              ▼

         CORTEX WORKER
         LM Studio
              │
              ▼

         CortexResponse
              │
              ▼

step
step
step
CortexResponse geprüft
step
```

---

# 17. CortexWorker

```cpp
class CortexWorker {
public:
    void submit(CortexRequest request);

    [[nodiscard]]
    std::optional<CortexResponse> poll();

    void stop();
};
```

Default:

```text
max_inflight_requests = 1
queue_capacity = 2
```

Wenn die Queue voll ist:

```text
neue nichtkritische Anfrage verwerfen
```

Niemals OrganismThread blockieren.

---

# 18. Schutz gegen veraltete Antworten

Jede Anfrage bekommt:

```cpp
organismStep
functionalFingerprint
requestId
```

Beispiel:

```text
Request:

step = 1,750,920
fingerprint = ABC123
```

LLM antwortet später.

TATARUS steht inzwischen bei:

```text
step = 1,751,800
fingerprint = X99812
```

Dann kann die Antwort irrelevant sein.

Deshalb:

```cpp
if (!responseStillRelevant(response, currentState)) {
    discard();
}
```

Metrik:

```text
cortex_stale_responses
```

---

# 19. Trigger Engine

Das LLM soll NICHT permanent denken.

Neue Klasse:

```cpp
class CortexTrigger {
public:
    CortexTriggerDecision evaluate(
        const CognitiveContext& cognition,
        const OrganismTelemetry& physiology,
        const CortexRuntimeState& cortexState);
};
```

---

# 20. Trigger

## Explicit

Immer:

```text
Benutzer stellt semantische Aufgabe
Benutzer fordert Imagination
neues abstraktes Ziel
```

## Novelty

Default:

```text
novelty >= 0.70
```

## Prediction Error

```text
prediction_error >= 0.45
```

## Unsicherheit

```text
motor_confidence <= 0.35
```

UND gleichzeitig relevante Handlung erforderlich.

## Loop/Stagnation

```text
wiederholte Orte
wiederholte Aktionen
kein Fortschritt
```

## Goal Conflict

Beispiel:

```text
kurzer Weg
versus
hohe physiologische Belastung
```

## Imagination Opportunity

```text
mehrere Strategien ähnlich bewertet
```

Dann:

```text
SEND_TO_IMAGINATION
```

---

# 21. Wichtig: Trigger sind keine Verhaltensregeln

Beispiel:

```text
ATP < 0.30
```

darf NICHT bedeuten:

```text
STOP
```

Es bedeutet höchstens:

```text
CORTEX_REASONING_MAY_BE_USEFUL
```

Die eigentliche Reaktion bleibt organismisch.

---

# 22. LM Studio Transport

LM Studio läuft separat:

```text
TATARUS.exe
     │
     │ localhost HTTP
     ▼
LM Studio
     │
     ▼
2-Bit LLM
```

Default:

```text
http://127.0.0.1:1234/v1
```

Beim Start:

```text
GET /v1/models
```

Dann:

```text
POST /v1/chat/completions
```

LM Studio unterstützt aktuell beide OpenAI-kompatiblen Endpunkte. Es unterstützt außerdem JSON-Schema-Ausgaben; kleine Modelle können bei strukturierten Outputs jedoch weniger zuverlässig sein, weshalb TATARUS trotzdem selbst validieren muss.

---

# 23. Keine OpenAI SDK-Abhängigkeit

Für TATARUS würde ich keinen Python- oder OpenAI-Client einbauen.

Stattdessen:

```cpp
class ILmTransport {
public:
    virtual ~ILmTransport() = default;

    virtual LmHttpResponse postJson(
        std::string_view endpoint,
        std::string_view payload) = 0;
};
```

Windows:

```text
WinHTTP
```

Damit:

```text
keine Python Runtime
kein Node
kein Framework
kein LM Studio SDK
```

LM Studio bleibt lediglich ein lokaler HTTP-Prozess.

---

# 24. LM-Modell

Der Cortex muss modellunabhängig sein.

Konfiguration:

```json
{
  "provider": "lm_studio",
  "base_url": "http://127.0.0.1:1234/v1",
  "model": "AUTO",
  "enabled": false
}
```

Für den ersten Versuch:

```text
ca. 1–4B Instruct
GGUF
2-Bit Quantisierung
```

Nicht das größte verfügbare Modell verwenden.

Ziel ist ausdrücklich zu prüfen:

> Was entsteht aus TATARUS + einem kleinen abstrakten Sprachkern?

---

# 25. Prompt-Größe

Kein kompletter Chatverlauf.

Ziel:

```text
System Contract:       ~250–400 Tokens
Organism State:        ~300–600 Tokens
Task:                  ~50–150 Tokens
Recent Outcomes:       ~100–250 Tokens
Output:                <=256 Tokens
```

Gesamt ideal:

```text
< 1500–2000 Tokens
```

Dadurch bleibt ein kleines 2-Bit-Modell schnell.

---

# 26. Planner-Einstellungen

Startwerte:

```json
{
  "temperature": 0.20,
  "top_p": 0.90,
  "max_tokens": 256,
  "stream": false
}
```

Für IMAGINATIO:

```json
{
  "temperature": 0.65,
  "top_p": 0.95,
  "max_tokens": 384
}
```

Reasoning und freie Imagination dürfen unterschiedliche Profile besitzen.

---

# 27. JSON Contract

Beispielausgabe:

```json
{
  "strategies": [
    {
      "id": "S1",
      "kind": "FOLLOW_KNOWN_ROUTE",
      "rationale": "Known route has prior successful experience.",
      "estimated_risk": 0.18,
      "estimated_benefit": 0.72,
      "confidence": 0.81,
      "required_capabilities": []
    },
    {
      "id": "S2",
      "kind": "EXPLORE_FRONTIER",
      "rationale": "Direct route may reduce distance.",
      "estimated_risk": 0.55,
      "estimated_benefit": 0.88,
      "confidence": 0.58,
      "required_capabilities": ["SCAN"]
    }
  ],
  "information_requests": [],
  "summary": "Known route is safer; frontier exploration offers higher potential gain."
}
```

---

# 28. Parser-Regel

LLM-Ausgabe ist grundsätzlich:

```text
UNTRUSTED INPUT
```

Deshalb:

```text
HTTP
 ↓
JSON parser
 ↓
schema validation
 ↓
range validation
 ↓
enum validation
 ↓
capability validation
 ↓
CortexArbiter
```

Erst danach darf irgendetwas TATARUS erreichen.

---

# 29. Rover Adapter

Dateien:

```text
rover_cortex_adapter.hpp
rover_cortex_adapter.cpp
```

Aufgabe:

```text
TATARUS Rover State
        ↓
domain-independent CortexState
```

und:

```text
CortexStrategy
        ↓
Rover cognitive objective
```

---

# 30. Rover Skills

Der Cortex kennt nur Fähigkeiten.

Beispielsweise:

```text
SCAN
RECALL_ROUTE
FOLLOW_ROUTE
EXPLORE_FRONTIER
REASSESS
WAIT
APPROACH
SEARCH_ALTERNATIVE
```

Nicht:

```text
motor_left = 0.7
motor_right = 0.2
```

---

# 31. Rover-Ablauf

```text
Sensoren
   ↓
SyntheticOrganism
   ↓
RobotMind
   ↓
Motorentscheidung
   ↓
Trigger Engine
```

Normalfall:

```text
kein Trigger
   ↓
TATARUS fährt allein
```

Unbekannte Situation:

```text
Novelty = 0.84
Prediction Error = 0.62
Confidence = 0.29
```

Dann:

```text
CortexRequest
```

LLM:

```text
S1 known route
S2 scan frontier
S3 explore cautiously
```

Arbiter:

```text
S2 → request additional scan
```

Rover scannt.

Neue echte Sensorinformation kommt zurück.

Dann wird neu entschieden.

---

# 32. IMAGINATIO ist KEIN Sonderfall

Das ist mir besonders wichtig.

Der Cortex bekommt nicht:

```text
Rover AI
```

und separat:

```text
Drawing AI
```

Es ist immer derselbe Cortex.

Nur die verfügbaren Skills ändern sich.

---

# 33. IMAGINATIO Skills

Aktuell existieren bereits:

```text
learnToTrace()
drawFromMemory()
associateSymbol()
drawFromSymbol()
drawFreely()
compose()
```

Darauf wird der Adapter aufgebaut.

Cortex-Fähigkeiten:

```text
VISUAL_RECALL
SYMBOL_RECALL
COMPOSE
FREE_IMAGINATION
OBSERVE_CANVAS
COMPARE_MEMORY
```

---

# 34. Beispiel IMAGINATIO

Benutzer sagt:

```text
Stell dir ein Haus unter einem Baum vor.
```

Das LLM darf NICHT Pixel erzeugen.

Es bekommt:

```text
known_symbols:
HOUSE
TREE
SUN
ROAD
```

Das LLM schlägt vor:

```json
{
  "imagination": {
    "mode": "COMPOSE",
    "symbols": ["HOUSE", "TREE"],
    "concept": "house beneath tree"
  }
}
```

Dann:

```text
LLM
 ↓
ImaginatioCortexAdapter
 ↓
VisualImagination::compose()
 ↓
TATARUS Nervensystem
 ↓
Canvas
```

Das erzeugte Bild stammt weiterhin aus IMAGINATIO.

Nicht aus dem LLM.

---

# 35. Noch interessanter: semantisch gelenkte freie Imagination

Beispiel:

```text
Imagine etwas, das Schutz bedeutet.
```

Das LLM könnte aufgrund seiner semantischen Fähigkeit Kandidaten erzeugen:

```text
HOUSE
SHELTER
TREE
WALL
```

TATARUS besitzt davon vielleicht:

```text
HOUSE
TREE
```

Dann sagt der Cortex:

```text
COMPOSE HOUSE + TREE
```

Aber das konkrete visuelle Resultat entsteht aus dem persistenten visuellen Gedächtnis von TATARUS.

Damit trennt man:

```text
Semantik
```

von:

```text
persönlicher visueller Erfahrung
```

Das ist extrem wichtig.

---

# 36. Prospective Imagination

Später erweitern wir IMAGINATIO von:

```text
visual imagination
```

auf:

```text
counterfactual imagination
```

Neue Klasse:

```cpp
class ImaginationSandbox;
```

Sie bekommt:

```text
current organism state
+
candidate strategy
+
environment model
```

und beantwortet:

```text
Was könnte passieren?
```

---

# 37. Beispiel Rover + Imagination

Reale Situation:

```text
Route A:
unbekannt
kurz
steiler Hang

Route B:
bekannt
lang
```

LLM:

```text
A
B
```

Anstatt sofort zu handeln:

```text
SEND_TO_IMAGINATION
```

Dann:

```text
TATARUS Snapshot Branch
       ↓
simulated Route A
       ↓
physiology prediction
       ↓
motor prediction
       ↓
risk
```

und:

```text
TATARUS Snapshot Branch
       ↓
simulated Route B
       ↓
...
```

Erst danach:

```text
real world decision
```

---

# 38. Sehr wichtige Trennung

Imaginierte Erfahrung darf zunächst NICHT wie reale Erfahrung gelernt werden.

Deshalb:

```text
REAL EXPERIENCE
→ plasticity enabled

IMAGINED EXPERIENCE
→ isolated branch
```

Sonst könnte TATARUS lernen:

```text
etwas ist passiert
```

obwohl es lediglich vorgestellt wurde.

---

# 39. Imagination Provenance

Jede Erfahrung bekommt künftig:

```cpp
enum class ExperienceOrigin {
    Real,
    Simulation,
    Imagination,
    Replay,
    Teacher
};
```

Diese Herkunft muss dauerhaft erhalten bleiben.

---

# 40. Learning Transfer

Der stärkste Teil des Systems kommt erst später.

Das LLM schlägt beispielsweise Strategie S2 vor.

TATARUS führt sie real aus.

Resultat:

```text
Reward        +0.31
Success        1.0
Novelty        0.42
ATP cost       niedrig
Distress       gesunken
Goal progress  positiv
```

Dann:

```text
Cortex suggestion
      ↓
real action
      ↓
real consequence
      ↓
TATARUS learning
```

Nicht:

```text
LLM says good
↓
reward +1
```

---

# 41. Teacher-Learning

Neue Struktur:

```cpp
struct CortexTeachingEpisode {
    std::uint64_t contextFingerprint;

    std::string strategyId;

    ActionId resultingAction;

    ActionOutcome outcome;

    bool cortexUsed;

    bool successful;
};
```

TATARUS lernt ausschließlich aus dem echten `ActionOutcome`.

---

# 42. Ziel: Cortex wird mit Erfahrung weniger benötigt

Wir messen:

```text
cortex_requests_per_1000_steps
```

Wenn TATARUS eine Situation gelernt hat:

```text
novelty sinkt
prediction confidence steigt
motor confidence steigt
```

Dann wird automatisch seltener gefragt.

Idealer Verlauf:

```text
Episode 1       15 Cortex Requests
Episode 10       8
Episode 50       3
Episode 200      0
```

Das wäre ein sehr starkes Forschungsergebnis.

---

# 43. Kompetenzkarte

Neue Cortex-Memory-Struktur:

```cpp
struct LearnedCortexCompetence {
    std::uint64_t contextClass;

    std::uint64_t consultations = 0;
    std::uint64_t successfulTransfers = 0;

    double autonomousSuccessRate = 0.0;
};
```

Wenn:

```text
autonomousSuccessRate > threshold
```

wird für ähnliche Situationen kein Cortex mehr verwendet.

---

# 44. Cognitive Bridge Erweiterung

Die vorhandene Struktur besitzt derzeit:

```cpp
AttentionTarget attention;
double motorIntent;
double intentStrength;
uint32_t recallCue;
double recallStrength;
double reward;
```

Für den Hybrid-Cortex würde ich **Reward ausdrücklich nicht freigeben**.

Stattdessen neue modellunabhängige Struktur:

```cpp
struct CognitiveCue {
    AttentionTarget attention = AttentionTarget::Balanced;

    std::uint32_t recallCue = 0;
    double recallStrength = 0.0;

    std::array<double, 4> goalBias{};

    double goalBiasStrength = 0.0;

    std::vector<double> semanticContext;
};
```

---

# 45. goalBias nicht direkt auf Motoren

Wichtig:

```text
goalBias
   ↓
context population
   ↓
neural processing
   ↓
motor population
```

Nicht:

```text
goalBias
   ↓
selectedDirection
```

Damit kann der Cortex einen Gedanken bzw. eine Absicht einspeisen, ohne die neuronale Entscheidung zu überschreiben.

---

# 46. Neutraler CognitiveCue muss bit-identisch sein

Das ist eine harte Bedingung.

```cpp
CognitiveCue{}
```

muss exakt dasselbe erzeugen wie:

```text
kein Cortex
```

Kein zusätzliches Null-Array in `contextEvents`.

Keine andere Kanalzahl.

Keine andere Random-Sequenz.

Keine anderen Timings innerhalb der Simulation.

Test:

```text
baseline state hash
==
cortex-disabled state hash
```

nach beispielsweise:

```text
100
1,000
10,000
100,000 Steps
```

---

# 47. Snapshot-Erweiterung

Aktuell enthält die persistente Struktur bereits Nervensystem, SDK-State, Identity und Kartografie; IMAGINATIO besitzt ebenfalls Snapshot-Support.

Neu:

```text
snapshot/
├── experience_core.tns
├── sdk_state.bin
├── environment_map.tcm
├── identity/
├── organism/
├── imaginatio/
└── cortex/
    ├── cortex_state.bin
    ├── competence.bin
    ├── replay_index.bin
    └── manifest.json
```

---

# 48. Was NICHT in den Snapshot gehört

Nicht:

```text
LM weights
```

Nicht:

```text
LM Studio installation
```

Nicht:

```text
HTTP runtime
```

Diese Dinge sind externe Infrastruktur.

---

# 49. Cortex Manifest

Speichern:

```json
{
  "schema": "tatarus-cortex-v1",
  "mode": "Hybrid",
  "provider": "lm_studio",
  "model_identifier": "...",
  "quantization": "Q2",
  "request_count": 182,
  "accepted_strategy_count": 44,
  "rejected_strategy_count": 117,
  "stale_response_count": 21
}
```

---

# 50. Reproduzierbarkeit

Ein Live-LLM ist nicht zuverlässig bit-deterministisch.

Deshalb drei Betriebsarten:

```cpp
enum class CortexExecutionMode {
    Off,
    Live,
    Record,
    Replay
};
```

---

# 51. Record Mode

Speichert:

```text
request fingerprint
request JSON
response JSON
accepted/rejected
outcome
```

in:

```text
cortex_record.jsonl
```

---

# 52. Replay Mode

Kein LM Studio.

Statt:

```text
request
 ↓
LM
```

wird:

```text
request fingerprint
 ↓
record lookup
 ↓
recorded response
```

verwendet.

Damit können vollständige Experimente reproduzierbar werden.

---

# 53. Provenance

Jede Cortex-Entscheidung bekommt:

```cpp
struct CortexProvenance {
    std::uint64_t requestId;
    std::uint64_t sourceStep;
    std::uint64_t sourceFingerprint;

    std::string model;
    CortexTriggerReason trigger;

    bool accepted;
    std::string acceptedStrategy;

    std::uint64_t resultingActionId;
};
```

Damit kann später exakt ausgewertet werden:

> War diese Handlung selbst gelernt oder Cortex-induziert?

---

# 54. UI-Erweiterung

Im Live Monitor neuer Bereich:

```text
CORTEX
────────────────────────

Status          ONLINE
Mode            HYBRID
Model           2-Bit Local
Requests        182
Accepted        44
Rejected        117
Stale           21

Last Trigger
Prediction Error

Last Proposal
EXPLORE_FRONTIER

Decision
REJECTED

Reason
High physiological cost
```

---

# 55. Besonders wichtige Visualisierung

Ich würde zwei Aktivitätslinien anzeigen:

```text
TATARUS AUTONOMY
██████████████████░░ 91%

CORTEX DEPENDENCY
██░░░░░░░░░░░░░░░░  9%
```

Das könnte später eine der interessantesten Kennzahlen überhaupt werden.

---

# 56. Imagination UI

Zusätzlich:

```text
IMAGINATION SOURCE

REAL MEMORY
SYMBOL RECALL
CORTEX SUGGESTION
FREE ASSOCIATION
COUNTERFACTUAL
```

Damit sieht man, warum etwas vorgestellt wurde.

---

# 57. Implementierungsreihenfolge

Jetzt der wichtigste Teil.

Nicht alles gleichzeitig bauen.

---

# PHASE 0 — BASELINE FREEZE

## Zuerst

Noch keine LM-Integration.

### Aufgaben

1. aktuellen TATARUS Stand taggen
2. alle bestehenden Tests ausführen
3. Referenz-Snapshots erzeugen
4. State Hash dokumentieren
5. Rover-Referenzlauf speichern
6. IMAGINATIO-Referenzlauf speichern

### Ziel

Wir brauchen eine unveränderliche Nullmessung.

### Gate

Erst Phase 1 beginnen, wenn:

```text
ALL EXISTING TESTS PASS
```

---

# PHASE 1 — LM STUDIO TRANSPORT

Noch keinerlei Verbindung zum Nervensystem.

Implementieren:

```text
lm_transport.hpp
lm_transport_winhttp.cpp
lm_studio_client.hpp
lm_studio_client.cpp
tools/cortex_probe/main.cpp
```

`cortex_probe` macht nur:

```text
GET /v1/models
POST /v1/chat/completions
```

### Test

```text
TATARUS_CORTEX_PROBE.exe
```

soll ausgeben:

```text
LM Studio: reachable
Model: ...
Prompt: OK
JSON response: valid
```

### Gate

Noch kein `RobotMind`.

---

# PHASE 2 — CONTRACT

Implementieren:

```text
cortex_types.hpp
cortex_json.cpp
cortex_json.hpp
```

Danach:

```text
CortexRequest
CortexResponse
CortexStrategy
ImaginationDirective
```

### Tests

* malformed JSON
* fehlende Felder
* falsche enums
* NaN
* Infinity
* Werte außerhalb [0,1]
* zu viele Strategien
* unbekannte Skill-Namen

### Harte Limits

```text
max strategies = 3
max information requests = 3
max summary bytes = 1024
max rationale bytes = 512
```

### Gate

100 % Contract Tests.

---

# PHASE 3 — OBSERVE ONLY

Erst jetzt TATARUS anbinden.

Implementieren:

```text
cortex_context.cpp
cortex_trigger.cpp
cortex_worker.cpp
cortex_orchestrator.cpp
```

Aber:

```text
CortexMode = ObserveOnly
```

Der Cortex darf nichts verändern.

Ablauf:

```text
TATARUS
 ↓
state summary
 ↓
LLM
 ↓
proposal
 ↓
LOG ONLY
```

### Forschungstest

Vergleichen:

```text
Was hätte das LLM vorgeschlagen?
```

gegen:

```text
Was hat TATARUS tatsächlich getan?
```

Das ist bereits hochinteressant.

### Gate

State Hash von TATARUS mit/ohne ObserveOnly muss identisch bleiben.

---

# PHASE 4 — CORTEX ARBITER

Implementieren:

```text
cortex_arbiter.cpp
```

Das LLM darf jetzt Vorschläge liefern.

Aber weiterhin keine direkten Kommandos.

Arbiter bewertet:

```text
prospection
spatial memory
physiology
recent outcomes
```

### Gate

Eine absichtlich schlechte LLM-Antwort muss zuverlässig verworfen werden.

Beispielsweise:

```text
„Fahre gegen das bekannte Hindernis.“
```

muss keinerlei direkte Motorwirkung haben.

---

# PHASE 5 — ROVER ADAPTER

Jetzt:

```text
rover_cortex_adapter.cpp
```

Fähigkeiten implementieren:

```text
SCAN
RECALL_ROUTE
EXPLORE_FRONTIER
FOLLOW_KNOWN_ROUTE
SEARCH_ALTERNATIVE
```

Noch keine direkte Richtungssteuerung.

### A/B-Test

```text
A: TATARUS alone
B: TATARUS + Cortex
```

gleiche Maps, Seeds und Ziele.

Messen:

```text
completion rate
distance
loops
collisions
ATP consumption
cortex calls
time
novelty
prediction error
```

---

# PHASE 6 — IMAGINATIO ADAPTER

Jetzt:

```text
imaginatio_cortex_adapter.cpp
```

Der Cortex erhält Skills:

```text
RECALL
COMPOSE
FREE_IMAGINATION
```

### Test 1

Bekannte Symbole:

```text
HOUSE
TREE
```

Aufgabe:

```text
„Stell dir Schutz vor.“
```

LLM darf semantische Symbolkombination bestimmen.

IMAGINATIO muss selbst zeichnen.

### Test 2

LLM deaktivieren.

Die gespeicherten visuellen Engramme müssen unverändert vorhanden sein.

---

# PHASE 7 — UNIFIED HYBRID MODE

Erst jetzt:

```text
CortexMode::Hybrid
```

Ein Cortex für:

```text
Rover
+
IMAGINATIO
```

Beispiel:

```text
Rover findet unbekanntes Objekt
        ↓
TATARUS merkt hohe Novelty
        ↓
Cortex erkennt semantisches Problem
        ↓
IMAGINATIO erinnert ähnliche Formen
        ↓
Cortex vergleicht Konzepte
        ↓
TATARUS entscheidet
```

Das wäre der erste echte Hybridkreislauf.

---

# PHASE 8 — COUNTERFACTUAL IMAGINATION

Jetzt erst:

```text
ImaginationSandbox
```

Kandidaten können intern ausprobiert werden.

Wichtig:

```text
Branch != Real Organism
```

### Keine Plastizitätslecks

Nach Sandbox-Lauf muss:

```text
real organism state hash
```

unverändert sein.

---

# PHASE 9 — TEACHER TRANSFER

Jetzt darf gemessen werden, ob TATARUS Cortex-Strategien selbst lernt.

Ablauf:

```text
Cortex proposes
 ↓
TATARUS selects
 ↓
real execution
 ↓
real consequence
 ↓
TATARUS plasticity
```

Danach erneut dieselbe Situation.

Messen:

```text
Braucht TATARUS den Cortex erneut?
```

---

# PHASE 10 — BOUNDED TOP-DOWN COGNITION

Erst wenn Phasen 0–9 stabil sind.

Jetzt CognitiveBridge erweitern.

Erlaubt:

```text
attention
recall cue
goal context
semantic context
```

Nicht erlaubt:

```text
reward injection
direct motor override
physiology mutation
```

Zunächst sehr geringe Stärke:

```text
goalBiasStrength <= 0.20
```

Danach experimentell erhöhen.

---

# PHASE 11 — SLEEP / DREAM CORTEX

Erst ganz zuletzt.

TATARUS besitzt bereits Schlafzustände.

Damit könnte der Cortex während:

```text
NREM
REM
```

anders arbeiten.

## NREM

Kein LLM oder nur Zusammenfassung.

Schwerpunkt:

```text
Consolidation
```

## REM

IMAGINATIO kann stärker aktiviert werden:

```text
memory fragments
+
symbol associations
+
weak semantic Cortex cues
```

Das wäre die geeignete Phase für freie Kombinationen.

Nicht reale Aktionen.

---

# 58. Systemzustände

Final:

```text
WAKE / FAMILIAR
→ Cortex mostly off

WAKE / NOVEL
→ Cortex advisor possible

WAKE / CONFLICT
→ Cortex reasoning

IMAGINATION
→ Cortex semantic director

NREM
→ consolidation

REM
→ associative imagination
```

Damit wird das LLM nicht der permanente Mittelpunkt.

---

# 59. Fallback-Verhalten

LM Studio beendet:

```text
TATARUS läuft weiter
```

HTTP Timeout:

```text
TATARUS läuft weiter
```

Modell antwortet Müll:

```text
TATARUS läuft weiter
```

JSON ungültig:

```text
TATARUS läuft weiter
```

Cortex Worker crasht:

```text
TATARUS läuft weiter
```

Das ist eine harte Architekturregel.

---

# 60. Default cortex.json

```json
{
  "enabled": false,
  "mode": "observe_only",

  "provider": {
    "type": "lm_studio",
    "base_url": "http://127.0.0.1:1234/v1",
    "model": "AUTO",
    "timeout_ms": 5000
  },

  "inference": {
    "planner_temperature": 0.20,
    "imagination_temperature": 0.65,
    "top_p": 0.90,
    "max_output_tokens": 256
  },

  "worker": {
    "max_inflight": 1,
    "queue_capacity": 2
  },

  "trigger": {
    "novelty": 0.70,
    "prediction_error": 0.45,
    "low_motor_confidence": 0.35,
    "cooldown_steps": 250
  },

  "limits": {
    "max_strategies": 3,
    "max_information_requests": 3,
    "max_prompt_tokens": 1800
  },

  "learning": {
    "allow_direct_reward": false,
    "allow_direct_motor_control": false,
    "allow_physiology_mutation": false,
    "allow_synapse_access": false
  }
}
```

---

# 61. Zentrale Laufzeit

Finaler Ablauf:

```text
WORLD
 │
 ▼
SENSORS
 │
 ▼
SYNTHETIC ORGANISM
 │
 ├── HEART
 ├── LUNG
 ├── KIDNEY
 ├── CIRCULATION
 ├── INTEROCEPTION
 │
 ▼
ROBOT MIND
 │
 ├── neural state
 ├── memory
 ├── prediction
 ├── motor decision
 │
 ▼
CORTEX TRIGGER
 │
 ├── no ────────────────► normal TATARUS operation
 │
 └── yes
      │
      ▼
CORTEX CONTEXT BUILDER
      │
      ▼
LOCAL 2-BIT LLM
      │
      ▼
CORTEX RESPONSE VALIDATOR
      │
      ▼
CORTEX ARBITER
      │
      ├── reject
      │
      ├── request sensor information
      │
      ├── send to imagination
      │
      └── accept cognitive strategy
              │
              ▼
       bounded cognitive cue
              │
              ▼
           TATARUS
              │
              ▼
           ACTION
              │
              ▼
         CONSEQUENCE
              │
              ▼
           LEARNING
```

---

# 62. Rover und Imagination gemeinsam

Dadurch entsteht beispielsweise folgender Ablauf:

```text
ROVER:
„Ich kenne diesen Bereich nicht.“

Novelty ↑
Prediction confidence ↓

        ↓

CORTEX:
„Es gibt drei plausible Strategien.“

        ↓

TATARUS:
„Keine ist eindeutig.“

        ↓

IMAGINATIO:
simuliert / rekonstruiert bekannte ähnliche Situationen

        ↓

CORTEX:
ordnet die Ergebnisse semantisch

        ↓

TATARUS:
wählt anhand eigener Erinnerung,
Physiologie und aktueller Umwelt

        ↓

REAL ACTION

        ↓

REAL CONSEQUENCE

        ↓

NEURAL LEARNING
```

Das ist die eigentliche Zielarchitektur.

---

# 63. Wichtigste Forschungsmetriken

Nicht nur:

```text
Hat der Rover das Ziel erreicht?
```

Sondern:

## Cortex Dependency

```text
Cortex Calls / 1000 Steps
```

## Cortex Acceptance

```text
accepted / proposed
```

## Transfer Efficiency

```text
Wie viele Cortex-Hilfen waren nötig,
bis TATARUS das Problem autonom löst?
```

## Autonomous Retention

Cortex entfernen.

Dann testen:

```text
Bleibt die Leistung erhalten?
```

## Imagination Benefit

```text
mit Sandbox
versus
ohne Sandbox
```

## Physiological Cost

```text
ATP
O2
MAP
cardiac load
distress
```

## Neural Change

```text
assemblies
synapses
myelin
prediction
prospective memory
```

---

# 64. Das wichtigste Experiment

Vier Gruppen:

```text
A
TATARUS naive
kein Cortex

B
TATARUS + Cortex

C
TATARUS nach Cortex-Training
Cortex wieder deaktiviert

D
LLM-basierter Agent ohne TATARUS-Lernen
```

Wenn:

```text
C > A
```

nach Entfernung des Cortex,

dann hat das Sprachmodell Wissen bzw. Strategien in das persistente TATARUS-Verhalten übertragen.

Wenn zusätzlich:

```text
Cortex Calls → 0
```

während:

```text
Performance bleibt hoch
```

dann wäre das wesentlich interessanter als lediglich ein LLM-gesteuerter Rover.

---

# 65. Was dieses System dann tatsächlich wäre

Nicht:

```text
LLM + Robot
```

Nicht:

```text
LLM + Memory
```

Nicht:

```text
TATARUS mit Chatfunktion
```

Sondern:

```text
Persistent synthetic organism
+
temporary abstract cortex
+
internal imagination
+
real consequence
+
experience-dependent neural transfer
```

Das LLM wäre damit kein Organismus.

Es wäre ein **temporär verfügbarer abstrakter Denkraum**.

Die kontinuierliche Identität läge weiterhin in:

```text
TATARUS Nervenzustand
+
Gedächtnis
+
Physiologie
+
Entwicklungsgeschichte
+
Imagination Engrams
+
realer Erfahrung
```

---

# 66. Meine Priorität für die tatsächliche Implementierung

Die Reihenfolge darf nicht verändert werden:

```text
0  Freeze baseline
1  LM Studio transport
2  JSON contract
3  Observe-only Cortex
4  Arbiter
5  Rover
6  IMAGINATIO
7  Unified Hybrid
8  Counterfactual Sandbox
9  Teacher Transfer
10 Bounded Top-down Cognition
11 Sleep / Dream Cortex
```

Insbesondere:

> **Keinen motorischen LLM-Einfluss bauen, bevor Observe-Only, Replay, Arbiter und Imagination sauber funktionieren.**

Das hält das System wissenschaftlich interpretierbar.

---

# 67. Definition von „fertig“

Der Hybrid-Cortex ist erst dann erfolgreich implementiert, wenn folgende Anforderungen gleichzeitig erfüllt sind:

```text
[PASS] TATARUS funktioniert ohne LM Studio unverändert.

[PASS] Cortex kann vollständig deaktiviert werden.

[PASS] ObserveOnly verändert keinen Nervenzustand.

[PASS] LLM kann keine Neuronen oder Synapsen schreiben.

[PASS] LLM kann keinen Reward erzeugen.

[PASS] LLM kann keinen Motor direkt steuern.

[PASS] LLM-Ausfall stoppt TATARUS nicht.

[PASS] Rover kann Cortex-Vorschläge nutzen.

[PASS] IMAGINATIO kann Cortex-Vorschläge nutzen.

[PASS] Beide verwenden denselben Cortex.

[PASS] Imagination verändert nicht unbeabsichtigt reale Erfahrung.

[PASS] Alle Cortex-Interaktionen besitzen Provenance.

[PASS] Record/Replay reproduziert Cortex-Experimente.

[PASS] TATARUS kann erfolgreiche Cortex-Strategien durch reale Konsequenz lernen.

[PASS] Nach Cortex-Entfernung kann überprüft werden, ob gelerntes Verhalten erhalten bleibt.
```

---

# Endarchitektur

```text
                         LANGUAGE / ABSTRACT WORLD
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ LOCAL 2-BIT LLM │
                         │     CORTEX      │
                         └────────┬────────┘
                                  │
                             proposals
                                  │
                                  ▼
                     ┌────────────────────────┐
                     │    CORTEX ARBITER      │
                     │                        │
                     │ TATARUS has final say │
                     └───────┬────────┬───────┘
                             │        │
                    cognition│        │imagination
                             │        │
                  ┌──────────▼──┐  ┌──▼────────────┐
                  │  RobotMind  │  │  IMAGINATIO   │
                  │             │  │               │
                  │ Prediction  │◄─┤ Visual Memory │
                  │ Memory      │  │ Composition   │
                  │ Plasticity  │  │ Simulation    │
                  └──────┬──────┘  └───────────────┘
                         │
                  ┌──────▼──────┐
                  │ PHYSIOLOGY  │
                  │             │
                  │ Heart       │
                  │ Lung        │
                  │ Kidney      │
                  │ Blood       │
                  │ ATP         │
                  │ Stress      │
                  └──────┬──────┘
                         │
                         ▼
                  ┌─────────────┐
                  │    BODY     │
                  │             │
                  │ Rover       │
                  │ Drone       │
                  │ Simulation  │
                  │ Future Body │
                  └──────┬──────┘
                         │
                         ▼
                       WORLD
                         │
                         └──────────────► EXPERIENCE
```

**Architekturprinzip:**

> The Cortex may suggest.
> The imagination may simulate.
> The nervous system must decide.
> The world supplies the consequence.
> TATARUS carries the experience forward.

Ein technischer Punkt ist dabei besonders günstig: LM Studio stellt aktuell `/v1/models` und `/v1/chat/completions` als OpenAI-kompatible lokale Endpunkte bereit. Damit können wir den kompletten Runtime-Client direkt in C++ über localhost bauen. ([LM Studio][1]) LM Studio unterstützt auch JSON-Schema-Ausgaben; die eigene Dokumentation weist allerdings darauf hin, dass gerade kleine Modelle unter 7B damit Schwierigkeiten haben können. Deshalb habe ich im Blueprint **Parser + eigene Validierung + Arbiter** vorgesehen und vertraue nicht darauf, dass ein 2-Bit-Kleinmodell immer perfektes JSON erzeugt. ([LM Studio][2])

Der für mich wichtigste Teil dieses Blueprints ist **Phase 8/9**. Der Rover ist nur eine Anwendung. IMAGINATIO ist eine zweite. Wenn wir die Schnittstelle so bauen, kann später praktisch jedes TATARUS-Subsystem denselben Cortex verwenden. Und wenn TATARUS nach wiederholter Cortex-Unterstützung dieselbe Situation anschließend **ohne eingeschaltetes LLM** erfolgreich löst, haben wir genau das Hybridprinzip erreicht, über das wir gesprochen haben: Das Tensor-Modell dient zeitweise als abstrakter Lehrer, während das eigentliche persistente Lernen im synthetischen Nervensystem verbleibt. 

**Als nächsten Implementierungsschritt würde ich nicht mit dem Rover anfangen.** Ich würde jetzt unmittelbar **Phase 0–3 implementieren: `tatarus_cortex` als neues C++-Modul, LM-Studio-Client, JSON-Vertrag, Worker-Thread, Trigger Engine und Observe-Only-Modus**. Damit können wir das kleine 2-Bit-Modell erstmals an den laufenden Organismus anschließen, ohne dass es auch nur ein einziges Bit seines Verhaltens verändern darf. Danach haben wir eine saubere Basis für Rover **und** IMAGINATIO.

[1]: https://lmstudio.ai/docs/developer/openai-compat?utm_source=chatgpt.com "OpenAI Compatibility Endpoints | LM Studio"
[2]: https://beta.lmstudio.ai/docs/developer/openai-compat/structured-output?utm_source=chatgpt.com "Structured Output | LM Studio Docs"
