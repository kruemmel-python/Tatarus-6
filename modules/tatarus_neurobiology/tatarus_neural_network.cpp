#include "tatarus_neural_network.hpp"
#include "tatarus_tissue.hpp"
#include "tatarus_physiology.hpp"
#include "tatarus_prospection.hpp"

#include "tatarus_signal_kernel.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace tatarus::neuro {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSpatialTraceDecay = 0.94;
constexpr double kSpatialTraceFloor = 0.015;
constexpr std::size_t kMaximumSpatialTraceEntries = 96;
constexpr std::size_t kMaximumSpatialEpisodeEntries = 4'096;
constexpr double kSpatialDirectLearningRate = 0.28;
constexpr double kSpatialNoveltyBackpropGain = 0.22;
constexpr double kSpatialSuccessBackpropGain = 0.58;
constexpr double kSpatialTemporalDiscount = 0.94;
constexpr double kSpatialTemporalLearningRate = 0.24;

bool hasPlasticityWriteSignal(const SensorFrame& frame) {
    const auto nonzero = [](const auto& values) {
        return std::any_of(
            values.begin(),
            values.end(),
            [](const auto value) {
                return std::abs(static_cast<double>(value)) > 1e-12;
            });
    };
    return nonzero(frame.visionEvents)
        || nonzero(frame.audioSamples)
        || nonzero(frame.touch)
        || nonzero(frame.vestibularEvents)
        || nonzero(frame.spatialContext)
        || !frame.textBytes.empty()
        || nonzero(frame.contextEvents)
        || std::abs(frame.reward) > 1e-12
        || std::abs(frame.novelty) > 1e-12;
}

template <class T>
void writePod(std::ostream& output, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(
        reinterpret_cast<const char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
    if (!output) {
        throw std::runtime_error("Snapshot konnte nicht geschrieben werden");
    }
}

template <class T>
void readPod(std::istream& input, T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    input.read(
        reinterpret_cast<char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
    if (!input) {
        throw std::runtime_error("Snapshot ist abgeschnitten");
    }
}

template <class T>
void writePodVector(std::ostream& output, const std::vector<T>& values) {
    const std::uint64_t size = values.size();
    writePod(output, size);
    if constexpr (std::is_trivially_copyable_v<T>) {
        if (!values.empty()) {
            output.write(
                reinterpret_cast<const char*>(values.data()),
                static_cast<std::streamsize>(values.size() * sizeof(T)));
        }
    } else {
        for (const auto& value : values) {
            writePod(output, value);
        }
    }
    if (!output) {
        throw std::runtime_error("Snapshot-Vektor konnte nicht geschrieben werden");
    }
}

template <class T>
void readPodVector(std::istream& input, std::vector<T>& values) {
    std::uint64_t size = 0;
    readPod(input, size);
    if (size > 100'000'000ULL) {
        throw std::runtime_error("Snapshot-Vektor ist unplausibel groß");
    }
    values.resize(static_cast<std::size_t>(size));
    if constexpr (std::is_trivially_copyable_v<T>) {
        if (!values.empty()) {
            input.read(
                reinterpret_cast<char*>(values.data()),
                static_cast<std::streamsize>(values.size() * sizeof(T)));
        }
    } else {
        for (auto& value : values) {
            readPod(input, value);
        }
    }
    if (!input) {
        throw std::runtime_error("Snapshot-Vektor ist abgeschnitten");
    }
}

std::string jsonEscape(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (char value : text) {
        switch (value) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += value; break;
        }
    }
    return escaped;
}

double generatedGate(double value) {
    const double kernel =
        tatarus_signal_kernel::kernel(value);
    return std::clamp(
        0.5 * (1.0 + std::tanh(kernel)),
        0.05,
        0.95);
}

std::uint64_t mixHash(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

}  // namespace

struct PersistentNervousSystem::Neuron {
    PopulationRole role = PopulationRole::Excitatory;
    NeuronSubtype subtype = NeuronSubtype::Pyramidal;
    double somaMv = -65.0;
    double dendriteMv = -65.0;
    double gAmpa = 0.0;
    double gNmda = 0.0;
    double gGabaA = 0.0;
    double gGabaB = 0.0;
    double adaptationMv = 0.0;
    double homeostaticBiasMv = 0.0;
    double filteredRateHz = 0.0;
    double fastRateHz = 0.0;
    double slowBaselineHz = 0.0;
    double energy = 1.0;
    double excitability = 1.0;
    int refractorySteps = 0;
    std::uint64_t spikeCount = 0;
    bool active = true;
};

struct PersistentNervousSystem::Synapse {
    std::int64_t parentSynapse = -1;
    std::uint32_t pre = 0;
    std::uint32_t post = 0;
    ReceptorType receptor = ReceptorType::Ampa;
    double weight = 0.0;
    double consolidatedWeight = 0.0;
    double eligibility = 0.0;
    double resource = 1.0;
    double facilitation = 0.0;
    double usage = 0.0;
    std::uint32_t delaySteps = 1;
    std::uint64_t ageSteps = 0;
    bool active = true;
};

struct PersistentNervousSystem::AxonEvent {
    std::uint32_t synapse = 0;
    double amplitude = 0.0;
};

struct PersistentNervousSystem::Assembly {
    std::uint64_t id = 0;
    std::vector<double> prototype;
    double activation = 0.0;
    std::uint64_t observations = 0;
    std::uint64_t lastActiveStep = 0;
};

struct PersistentNervousSystem::SpatialActionEngram {
    std::uint64_t placeKey = 0;
    std::uint64_t assemblyId = 0;
    std::uint64_t visits = 0;
    std::array<std::uint64_t, 4> actionObservations{};
    std::array<double, 4> rewardValue{};
    std::array<double, 4> noveltyValue{};
    std::array<double, 4> frontierValue{};
    std::array<double, 4> successValue{};
    // Long-horizon place/action value learned through the neural eligibility
    // trace. avoidanceValue is a persistent mark for familiar loops, blocked
    // exits and paths that belonged to an explicitly failed episode.
    std::array<double, 4> routeValue{};
    std::array<double, 4> avoidanceValue{};
    std::array<std::uint64_t, 4> failedEpisodeObservations{};
};

struct PersistentNervousSystem::SpatialEligibilityTrace {
    std::uint64_t placeKey = 0;
    std::uint32_t action = 0;
    double eligibility = 0.0;
};

struct PersistentNervousSystem::SpatialEpisodeStep {
    std::uint64_t placeKey = 0;
    std::uint32_t action = 0;
    double reward = 0.0;
    double novelty = 0.0;
};

PersistentNervousSystem::~PersistentNervousSystem() = default;

int NervousSystemConfig::neuronCount() const {
    return sensoryNeurons + excitatoryNeurons + inhibitoryNeurons
        + contextNeurons + motorNeurons + modulatoryNeurons;
}

void NervousSystemConfig::validate() const {
    if (sensoryNeurons < 4
        || excitatoryNeurons < 2
        || inhibitoryNeurons < 1
        || contextNeurons < 1
        || motorNeurons < 4
        || modulatoryNeurons < 1
        || neuronCount() > 65536) {
        throw std::invalid_argument("Ungültige Populationsgrößen");
    }
    const std::array<double, 41> finiteValues{
        dtMs, connectionProbability, restingMv, resetMv, thresholdMv,
        tauSomaMs, tauDendriteMs, somaDendriteCoupling, baseCurrent,
        motorRateScaleHz, tauAmpaMs,
        tauNmdaMs, tauGabaAMs, tauGabaBMs, ampaReversalMv,
        nmdaReversalMv, gabaAReversalMv, gabaBReversalMv, refractoryMs,
        adaptationIncrementMv, adaptationTauMs, targetRateHz,
        homeostasisTauMs, homeostasisGain, eligibilityTauMs,
        eligibilityTransmissionGain, eligibilityIncrement, learningRate,
        consolidationRate, dopamineTauMs, acetylcholineTauMs,
        resourceRecoveryTauMs, facilitationTauMs,
        releaseProbability, structuralIntervalMs, assemblySimilarityThreshold,
        physiologyTimeScale, circadianCycleMs, sleepPressureTauMs,
        nremMinimumMs, remMinimumMs};
    if (!std::all_of(
            finiteValues.begin(),
            finiteValues.end(),
            [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("Nichtendliche Nervensystemparameter");
    }
    if (dtMs <= 0.0
        || connectionProbability < 0.0
        || connectionProbability > 1.0
        || thresholdMv <= restingMv
        || resetMv > restingMv
        || tauSomaMs <= 0.0
        || motorRateScaleHz <= 0.0
        || tauDendriteMs <= 0.0
        || tauAmpaMs <= 0.0
        || tauNmdaMs <= 0.0
        || tauGabaAMs <= 0.0
        || tauGabaBMs <= 0.0
        || refractoryMs < 0.0
        || adaptationTauMs <= 0.0
        || targetRateHz < 0.0
        || homeostasisTauMs <= 0.0
        || eligibilityTauMs <= 0.0
        || eligibilityTransmissionGain < 0.0
        || eligibilityIncrement < 0.0
        || dopamineTauMs <= 0.0
        || acetylcholineTauMs <= 0.0
        || resourceRecoveryTauMs <= 0.0
        || facilitationTauMs <= 0.0
        || releaseProbability < 0.0
        || releaseProbability > 1.0
        || energyRecoveryPerMs < 0.0
        || spikeEnergyCost < 0.0
        || transmissionEnergyCost < 0.0
        || structuralIntervalMs < dtMs
        || maximumNewSynapsesPerInterval < 0
        || maximumAssemblies < 1
        || assemblySimilarityThreshold < 0.0
        || assemblySimilarityThreshold > 1.0
        || physiologyTimeScale <= 0.0
        || circadianCycleMs <= 0.0
        || sleepPressureTauMs <= 0.0
        || nremMinimumMs <= 0.0
        || remMinimumMs <= 0.0) {
        throw std::invalid_argument("Nervensystemparameter außerhalb des Bereichs");
    }
}

MechanismLibrary::MechanismLibrary() {
    entries_ = {
        {
            "generated_polarity_gate",
            "g=clip((1+tanh(K(phi)))/2,0.05,0.95)",
            "praesynaptische Freisetzungsstaerke",
            "phi in [-4,4], g in [0.05,0.95]",
            "Deterministische Gate-, Reset- und Finite-State-Pruefung",
            "validated"},
        {
            "signed_local_eligibility",
            "e<-clip(e*exp(-dt/tau)+post*preTrace-pre*postTrace)",
            "lokaler Zustand jeder aktiven Synapse",
            "tau>0, |e|<=e_max",
            "Trace-essential Recall-XOR: 1.000 vs 0.486111 auf 12 Holdout-Netzen",
            "validated"},
        {
            "short_term_resource",
            "R<-R+(1-R)dt/tau_rec; release=u*R",
            "praesynaptische Vesikelressource",
            "R,u in [0,1]",
            "synthetische Invarianz- und Langzeittests",
            "validated"},
        {
            "rate_homeostasis",
            "theta_h<-theta_h+eta*(rate-target)*dt",
            "neuronale Erregbarkeit",
            "target>=0, theta_h in [-12,12] mV",
            "Stabilitaets- und Stoerungstests",
            "validated"},
        {
            "reward_modulated_consolidation",
            "dw=eta*dopamine*eligibility",
            "lokale Langzeitplastizitaet",
            "|w| begrenzt; Dale-Vorzeichen erhalten",
            "Closed-loop Softwareumwelt",
            "validated"},
        {
            "structural_coactivity",
            "grow/prune=f(trace,usage,age,energy)",
            "rekurrente Topologie",
            "periodisch, deterministisch geseedet",
            "Struktur- und Snapshot-Regression",
            "validated"},
        {
            "axonal_path_repair",
            "w_new<-w_consolidated(parent), delay_new<-max(1,delay_parent-1)",
            "inaktive, zuvor benutzte Sensor-Motor-Bahn",
            "|w_parent|>=0.20, usage_parent>=0.05, Endpunkte aktiv",
            "8/8 Holdout-Seeds: Funktionsverlust und >=70 Prozent Wiedergewinn",
            "validated"},
        {
            "topographic_raw_projection",
            "sensory_channel->overlapping_excitatory_microassembly",
            "rohe multimodale Eingangsprojektion",
            "fanout=5, AMPA/NMDA, geseedete feste Topografie",
            "8/8 Holdout-Seeds; mittlere Transitionserkennung 77.3438 Prozent",
            "validated"},
        {
            "evoked_state_representation",
            "r_evoked=r_fast-r_slow; p includes signed dendritic deviation",
            "Assembly- und Reaktionszustand",
            "tau_slow=5000 ms, signed dendritic state in [-1,1]",
            "8/8 Holdout-Seeds; Reaktivierung 0.907323, nach Schaden 0.852185",
            "validated"},
        {
            "competitive_temporal_assembly",
            "winner=max cosine(p_k,r); update winner or create if sim<threshold",
            "reizphasengebundener Assembly-Katalog",
            "maxAssemblies>=1, similarity in [0,1]",
            "8/8 Holdout-Seeds; im Mittel 6.125 getrennte Assemblies",
            "validated"},
        {
            "restricted_tatarus_cognition",
            "C_t=pool(assemblies,recall,salience,needs,error); X_{t+1}=attention+intent+cue+reward",
            "funktionale Grenze zwischen persistentem Nervensystem und hoeherer KI",
            "64 neuronale und 64 recall-gebundene Synapsenpools; kein Einzelzellzugriff",
            "8/8 Holdout-Seeds: 1.0 vs 0.515625 ohne Trace vs 0.5 ohne Nervensystem",
            "validated"},
        {
            "salience_gated_eligibility_write",
            "e<-decay(e)+1_external_event*local_causality",
            "lokale Eligibility jeder Synapse",
            "write bei Reiz, Recall, Neuheit oder Reward; reiner Zerfall in Leerzeit",
            "Episodisches Signal, Konsolidierung und kontrollierter Zerfall",
            "validated"},
        {
            "sparse_scalable_topology",
            "out_degree=round(p*(N-1)); unique sampled targets for N>2048",
            "Initialisierung grosser rekurrenter Netze",
            "N<=65536, Dale-konforme Rezeptor- und Gewichtszuweisung",
            "Snapshot- und Sicherheitstest bis 65536 Neuronen",
            "validated"},
        {
            "reward_adaptive_cognitive_policy",
            "a=sign(w*phi(C)); w<-decay(w)+eta*reward_inferred_target*phi(C)",
            "hoeherer Kern ueber beschraenkter Cognitive Bridge",
            "begrenzte Gewichte, Exploration, Entscheidung vor Konsequenz",
            "Offene Softwarewelten mit positivem Transferverhalten",
            "validated"},
        {
            "spatial_morphology",
            "P(i->j)=P0*scale(distance); delay=(axon_length/v_conduction)/dt",
            "dreidimensionale Soma- und Dendritengeometrie",
            "7 flache Dendritensegmente je Neuron; keine Pointer-Baeume",
            "C++ determinism, snapshot and finite-state regression",
            "validated"},
        {
            "active_dendritic_compartments",
            "Vseg<-leak+synapse+parent_coupling+local_NMDA_event; Ca<-event+decay",
            "lokale Dendritensegmente vor somatischer Integration",
            "AMPA/NMDA/GABA-A/GABA-B, lokales Ca, Refraktaerzustand",
            "Lokale dendritische Ereignisse und deterministische Fortsetzung",
            "validated"},
        {
            "astrocyte_domain_regulation",
            "Ca_astro<-f(glutamate,K,demand); uptake<-f(Ca_astro)",
            "raumgebundene Glia-Domaenen ueber mehreren Neuronen",
            "12 Neuronen pro Domaene; langsame lokale Regeldynamik",
            "Finite-State- und Persistenzpruefung",
            "validated"},
        {
            "neurovascular_energy_coupling",
            "flow<-f(astro_Ca,demand); O2/glucose<-supply(flow)-consumption",
            "Kapillarversorgung und neuronale Energieerholung",
            "lokale Versorgung skaliert bestehende energyRecoveryPerMs",
            "Snapshot- und dynamische Versorgungspruefung",
            "validated"},
        {
            "oligodendrocyte_myelin_support",
            "reserve<-vascular_supply-metabolic_load; myelin_growth*=reserve",
            "raumgebundene Oligodendrozyten als metabolisch begrenzte Myelinquelle",
            "16 Neuronen pro Oligodendrozyten-Domaene; Reserve in [0.05,1.20]",
            "Adaptive Myelin- und Stoffwechselpruefung",
            "validated"},
        {
            "activity_dependent_myelination",
            "M<-M+dt/tau*(target(use,arrival-post_coincidence)-M); v=f(M,caliber)",
            "axonale Leitungsgeschwindigkeit und effektive Signalverzoegerung",
            "M in [0.03,0.96], v in [90,1150] um/ms, growth resource-gated",
            "Deterministische Snapshot- und Remodeling-Pruefung",
            "validated"},
        {
            "microglial_surveillance_tagging",
            "C3_like<-f(inactivity,damage,inflammation)-functional_protection",
            "raumgebundene Synapsenueberwachung durch Mikroglia-Domaenen",
            "10 Neuronen pro Domaene; complement-like tag in [0,1.5]",
            "Deterministische Schadens-, Ueberwachungs- und Reparaturpruefung",
            "validated"},
        {
            "microglial_structural_maintenance",
            "prune=f(tag,age,usage,weight,activation); repair=f(damage,memory,capacity)",
            "lokaler struktureller Umbau und Wiederherstellung beschaedigter Synapsenlinien",
            "vascular resource-gated repair; parentSynapse lineage preserved",
            "Gezielte Reparatur erhaelt aktive synaptische Abstammungslinien",
            "validated"},
        {
            "ecs_ion_electrodiffusion_and_pump",
            "Jij=D_eff*w_ij*(c_i-c_j); ATPase: 3Na_out/2K_in",
            "gekoppelte intra-/extrazellulaere Kompartimente je Neuron",
            "Na/K/Ca/Cl bounded; Nernst shift in [-10,15] mV",
            "Kager-Wadman-Somjen plus Kirchhoff-Nernst-Planck approximation",
            "validated"},
        {
            "volume_neuromodulation_gpcr",
            "M<-diffusion+release-decay; cAMP/IP3<-receptor_specific(M)",
            "raumgebundene DA/5HT/NA/ACh-Felder",
            "four bounded fields and seeded receptor sensitivities",
            "CAMPER intact-circuit GPCR measurements",
            "validated"},
        {
            "dendritic_channel_gradients",
            "gHCN=1+5*d; gKv=1+4*d; I=g*a(V)*(E-V)",
            "geometrieabhaengige dendritische Integration",
            "d=clamp(mean soma-segment distance/180um)",
            "Magee 1998 and Hoffman et al. 1997",
            "validated"},
        {
            "synaptic_tagging_protein_capture",
            "activity->CREB->mRNA->protein; stable_dw=tag*captured_protein",
            "postsynaptischer Zellkern und einzelne Synapsen",
            "tag tau=3h; bounded protein/capture pools",
            "Frey and Morris 1997",
            "validated"},
        {
            "circadian_sleep_glymphatic_homeostasis",
            "wake_pressure+circadian->NREM/REM; waste_clearance and Homer1a downscale",
            "globaler Gewebezustand mit synapsenspezifischem Schutz",
            "24h default; accelerated experiment parameters supported",
            "Xie et al. 2013 and Diering et al. 2017",
            "validated"},
        {
            "pv_sst_vip_microcircuit",
            "PV->perisomatic fast inhibition; SST->distal; VIP->inhibitory disinhibition",
            "deterministische kortikale Mikroarchitektur",
            "50/30/20 inhibitory split; 40Hz/7Hz phase coupling",
            "Sohal et al. 2009 and Lee et al. 2013",
            "validated"},
        {
            "episodic_successor_memory",
            "T(i->j)<-count, mean_dt, outcome; predict(j|i)=argmax support",
            "zeitliche Kopplung aufeinanderfolgender Assembly-Ereignisse",
            "persistente Transitionen mit mittlerem Intervall und branch confidence",
            "deterministische Sequenz-, Prediction-Hit- und Snapshot-Regression",
            "validated"},
        {
            "prospective_dendritic_priming",
            "D_next += weak prototype(predicted successor); error=f(actual,predicted,timing)",
            "schwache top-down Vorbereitung interner Dendriten plus Prediction Error",
            "Priming unterhalb externer Sensorstaerke; Surprise und ACh-Kopplung begrenzt",
            "Sequenzverletzung erzeugt Prediction Miss und messbaren zeitlichen Surprise-Zustand",
            "validated"}};
}

const std::vector<MechanismDescriptor>& MechanismLibrary::entries() const {
    return entries_;
}

void MechanismLibrary::writeJson(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Mechanismenbibliothek konnte nicht geschrieben werden");
    }
    output << "{\n  \"schema\": \"agns-mechanisms-v1\",\n  \"mechanisms\": [\n";
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        output << "    {\n"
            << "      \"id\": \"" << jsonEscape(entry.id) << "\",\n"
            << "      \"formula\": \"" << jsonEscape(entry.formula) << "\",\n"
            << "      \"placement\": \"" << jsonEscape(entry.placement) << "\",\n"
            << "      \"parameter_range\": \"" << jsonEscape(entry.parameterRange) << "\",\n"
            << "      \"evidence\": \"" << jsonEscape(entry.evidence) << "\",\n"
            << "      \"status\": \"" << jsonEscape(entry.status) << "\"\n"
            << "    }" << (index + 1 == entries_.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
}

PersistentNervousSystem::PersistentNervousSystem(
    NervousSystemConfig config)
    : config_(std::move(config)), random_(config_.seed) {
    config_.validate();
    createNeurons();
    createBiologicalSubstrate();
    prospectiveMemory_ = std::make_unique<temporal::ProspectiveMemory>();
    prospectiveMemory_->setThroughputCounters(&throughput_);
    createSynapses();
    synchronizeBiologicalSubstrate();
    createPhysiologicalSubstrate();
    spikes_.assign(static_cast<std::size_t>(config_.neuronCount()), 0);
    preTrace_.assign(static_cast<std::size_t>(config_.neuronCount()), 0.0);
    postTrace_.assign(static_cast<std::size_t>(config_.neuronCount()), 0.0);
    assemblyAccumulator_.assign(
        static_cast<std::size_t>(config_.neuronCount()),
        0.0);
    assemblyBaseline_.assign(
        static_cast<std::size_t>(config_.neuronCount()),
        config_.restingMv);
    axonQueue_.resize(33);
    updateMetrics();
    biologicalSubstrate_->updateTissueMechanics(
        static_cast<std::size_t>(std::max(0, metrics_.activeSynapses)),
        physiologyMetrics().meanProteinPool,
        physiologyMetrics().extracellularVolumeFraction,
        0.0);
}

const NervousSystemConfig& PersistentNervousSystem::config() const {
    return config_;
}

const NervousSystemMetrics& PersistentNervousSystem::metrics() const {
    return metrics_;
}

const BiologicalMetrics& PersistentNervousSystem::biologicalMetrics() const {
    return biologicalSubstrate_->metrics();
}

const PhysiologyMetrics& PersistentNervousSystem::physiologyMetrics() const {
    return physiologicalSubstrate_->metrics();
}

const ProspectiveMetrics& PersistentNervousSystem::prospectiveMetrics() const {
    return prospectiveMemory_->metrics();
}

PopulationRole PersistentNervousSystem::roleForIndex(int index) const {
    int boundary = config_.sensoryNeurons;
    if (index < boundary) return PopulationRole::Sensory;
    boundary += config_.excitatoryNeurons;
    if (index < boundary) return PopulationRole::Excitatory;
    boundary += config_.inhibitoryNeurons;
    if (index < boundary) return PopulationRole::Inhibitory;
    boundary += config_.contextNeurons;
    if (index < boundary) return PopulationRole::Context;
    boundary += config_.motorNeurons;
    if (index < boundary) return PopulationRole::Motor;
    return PopulationRole::Modulatory;
}

NeuronSubtype PersistentNervousSystem::subtypeForIndex(int index) const {
    const auto role = roleForIndex(index);
    if (role == PopulationRole::Sensory) return NeuronSubtype::Sensory;
    if (role == PopulationRole::Modulatory) return NeuronSubtype::Neuromodulatory;
    if (role != PopulationRole::Inhibitory) return NeuronSubtype::Pyramidal;
    const int inhibitoryStart = config_.sensoryNeurons + config_.excitatoryNeurons;
    const int local = std::max(0, index - inhibitoryStart);
    const int slot = local % 10;
    if (slot < 5) return NeuronSubtype::Parvalbumin;
    if (slot < 8) return NeuronSubtype::Somatostatin;
    return NeuronSubtype::Vip;
}

void PersistentNervousSystem::createBiologicalSubstrate() {
    std::vector<PopulationRole> roles;
    roles.reserve(neurons_.size());
    for (const auto& neuron : neurons_) roles.push_back(neuron.role);
    biologicalSubstrate_ = std::make_unique<biology::SpatialSubstrate>(
        neurons_.size(),
        roles,
        config_.seed,
        config_.restingMv,
        config_.dtMs);
    biologicalSubstrate_->setThroughputCounters(&throughput_);
}

void PersistentNervousSystem::createPhysiologicalSubstrate() {
    std::vector<PopulationRole> roles;
    std::vector<NeuronSubtype> subtypes;
    roles.reserve(neurons_.size());
    subtypes.reserve(neurons_.size());
    for (const auto& neuron : neurons_) {
        roles.push_back(neuron.role);
        subtypes.push_back(neuron.subtype);
    }
    physiologicalSubstrate_ = std::make_unique<physiology::PhysiologicalSubstrate>(
        config_, inspectSpatial(), roles, subtypes);
    physiologicalSubstrate_->setThroughputCounters(&throughput_);
}

void PersistentNervousSystem::synchronizeBiologicalSubstrate() {
    throughput_.recordReadWrite<Synapse>(tatarus::ThroughputDomain::Synapse, synapses_.size());
    if (!biologicalSubstrate_) createBiologicalSubstrate();
    biologicalSubstrate_->truncateBindings(synapses_.size());
    for (std::size_t index = 0; index < synapses_.size(); ++index) {
        auto& synapse = synapses_[index];
        biologicalSubstrate_->synchronizeSynapse(
            index, synapse.pre, synapse.post);
        synapse.delaySteps = biologicalSubstrate_->conductionDelaySteps(
            synapse.pre, synapse.post, config_.dtMs);
    }
    biologicalSubstrate_->refreshMetrics();
}

void PersistentNervousSystem::createNeurons() {
    neurons_.resize(static_cast<std::size_t>(config_.neuronCount()));
    std::uniform_real_distribution<double> jitter(-1.0, 1.0);
    for (int index = 0; index < config_.neuronCount(); ++index) {
        auto& neuron = neurons_[static_cast<std::size_t>(index)];
        neuron.role = roleForIndex(index);
        neuron.subtype = subtypeForIndex(index);
        neuron.somaMv = config_.restingMv + jitter(random_);
        neuron.dendriteMv = config_.restingMv + jitter(random_);
        neuron.energy = 0.9 + 0.1 * (0.5 + 0.5 * jitter(random_));
    }
}

bool PersistentNervousSystem::connectionExists(int pre, int post) const {
    return std::any_of(
        synapses_.begin(),
        synapses_.end(),
        [pre, post](const Synapse& synapse) {
            return synapse.active
                && synapse.pre == static_cast<std::uint32_t>(pre)
                && synapse.post == static_cast<std::uint32_t>(post);
        });
}

void PersistentNervousSystem::createSynapses() {
    synapses_.clear();
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_real_distribution<double> excitatoryWeight(0.035, 0.11);
    std::uniform_real_distribution<double> inhibitoryWeight(0.045, 0.13);
    const int count = config_.neuronCount();
    const auto appendSynapse = [&](int pre, int post) {
        Synapse synapse;
        synapse.pre = static_cast<std::uint32_t>(pre);
        synapse.post = static_cast<std::uint32_t>(post);
        synapse.delaySteps = biologicalSubstrate_->conductionDelaySteps(
            static_cast<std::size_t>(pre),
            static_cast<std::size_t>(post),
            config_.dtMs);
        const bool inhibitory =
            roleForIndex(pre) == PopulationRole::Inhibitory;
        if (inhibitory) {
            synapse.weight = -inhibitoryWeight(random_);
            const auto subtype = subtypeForIndex(pre);
            const double fastFraction = subtype == NeuronSubtype::Parvalbumin
                ? 0.96 : subtype == NeuronSubtype::Somatostatin ? 0.42 : 0.78;
            synapse.receptor =
                unit(random_) < fastFraction
                    ? ReceptorType::GabaA
                    : ReceptorType::GabaB;
        } else {
            synapse.weight = excitatoryWeight(random_);
            synapse.receptor =
                unit(random_) < 0.75
                    ? ReceptorType::Ampa
                    : ReceptorType::Nmda;
        }
        if (roleForIndex(pre) == PopulationRole::Modulatory) {
            synapse.receptor = ReceptorType::Modulatory;
            synapse.weight = std::abs(synapse.weight);
        }
        if (roleForIndex(post) == PopulationRole::Motor) {
            synapse.weight *= 0.15;
        }
        synapse.consolidatedWeight = synapse.weight;
        synapses_.push_back(synapse);
    };
    constexpr int denseConnectivityLimit = 768;
    if (count <= denseConnectivityLimit) {
        for (int pre = 0; pre < count; ++pre) {
            for (int post = 0; post < count; ++post) {
                const double spatialProbability = std::clamp(
                    config_.connectionProbability
                        * biologicalSubstrate_->connectionScale(
                            static_cast<std::size_t>(pre),
                            static_cast<std::size_t>(post))
                        * ([&]() {
                            const auto subtype = subtypeForIndex(pre);
                            const auto postRole = roleForIndex(post);
                            if (subtype == NeuronSubtype::Vip) {
                                return postRole == PopulationRole::Inhibitory ? 1.85 : 0.38;
                            }
                            if (subtype == NeuronSubtype::Parvalbumin) {
                                return postRole == PopulationRole::Excitatory
                                    || postRole == PopulationRole::Motor ? 1.45 : 0.72;
                            }
                            if (subtype == NeuronSubtype::Somatostatin) {
                                return postRole == PopulationRole::Excitatory
                                    || postRole == PopulationRole::Context ? 1.35 : 0.70;
                            }
                            return 1.0;
                        })(),
                    0.0,
                    0.95);
                if (pre == post
                    || unit(random_) > spatialProbability) {
                    continue;
                }
                appendSynapse(pre, post);
            }
        }
    } else {
        const int outgoingPerNeuron = std::clamp(
            static_cast<int>(std::llround(
                config_.connectionProbability
                * 256.0)),
            8,
            std::min(32, count - 1));
        synapses_.reserve(
            static_cast<std::size_t>(count)
            * static_cast<std::size_t>(outgoingPerNeuron + 1));
        std::uniform_int_distribution<int> postChoice(0, count - 1);
        for (int pre = 0; pre < count; ++pre) {
            std::unordered_set<int> selected;
            selected.reserve(
                static_cast<std::size_t>(outgoingPerNeuron * 2 + 1));
            while (static_cast<int>(selected.size()) < outgoingPerNeuron) {
                int bestPost = -1;
                double bestScore = -1.0;
                // A small deterministic candidate tournament approximates a
                // spatial index while keeping construction O(N). Local and
                // region-compatible targets win most tournaments; sparse
                // long-range projections remain possible.
                for (int candidate = 0; candidate < 8; ++candidate) {
                    const int post = postChoice(random_);
                    if (post == pre || selected.contains(post)) continue;
                    const double score = biologicalSubstrate_->connectionScale(
                        static_cast<std::size_t>(pre),
                        static_cast<std::size_t>(post))
                        * (0.75 + 0.50 * unit(random_));
                    if (score > bestScore) {
                        bestScore = score;
                        bestPost = post;
                    }
                }
                if (bestPost < 0 || !selected.insert(bestPost).second) continue;
                appendSynapse(pre, bestPost);
            }
        }
    }

    // Deterministische topografische Sensorprojektion: Jeder Rohkanal
    // rekrutiert eine kleine, überlappende exzitatorische Mikroassembly.
    // Sie ersetzt keine lernbare Verbindung, sondern stellt sicher, dass
    // unterschiedliche Modalitätsereignisse überhaupt interne Kausalpfade
    // erreichen.
    constexpr int sensoryFanout = 5;
    for (int sensory = 0; sensory < config_.sensoryNeurons; ++sensory) {
        for (int branch = 0; branch < sensoryFanout; ++branch) {
            const int offset =
                (sensory * 7 + branch * 3) % config_.excitatoryNeurons;
            const int post = config_.sensoryNeurons + offset;
            if (count <= denseConnectivityLimit && connectionExists(sensory, post)) continue;
            Synapse projection;
            projection.pre = static_cast<std::uint32_t>(sensory);
            projection.post = static_cast<std::uint32_t>(post);
            projection.receptor =
                branch == sensoryFanout - 1
                    ? ReceptorType::Nmda
                    : ReceptorType::Ampa;
            projection.weight = 0.50;
            projection.consolidatedWeight = projection.weight;
            projection.delaySteps = biologicalSubstrate_->conductionDelaySteps(
                static_cast<std::size_t>(sensory),
                static_cast<std::size_t>(post),
                config_.dtMs);
            synapses_.push_back(projection);
        }
    }

    const int motorStart =
        config_.sensoryNeurons + config_.excitatoryNeurons
        + config_.inhibitoryNeurons + config_.contextNeurons;
    // Four causal sensorimotor channels. Sensory channels 0..3 represent
    // north/east/south/west goal direction and project to matching interleaved
    // motor pools. Reward-dependent plasticity acts on these real synapses.
    for (int direction = 0; direction < 4; ++direction) {
        const int sensory = direction;
        for (int motor = direction; motor < config_.motorNeurons; motor += 4) {
            const int post = motorStart + motor;
            if (post >= motorStart + config_.motorNeurons) {
                continue;
            }
            Synapse scaffold;
            scaffold.pre = static_cast<std::uint32_t>(sensory);
            scaffold.post = static_cast<std::uint32_t>(post);
            scaffold.receptor = ReceptorType::Ampa;
            scaffold.weight = 1.20;
            scaffold.consolidatedWeight = scaffold.weight;
            scaffold.delaySteps = biologicalSubstrate_->conductionDelaySteps(
                static_cast<std::size_t>(sensory),
                static_cast<std::size_t>(post),
                config_.dtMs);
            synapses_.push_back(scaffold);
        }
    }
    rebuildOutgoing();
}

void PersistentNervousSystem::rebuildOutgoing() {
    outgoing_.assign(
        static_cast<std::size_t>(config_.neuronCount()),
        {});
    for (std::size_t index = 0; index < synapses_.size(); ++index) {
        const auto& synapse = synapses_[index];
        if (synapse.active && synapse.pre < outgoing_.size()) {
            outgoing_[synapse.pre].push_back(index);
        }
    }
}

PersistentNervousSystem::SpatialActionEngram*
PersistentNervousSystem::spatialEngram(std::uint64_t placeKey) {
    if (placeKey == 0) return nullptr;
    std::uint64_t visited = 0;
    for (auto& engram : spatialActionMemory_) {
        ++visited;
        if (engram.placeKey == placeKey) {
            throughput_.recordRead<SpatialActionEngram>(
                tatarus::ThroughputDomain::SpatialMemory, visited);
            return &engram;
        }
    }
    throughput_.recordRead<SpatialActionEngram>(
        tatarus::ThroughputDomain::SpatialMemory, visited);
    return nullptr;
}

const PersistentNervousSystem::SpatialActionEngram*
PersistentNervousSystem::spatialEngram(std::uint64_t placeKey) const {
    if (placeKey == 0) return nullptr;
    std::uint64_t visited = 0;
    for (const auto& engram : spatialActionMemory_) {
        ++visited;
        if (engram.placeKey == placeKey) {
            throughput_.recordRead<SpatialActionEngram>(
                tatarus::ThroughputDomain::SpatialMemory, visited);
            return &engram;
        }
    }
    throughput_.recordRead<SpatialActionEngram>(
        tatarus::ThroughputDomain::SpatialMemory, visited);
    return nullptr;
}

PersistentNervousSystem::SpatialActionEngram&
PersistentNervousSystem::ensureSpatialEngram(std::uint64_t placeKey) {
    if (auto* existing = spatialEngram(placeKey)) return *existing;
    SpatialActionEngram created;
    created.placeKey = placeKey;
    spatialActionMemory_.push_back(created);
    throughput_.recordWrite<SpatialActionEngram>(
        tatarus::ThroughputDomain::SpatialMemory);
    return spatialActionMemory_.back();
}

void PersistentNervousSystem::updateSpatialCue(const SensorFrame& frame) {
    if (frame.spatialContext.empty()) {
        spatialCueWasPresent_ = false;
        return;
    }

    // Quantise before hashing so tiny floating-point differences in sensor
    // transport cannot split one physical place into many engrams.  The hash
    // represents the distributed grid-cell pattern itself; it is not a world
    // coordinate and cannot be converted back into a map position.
    std::uint64_t signature = 14695981039346656037ULL;
    if (frame.spatialEpisodeContext != 0) {
        signature = mixHash(
            signature,
            &frame.spatialEpisodeContext,
            sizeof(frame.spatialEpisodeContext));
    }
    const std::uint64_t count = frame.spatialContext.size();
    signature = mixHash(signature, &count, sizeof(count));
    for (const double raw : frame.spatialContext) {
        const auto quantized = static_cast<std::int16_t>(std::llround(
            std::clamp(raw, -1.0, 1.0) * 2047.0));
        signature = mixHash(signature, &quantized, sizeof(quantized));
    }
    if (signature == 0) signature = 1;
    recognizedPlaceKey_ = signature;

    if (!spatialCueWasPresent_ || signature != lastSpatialCueSignature_) {
        if (learningEnabled_) {
            auto& engram = ensureSpatialEngram(signature);
            if (engram.visits > 0) ++spatialRevisits_;
            ++engram.visits;
            throughput_.recordReadWrite<SpatialActionEngram>(
                tatarus::ThroughputDomain::SpatialMemory);
        }
        lastSpatialCueSignature_ = signature;
        spatialCueWasPresent_ = true;
    }
}

void PersistentNervousSystem::noteSpatialAssembly(std::uint64_t assemblyId) {
    if (assemblyId == 0) return;
    recognizedAssemblyId_ = assemblyId;
    if (learningEnabled_ && recognizedPlaceKey_ != 0) {
        ensureSpatialEngram(recognizedPlaceKey_).assemblyId = assemblyId;
        throughput_.recordWrite<SpatialActionEngram>(
            tatarus::ThroughputDomain::SpatialMemory);
    }
}

void PersistentNervousSystem::decaySpatialEligibility() {
    throughput_.recordReadWrite<SpatialEligibilityTrace>(
        tatarus::ThroughputDomain::SpatialMemory, spatialEligibilityTrace_.size());
    for (auto& trace : spatialEligibilityTrace_) {
        trace.eligibility *= kSpatialTraceDecay;
    }
    spatialEligibilityTrace_.erase(
        std::remove_if(
            spatialEligibilityTrace_.begin(), spatialEligibilityTrace_.end(),
            [](const SpatialEligibilityTrace& trace) {
                return trace.eligibility < kSpatialTraceFloor;
            }),
        spatialEligibilityTrace_.end());
    if (spatialEligibilityTrace_.size() > kMaximumSpatialTraceEntries) {
        spatialEligibilityTrace_.erase(
            spatialEligibilityTrace_.begin(),
            spatialEligibilityTrace_.begin()
                + static_cast<std::ptrdiff_t>(
                    spatialEligibilityTrace_.size() - kMaximumSpatialTraceEntries));
    }
}

void PersistentNervousSystem::reinforceSpatialTrace(double novelty, double success) {
    const double boundedNovelty = std::clamp(novelty, 0.0, 1.0);
    const double boundedSuccess = std::clamp(success, 0.0, 1.0);
    if (boundedNovelty <= 1e-9 && boundedSuccess <= 1e-9) return;

    throughput_.recordRead<SpatialEligibilityTrace>(
        tatarus::ThroughputDomain::SpatialMemory, spatialEligibilityTrace_.size());
    for (const auto& trace : spatialEligibilityTrace_) {
        auto& engram = ensureSpatialEngram(trace.placeKey);
        throughput_.recordReadWrite<SpatialActionEngram>(
            tatarus::ThroughputDomain::SpatialMemory);
        const auto action = static_cast<std::size_t>(trace.action % 4U);
        if (boundedNovelty > 0.0) {
            const double gain = kSpatialNoveltyBackpropGain
                * boundedNovelty * trace.eligibility;
            engram.frontierValue[action] = std::clamp(
                engram.frontierValue[action]
                    + gain * (1.0 - engram.frontierValue[action]),
                0.0, 1.0);
        }
        if (boundedSuccess > 0.0) {
            const double gain = kSpatialSuccessBackpropGain
                * boundedSuccess * trace.eligibility;
            engram.successValue[action] = std::clamp(
                engram.successValue[action]
                    + gain * (1.0 - engram.successValue[action]),
                0.0, 1.0);
        }
    }
}

std::array<double, 4>
PersistentNervousSystem::spatialMotorBias(std::uint64_t placeKey) const {
    std::array<double, 4> bias{};
    const auto* engram = spatialEngram(placeKey);
    if (!engram) return bias;

    const double familiarity = 1.0 - std::exp(
        -static_cast<double>(engram->visits) / 3.0);
    for (std::size_t action = 0; action < bias.size(); ++action) {
        const auto observations = engram->actionObservations[action];
        const double confidence = 1.0 - std::exp(
            -static_cast<double>(observations) / 2.5);
        const double unknownBonus = observations == 0
            ? 0.16 + 0.10 * familiarity
            : 0.0;
        const double repeatedFamiliar = confidence
            * (1.0 - engram->noveltyValue[action])
            * (1.0 - engram->frontierValue[action])
            * (1.0 - engram->successValue[action]);

        bias[action] =
            unknownBonus
            + 0.24 * engram->rewardValue[action]
            + 0.28 * engram->noveltyValue[action]
            + 0.42 * engram->frontierValue[action]
            + 0.82 * engram->successValue[action]
            + 0.68 * engram->routeValue[action]
            - 0.88 * engram->avoidanceValue[action]
            - 0.30 * repeatedFamiliar;
    }

    // Only the relative action preference should enter the motor pools.  By
    // removing the common mode the memory cannot globally excite all four
    // directions and therefore remains an action selector rather than a hidden
    // motor-current source.
    const double mean = std::accumulate(bias.begin(), bias.end(), 0.0)
        / static_cast<double>(bias.size());
    for (auto& value : bias) {
        value = std::clamp(value - mean, -0.90, 0.95);
    }
    return bias;
}

void PersistentNervousSystem::beginEmbodiedAction(std::uint32_t actionIndex) {
    if (!learningEnabled_) {
        pendingSpatialActionValid_ = false;
        return;
    }
    throughput_.record(
        tatarus::ThroughputDomain::SpatialMemory, 3, sizeof(std::uint64_t), false, true);
    pendingSpatialPlaceKey_ = recognizedPlaceKey_;
    pendingSpatialAction_ = actionIndex % 4U;
    pendingSpatialActionValid_ = pendingSpatialPlaceKey_ != 0;
}

void PersistentNervousSystem::endEmbodiedAction(
    double reward,
    double success,
    double novelty) {
    if (!learningEnabled_ || !pendingSpatialActionValid_ || pendingSpatialPlaceKey_ == 0) return;

    const auto action = static_cast<std::size_t>(pendingSpatialAction_ % 4U);
    auto& engram = ensureSpatialEngram(pendingSpatialPlaceKey_);
    throughput_.recordReadWrite<SpatialActionEngram>(
        tatarus::ThroughputDomain::SpatialMemory);
    const auto observations = ++engram.actionObservations[action];
    const double learningRate = std::max(
        0.045,
        kSpatialDirectLearningRate
            / std::sqrt(static_cast<double>(std::max<std::uint64_t>(1, observations))));
    const double boundedReward = std::clamp(reward, -1.0, 1.0);
    const double boundedNovelty = std::clamp(novelty, 0.0, 1.0);
    const double boundedSuccess = std::clamp(success, 0.0, 1.0);

    engram.rewardValue[action] += learningRate
        * (boundedReward - engram.rewardValue[action]);
    engram.noveltyValue[action] += learningRate
        * (boundedNovelty - engram.noveltyValue[action]);
    engram.rewardValue[action] = std::clamp(engram.rewardValue[action], -1.0, 1.0);
    engram.noveltyValue[action] = std::clamp(engram.noveltyValue[action], 0.0, 1.0);

    // A direct familiar transition should slowly lose frontier salience, but a
    // previously successful route is deliberately not erased by familiarity.
    if (boundedNovelty < 0.25) {
        engram.frontierValue[action] *= 0.985;
    } else {
        ++spatialNovelDiscoveries_;
    }

    // Immediate negative/familiar outcomes form an avoidance engram. Positive
    // goal evidence can extinguish it again, so a useful route is not banned
    // merely because it becomes familiar after learning.
    if (boundedSuccess > 0.5) {
        engram.avoidanceValue[action] *= 0.55;
    } else {
        const double negativeEvidence = std::clamp(-2.4 * boundedReward, 0.0, 1.0);
        const double familiarEvidence = boundedNovelty < 0.25
            ? std::clamp(4.0 * (0.02 - boundedReward), 0.0, 1.0)
            : 0.0;
        const double avoidanceGain = 0.22 * negativeEvidence
            + 0.035 * familiarEvidence;
        engram.avoidanceValue[action] = std::clamp(
            engram.avoidanceValue[action]
                + avoidanceGain * (1.0 - engram.avoidanceValue[action]),
            0.0, 1.0);
    }

    decaySpatialEligibility();
    auto duplicate = std::find_if(
        spatialEligibilityTrace_.begin(), spatialEligibilityTrace_.end(),
        [&](const SpatialEligibilityTrace& trace) {
            return trace.placeKey == pendingSpatialPlaceKey_
                && trace.action == pendingSpatialAction_;
        });
    if (duplicate != spatialEligibilityTrace_.end()) {
        duplicate->eligibility = 1.0;
        throughput_.recordWrite<SpatialEligibilityTrace>(
            tatarus::ThroughputDomain::SpatialMemory);
    } else {
        spatialEligibilityTrace_.push_back(SpatialEligibilityTrace{
            pendingSpatialPlaceKey_, pendingSpatialAction_, 1.0});
        throughput_.recordWrite<SpatialEligibilityTrace>(
            tatarus::ThroughputDomain::SpatialMemory);
    }

    // TD(lambda)-style route learning: the place/action engram stores not only
    // the immediate reward but the future value recalled at the next place.
    // This is the missing link that lets a late goal or dead end change earlier
    // neural decisions along the route.
    double futureValue = 0.0;
    if (boundedSuccess <= 0.5) {
        if (const auto* next = spatialEngram(recognizedPlaceKey_)) {
            for (std::size_t nextAction = 0; nextAction < 4; ++nextAction) {
                if (next->actionObservations[nextAction] > 0) {
                    futureValue = std::max(futureValue, next->routeValue[nextAction]);
                }
            }
        }
    }
    const double temporalTarget = std::clamp(
        boundedReward + kSpatialTemporalDiscount * futureValue,
        -1.0, 1.0);
    const double temporalError = temporalTarget - engram.routeValue[action];
    for (const auto& trace : spatialEligibilityTrace_) {
        auto& tracedEngram = ensureSpatialEngram(trace.placeKey);
        const auto tracedAction = static_cast<std::size_t>(trace.action % 4U);
        tracedEngram.routeValue[tracedAction] = std::clamp(
            tracedEngram.routeValue[tracedAction]
                + kSpatialTemporalLearningRate * temporalError * trace.eligibility,
            -1.0, 1.0);
        throughput_.recordReadWrite<SpatialActionEngram>(
            tatarus::ThroughputDomain::SpatialMemory);
    }

    spatialEpisodeTrace_.push_back(SpatialEpisodeStep{
        pendingSpatialPlaceKey_, pendingSpatialAction_, boundedReward, boundedNovelty});
    if (spatialEpisodeTrace_.size() > kMaximumSpatialEpisodeEntries) {
        spatialEpisodeTrace_.erase(spatialEpisodeTrace_.begin());
    }
    throughput_.recordWrite<SpatialEpisodeStep>(
        tatarus::ThroughputDomain::SpatialMemory);

    reinforceSpatialTrace(boundedNovelty, boundedSuccess);
    if (boundedSuccess > 0.5) ++spatialSuccessfulConsolidations_;
    ++spatialActionUpdates_;

    pendingSpatialActionValid_ = false;
}

void PersistentNervousSystem::endEmbodiedEpisode(bool reachedGoal) {
    pendingSpatialActionValid_ = false;
    if (!learningEnabled_) return;
    if (spatialEpisodeTrace_.empty()) return;

    if (reachedGoal) {
        // Loop erasure retains the actions that actually make up the successful
        // route. Earlier departures from a place that later had to be revisited
        // are not accidentally consolidated as part of the solution.
        std::vector<SpatialEpisodeStep> route;
        route.reserve(spatialEpisodeTrace_.size());
        for (const auto& step : spatialEpisodeTrace_) {
            const auto repeatedPlace = std::find_if(
                route.begin(), route.end(),
                [&](const SpatialEpisodeStep& candidate) {
                    return candidate.placeKey == step.placeKey;
                });
            if (repeatedPlace != route.end()) route.erase(repeatedPlace, route.end());
            route.push_back(step);
        }

        double credit = 1.0;
        for (auto item = route.rbegin(); item != route.rend(); ++item) {
            auto& engram = ensureSpatialEngram(item->placeKey);
            const auto action = static_cast<std::size_t>(item->action % 4U);
            const double gain = 0.30 * credit;
            engram.successValue[action] = std::clamp(
                engram.successValue[action]
                    + gain * (1.0 - engram.successValue[action]),
                0.0, 1.0);
            engram.routeValue[action] = std::clamp(
                engram.routeValue[action]
                    + gain * (credit - engram.routeValue[action]),
                -1.0, 1.0);
            engram.avoidanceValue[action] *= std::max(0.0, 1.0 - 0.65 * credit);
            credit *= kSpatialTemporalDiscount;
            throughput_.recordReadWrite<SpatialActionEngram>(
                tatarus::ThroughputDomain::SpatialMemory);
        }
    } else {
        struct FailedStepSummary {
            std::uint64_t placeKey = 0;
            std::uint32_t action = 0;
            std::uint64_t occurrences = 0;
            double rewardSum = 0.0;
            double noveltySum = 0.0;
        };
        std::vector<FailedStepSummary> summaries;
        summaries.reserve(spatialEpisodeTrace_.size());
        for (const auto& step : spatialEpisodeTrace_) {
            auto summary = std::find_if(
                summaries.begin(), summaries.end(),
                [&](const FailedStepSummary& candidate) {
                    return candidate.placeKey == step.placeKey
                        && candidate.action == step.action;
                });
            if (summary == summaries.end()) {
                summaries.push_back(FailedStepSummary{
                    step.placeKey, step.action, 1, step.reward, step.novelty});
            } else {
                ++summary->occurrences;
                summary->rewardSum += step.reward;
                summary->noveltySum += step.novelty;
            }
        }

        // Every place/action pair is retained as belonging to a failed route,
        // while repeated familiar loops and negative exits receive the largest
        // avoidance mark. Unique exploratory transitions remain recoverable.
        for (const auto& summary : summaries) {
            auto& engram = ensureSpatialEngram(summary.placeKey);
            const auto action = static_cast<std::size_t>(summary.action % 4U);
            const double count = static_cast<double>(summary.occurrences);
            const double meanReward = summary.rewardSum / count;
            const double meanNovelty = summary.noveltySum / count;
            const double repeated = 1.0 - std::exp(
                -0.70 * static_cast<double>(summary.occurrences - 1));
            const double familiar = std::clamp(1.0 - meanNovelty, 0.0, 1.0);
            const double negative = std::clamp(-3.0 * meanReward, 0.0, 1.0);
            const double failureSignal = std::clamp(
                0.10 + 0.48 * repeated + 0.25 * familiar + 0.30 * negative,
                0.0, 1.0);
            const double gain = 0.04 + 0.16 * failureSignal;
            engram.avoidanceValue[action] = std::clamp(
                engram.avoidanceValue[action]
                    + gain * (1.0 - engram.avoidanceValue[action]),
                0.0, 1.0);
            const double failedTarget = -0.20 - 0.35 * failureSignal;
            engram.routeValue[action] = std::clamp(
                engram.routeValue[action]
                    + 0.18 * (failedTarget - engram.routeValue[action]),
                -1.0, 1.0);
            ++engram.failedEpisodeObservations[action];
            throughput_.recordReadWrite<SpatialActionEngram>(
                tatarus::ThroughputDomain::SpatialMemory);
        }

        // A terminal negative prediction error additionally reaches the recent
        // causal chain through the neural eligibility trace.
        for (const auto& trace : spatialEligibilityTrace_) {
            auto& engram = ensureSpatialEngram(trace.placeKey);
            const auto action = static_cast<std::size_t>(trace.action % 4U);
            engram.routeValue[action] = std::clamp(
                engram.routeValue[action]
                    + 0.20 * (-0.45 - engram.routeValue[action]) * trace.eligibility,
                -1.0, 1.0);
        }
        ++spatialFailedConsolidations_;
    }

    throughput_.recordRead<SpatialEpisodeStep>(
        tatarus::ThroughputDomain::SpatialMemory, spatialEpisodeTrace_.size());
    // The host may teleport/reset the body between episodes. Do not let the
    // final place assembly mislabel the first action after that discontinuity.
    spatialEpisodeTrace_.clear();
    spatialEligibilityTrace_.clear();
    recognizedPlaceKey_ = 0;
    recognizedAssemblyId_ = 0;
    lastSpatialCueSignature_ = 0;
    spatialCueWasPresent_ = false;
}

SpatialMemoryMetrics PersistentNervousSystem::spatialMemoryMetrics() const {
    SpatialMemoryMetrics out;
    out.available = true;
    out.learningEnabled = learningEnabled_;
    out.engrams = spatialActionMemory_.size();
    out.actionUpdates = spatialActionUpdates_;
    out.novelDiscoveries = spatialNovelDiscoveries_;
    out.revisits = spatialRevisits_;
    out.successfulConsolidations = spatialSuccessfulConsolidations_;
    out.failedConsolidations = spatialFailedConsolidations_;
    out.currentPlaceKey = recognizedPlaceKey_;
    out.currentAssemblyId = recognizedAssemblyId_;
    const auto* engram = spatialEngram(recognizedPlaceKey_);
    if (!engram) return out;

    out.currentVisits = engram->visits;
    out.currentFamiliarity = 1.0 - std::exp(
        -static_cast<double>(engram->visits) / 3.0);
    out.rewardValue = engram->rewardValue;
    out.noveltyValue = engram->noveltyValue;
    out.frontierValue = engram->frontierValue;
    out.successValue = engram->successValue;
    out.routeValue = engram->routeValue;
    out.avoidanceValue = engram->avoidanceValue;
    out.actionObservations = engram->actionObservations;
    out.failedEpisodeObservations = engram->failedEpisodeObservations;
    out.motorBias = spatialMotorBias(recognizedPlaceKey_);
    const auto preferred = std::max_element(out.motorBias.begin(), out.motorBias.end());
    out.preferredDirection = static_cast<std::uint32_t>(
        std::distance(out.motorBias.begin(), preferred));
    auto ordered = out.motorBias;
    std::sort(ordered.begin(), ordered.end(), std::greater<>());
    const std::uint64_t totalObservations = std::accumulate(
        engram->actionObservations.begin(), engram->actionObservations.end(),
        std::uint64_t{0});
    out.confidence = std::clamp(
        (ordered[0] - ordered[1])
            * (1.0 - std::exp(-static_cast<double>(totalObservations) / 4.0)),
        0.0, 1.0);
    return out;
}

void PersistentNervousSystem::encodeSensors(
    const SensorFrame& frame,
    std::vector<double>& somaDrive,
    std::vector<double>& dendriteDrive) const {
    const int sensory = config_.sensoryNeurons;
    auto addChannel = [&](int channel, double value, double scale) {
        if (sensory <= 0) return;
        const std::size_t index =
            static_cast<std::size_t>((channel % sensory + sensory) % sensory);
        somaDrive[index] += scale * std::clamp(value, -1.0, 1.0);
    };
    for (std::size_t index = 0; index < frame.visionEvents.size(); ++index) {
        addChannel(
            static_cast<int>(index),
            frame.visionEvents[index],
            32.0);
    }
    for (std::size_t index = 0; index < frame.audioSamples.size(); ++index) {
        addChannel(
            4 + static_cast<int>(index),
            frame.audioSamples[index],
            24.0);
    }
    for (std::size_t index = 0; index < frame.touch.size(); ++index) {
        addChannel(
            8 + static_cast<int>(index),
            frame.touch[index],
            28.0);
    }
    for (std::size_t index = 0; index < frame.vestibularEvents.size(); ++index) {
        addChannel(
            12 + static_cast<int>(index),
            frame.vestibularEvents[index],
            30.0);
    }
    for (std::uint8_t byte : frame.textBytes) {
        for (int bit = 0; bit < 8; ++bit) {
            const double value = ((byte >> bit) & 1U) ? 1.0 : -0.35;
            addChannel(12 + bit, value, 18.0);
        }
    }
    addChannel(sensory - 2, frame.temperature, 18.0);
    addChannel(
        sensory - 1,
        2.0 * std::clamp(frame.internalEnergy, 0.0, 1.0) - 1.0,
        16.0);

    const int contextStart =
        config_.sensoryNeurons + config_.excitatoryNeurons
        + config_.inhibitoryNeurons;
    for (int offset = 0; offset < config_.contextNeurons; ++offset) {
        dendriteDrive[static_cast<std::size_t>(contextStart + offset)] +=
            8.0 * frame.novelty
            * std::sin((offset + 1) * 0.5);
        if (!frame.contextEvents.empty()) {
            const auto channel = static_cast<std::size_t>(
                offset % static_cast<int>(frame.contextEvents.size()));
            dendriteDrive[static_cast<std::size_t>(contextStart + offset)] +=
                10.0
                * std::clamp(frame.contextEvents[channel], -1.0, 1.0)
                * ((offset / static_cast<int>(frame.contextEvents.size())) % 2
                    == 0 ? 1.0 : -0.35);
        }
        if (!frame.spatialContext.empty()) {
            const auto channel = static_cast<std::size_t>(
                offset % static_cast<int>(frame.spatialContext.size()));
            dendriteDrive[static_cast<std::size_t>(contextStart + offset)] +=
                13.0
                * std::clamp(frame.spatialContext[channel], -1.0, 1.0)
                * ((offset / static_cast<int>(frame.spatialContext.size())) % 2
                    == 0 ? 1.0 : -0.45);
        }
    }
    // A learned successor is not injected as a synthetic sensory event.  It
    // arrives as a weak top-down dendritic bias over the internal assembly
    // population, so an expected continuation becomes easier to reactivate
    // without overriding current sensor evidence.
    if (prospectiveMemory_) {
        const auto& prime = prospectiveMemory_->primingPattern();
        const int firstInternal = config_.sensoryNeurons;
        const int lastInternal = config_.sensoryNeurons
            + config_.excitatoryNeurons
            + config_.inhibitoryNeurons
            + config_.contextNeurons;
        const auto expectedSize = static_cast<std::size_t>(lastInternal - firstInternal);
        if (prime.size() == expectedSize) {
            for (std::size_t feature = 0; feature < prime.size(); ++feature) {
                const auto neuron = static_cast<std::size_t>(firstInternal) + feature;
                dendriteDrive[neuron] += 0.35 * prime[feature];
            }
        }
    }

    // Recalled place-action value is injected as top-down dendritic current into
    // the real motor-neuron pools.  The memory therefore changes the neural
    // dynamics before spike/rate integration; decodeAction() remains a pure
    // readout and contains no hidden route controller.
    const auto memoryBias = spatialMotorBias(recognizedPlaceKey_);
    const int motorStart = config_.neuronCount()
        - config_.modulatoryNeurons - config_.motorNeurons;
    for (int offset = 0; offset < config_.motorNeurons; ++offset) {
        const auto direction = static_cast<std::size_t>(offset % 4);
        const auto neuron = static_cast<std::size_t>(motorStart + offset);
        dendriteDrive[neuron] += 96.0 * memoryBias[direction];
        somaDrive[neuron] += 22.0 * memoryBias[direction];
    }

    const int modStart =
        config_.neuronCount() - config_.modulatoryNeurons;
    for (int offset = 0; offset < config_.modulatoryNeurons; ++offset) {
        somaDrive[static_cast<std::size_t>(modStart + offset)] +=
            12.0 * frame.reward
            + 7.0 * frame.novelty;
    }
}

void PersistentNervousSystem::deliverAxonEvents(
    const SensorFrame& frame) {
    const bool plasticityWrite = hasPlasticityWriteSignal(frame);
    auto& events = axonQueue_[
        static_cast<std::size_t>(metrics_.step % axonQueue_.size())];
    throughput_.recordRead<AxonEvent>(tatarus::ThroughputDomain::AxonEvent, events.size());
    for (const auto& event : events) {
        if (event.synapse >= synapses_.size()) {
            continue;
        }
        throughput_.recordReadWrite<Synapse>(tatarus::ThroughputDomain::Synapse);
        Synapse& synapse = synapses_[event.synapse];
        if (!synapse.active || synapse.post >= neurons_.size()) {
            continue;
        }
        auto& post = neurons_[synapse.post];
        if (learningEnabled_ && config_.eligibilityMemoryEnabled && plasticityWrite) {
            const double localPostState = std::tanh(
                (post.dendriteMv - config_.restingMv) / 8.0);
            synapse.eligibility = std::clamp(
                synapse.eligibility
                    + 0.02 * config_.eligibilityIncrement
                        * localPostState,
                -4.0,
                4.0);
        }
        biologicalSubstrate_->recordAxonArrival(event.synapse, event.amplitude);
        switch (synapse.receptor) {
            case ReceptorType::Ampa:
                biologicalSubstrate_->deposit(
                    event.synapse, biology::SynapticChannel::Ampa, event.amplitude,
                    0.25 * config_.transmissionEnergyCost);
                break;
            case ReceptorType::Nmda:
                biologicalSubstrate_->deposit(
                    event.synapse, biology::SynapticChannel::Nmda, event.amplitude,
                    0.25 * config_.transmissionEnergyCost);
                break;
            case ReceptorType::GabaA:
                biologicalSubstrate_->deposit(
                    event.synapse, biology::SynapticChannel::GabaA, event.amplitude,
                    0.25 * config_.transmissionEnergyCost);
                break;
            case ReceptorType::GabaB:
                biologicalSubstrate_->deposit(
                    event.synapse, biology::SynapticChannel::GabaB, event.amplitude,
                    0.25 * config_.transmissionEnergyCost);
                break;
            case ReceptorType::Modulatory:
                acetylcholine_ += 0.02 * event.amplitude;
                break;
        }
        if (config_.energyRegulationEnabled) {
            post.energy = std::max(
                0.0,
                post.energy - config_.transmissionEnergyCost);
        }
        ++metrics_.totalTransmissions;
    }
    events.clear();
}

void PersistentNervousSystem::updateNeurons(
    const std::vector<double>& somaDrive,
    const std::vector<double>& dendriteDrive) {
    throughput_.recordReadWrite<Neuron>(tatarus::ThroughputDomain::Neuron, neurons_.size());
    const double adaptationDecay =
        std::exp(-config_.dtMs / config_.adaptationTauMs);
    const biology::ElectrophysiologyParameters dendriteParameters{
        config_.dtMs,
        config_.restingMv,
        config_.tauDendriteMs,
        config_.somaDendriteCoupling,
        config_.tauAmpaMs,
        config_.tauNmdaMs,
        config_.tauGabaAMs,
        config_.tauGabaBMs,
        config_.ampaReversalMv,
        config_.nmdaReversalMv,
        config_.gabaAReversalMv,
        config_.gabaBReversalMv};
    const int refractoryDuration = std::max(
        1,
        static_cast<int>(std::ceil(
            config_.refractoryMs / config_.dtMs)));
    std::fill(spikes_.begin(), spikes_.end(), static_cast<std::uint8_t>(0));
    for (std::size_t index = 0; index < neurons_.size(); ++index) {
        auto& neuron = neurons_[index];
        if (!neuron.active) {
            neuron.somaMv = config_.restingMv;
            neuron.dendriteMv = config_.restingMv;
            neuron.gAmpa = 0.0;
            neuron.gNmda = 0.0;
            neuron.gGabaA = 0.0;
            neuron.gGabaB = 0.0;
            neuron.filteredRateHz = 0.0;
            neuron.fastRateHz = 0.0;
            continue;
        }
        neuron.adaptationMv *= adaptationDecay;
        const double physiologyDendrite = physiologicalSubstrate_
            ? physiologicalSubstrate_->dendriteBiasMv(index)
            : 0.0;
        const auto dendrite = biologicalSubstrate_->updateNeuron(
            index, neuron.somaMv, dendriteDrive[index] + physiologyDendrite,
            dendriteParameters);
        neuron.dendriteMv = dendrite.membraneMv;
        neuron.gAmpa = dendrite.gAmpa;
        neuron.gNmda = dendrite.gNmda;
        neuron.gGabaA = dendrite.gGabaA;
        neuron.gGabaB = dendrite.gGabaB;
        if (neuron.refractorySteps > 0) {
            --neuron.refractorySteps;
            neuron.somaMv = config_.resetMv;
            continue;
        }
        const double energyFactor =
            config_.energyRegulationEnabled
                ? std::clamp(
                    neuron.energy * (physiologicalSubstrate_
                        ? physiologicalSubstrate_->metabolicScale(index)
                        : 1.0),
                    0.08, 1.0)
                : 1.0;
        const double physiologySoma = physiologicalSubstrate_
            ? physiologicalSubstrate_->somaBiasMv(index)
            : 0.0;
        neuron.somaMv +=
            config_.dtMs / config_.tauSomaMs
            * (
                config_.restingMv - neuron.somaMv
                + somaDrive[index]
                + physiologySoma
                + config_.baseCurrent * neuron.excitability
                + config_.somaDendriteCoupling
                    * (neuron.dendriteMv - neuron.somaMv));
        const double threshold =
            config_.thresholdMv
            + neuron.adaptationMv
            + neuron.homeostaticBiasMv
            + (1.0 - energyFactor) * 8.0;
        if (neuron.somaMv >= threshold) {
            spikes_[index] = 1;
            neuron.somaMv = config_.resetMv;
            neuron.refractorySteps = refractoryDuration;
            neuron.adaptationMv += config_.adaptationIncrementMv;
            ++neuron.spikeCount;
            ++metrics_.totalSpikes;
            if (config_.energyRegulationEnabled) {
                neuron.energy = std::max(
                    0.0,
                    neuron.energy - config_.spikeEnergyCost);
            }
        }
    }
}

void PersistentNervousSystem::scheduleSpikeEvents() {
    for (std::size_t pre = 0; pre < spikes_.size(); ++pre) {
        if (!spikes_[pre] || !neurons_[pre].active) {
            continue;
        }
        for (std::size_t synapseIndex : outgoing_[pre]) {
            throughput_.recordReadWrite<Synapse>(tatarus::ThroughputDomain::Synapse);
            auto& synapse = synapses_[synapseIndex];
            if (!synapse.active) continue;
            const double utilization = std::clamp(
                config_.releaseProbability + synapse.facilitation,
                0.01,
                0.95);
            const double gate =
                config_.generatedOperatorEnabled
                    ? generatedGate(
                        config_.eligibilityMemoryEnabled
                            ? synapse.eligibility
                            : 0.0)
                    : 1.0;
            const double eligibilityModulation =
                config_.eligibilityMemoryEnabled
                    ? std::clamp(
                        std::exp(
                            config_.eligibilityTransmissionGain
                            * std::tanh(synapse.eligibility)),
                        0.05,
                        8.00)
                    : 1.0;
            const double amplitude =
                std::abs(synapse.weight)
                * utilization
                * synapse.resource
                * gate
                * eligibilityModulation;
            biologicalSubstrate_->recordAxonUse(synapseIndex, amplitude);
            const std::uint32_t effectiveDelay =
                biologicalSubstrate_->effectiveConductionDelaySteps(
                    synapseIndex, synapse.delaySteps, config_.dtMs);
            const std::size_t slot = static_cast<std::size_t>(
                (metrics_.step + effectiveDelay)
                % axonQueue_.size());
            axonQueue_[slot].push_back(AxonEvent{
                static_cast<std::uint32_t>(synapseIndex),
                amplitude});
            if (config_.shortTermPlasticityEnabled) {
                synapse.resource *= (1.0 - utilization);
                synapse.facilitation = std::clamp(
                    synapse.facilitation
                        + 0.12 * (1.0 - synapse.facilitation),
                    0.0,
                    0.8);
            }
            synapse.usage += 1.0;
        }
    }
}

void PersistentNervousSystem::updatePlasticity(const SensorFrame& frame) {
    throughput_.recordReadWrite<Synapse>(tatarus::ThroughputDomain::Synapse, synapses_.size());
    throughput_.recordRead<Neuron>(tatarus::ThroughputDomain::Neuron, neurons_.size());
    const double traceDecay = std::exp(-config_.dtMs / 20.0);
    const double eligibilityDecay =
        std::exp(-config_.dtMs / config_.eligibilityTauMs);
    const double resourceRecovery =
        config_.dtMs / config_.resourceRecoveryTauMs;
    const double facilitationDecay =
        std::exp(-config_.dtMs / config_.facilitationTauMs);
    const bool plasticityWrite = hasPlasticityWriteSignal(frame);
    for (std::size_t index = 0; index < neurons_.size(); ++index) {
        preTrace_[index] *= traceDecay;
        postTrace_[index] *= traceDecay;
    }
    dopamine_ =
        dopamine_ * std::exp(-config_.dtMs / config_.dopamineTauMs)
        + 0.6 * frame.reward;
    acetylcholine_ =
        acetylcholine_ * std::exp(
            -config_.dtMs / config_.acetylcholineTauMs)
        + 0.25 * std::abs(frame.novelty);
    dopamine_ = std::clamp(dopamine_, -2.0, 2.0);
    acetylcholine_ = std::clamp(acetylcholine_, 0.0, 2.0);
    for (auto& synapse : synapses_) {
        if (!synapse.active) continue;
        const std::size_t pre = synapse.pre;
        const std::size_t post = synapse.post;
        const double causal = spikes_[post] * preTrace_[pre];
        const double antiCausal = spikes_[pre] * postTrace_[post];
        synapse.eligibility = config_.eligibilityMemoryEnabled
            ? std::clamp(
                synapse.eligibility * eligibilityDecay
                    + (plasticityWrite ? config_.eligibilityIncrement : 0.0)
                        * (causal - antiCausal),
                -4.0,
                4.0)
            : 0.0;
        synapse.resource = std::clamp(
            synapse.resource
                + resourceRecovery * (1.0 - synapse.resource),
            0.0,
            1.0);
        synapse.facilitation *= facilitationDecay;
        synapse.usage *= std::exp(-config_.dtMs / 5000.0);
        ++synapse.ageSteps;
        if (learningEnabled_ && config_.longTermPlasticityEnabled) {
            const bool inhibitory =
                neurons_[pre].role == PopulationRole::Inhibitory;
            const double sign = inhibitory ? -1.0 : 1.0;
            const bool sensoryProjection =
                neurons_[pre].role == PopulationRole::Sensory;
            const bool sensoryMotor =
                sensoryProjection
                && neurons_[synapse.post].role == PopulationRole::Motor;
            const double plasticitySignal = sensoryMotor
                ? std::abs(synapse.eligibility)
                : synapse.eligibility;
            double magnitude = std::abs(synapse.weight);
            magnitude +=
                config_.learningRate
                * dopamine_
                * plasticitySignal
                * (sensoryMotor ? 100.0 : 1.0);
            magnitude +=
                0.02 * config_.learningRate
                * acetylcholine_
                * std::abs(synapse.eligibility);
            magnitude = std::clamp(
                magnitude,
                0.001,
                sensoryProjection ? 1.20 : 0.35);
            synapse.weight = sign * magnitude;
            if (dopamine_ > 0.0) {
                synapse.consolidatedWeight +=
                    config_.consolidationRate
                    * dopamine_
                    * (synapse.weight - synapse.consolidatedWeight);
            } else {
                synapse.weight +=
                    config_.consolidationRate
                    * (synapse.consolidatedWeight - synapse.weight);
            }
            if (!inhibitory) {
                synapse.weight = std::abs(synapse.weight);
            } else {
                synapse.weight = -std::abs(synapse.weight);
            }
        }
    }
    for (std::size_t index = 0; index < neurons_.size(); ++index) {
        preTrace_[index] += spikes_[index];
        postTrace_[index] += spikes_[index];
    }
    if (learningEnabled_ && physiologicalSubstrate_) {
        std::vector<physiology::SynapseCoupling> coupling;
        coupling.reserve(synapses_.size());
        for (const auto& synapse : synapses_) {
            coupling.push_back(physiology::SynapseCoupling{
                synapse.pre,
                synapse.post,
                synapse.eligibility,
                synapse.weight,
                synapse.pre < neurons_.size()
                    && neurons_[synapse.pre].role == PopulationRole::Inhibitory,
                synapse.active});
        }
        const auto effects = physiologicalSubstrate_->updateSynapses(coupling);
        const std::size_t count = std::min(effects.size(), synapses_.size());
        for (std::size_t index = 0; index < count; ++index) {
            auto& synapse = synapses_[index];
            if (!synapse.active) continue;
            const bool inhibitory = neurons_[synapse.pre].role == PopulationRole::Inhibitory;
            const double sign = inhibitory ? -1.0 : 1.0;
            const double ceiling = neurons_[synapse.pre].role == PopulationRole::Sensory
                ? 1.20 : 0.35;
            double consolidated = std::abs(synapse.consolidatedWeight)
                + effects[index].consolidationGain;
            consolidated = std::clamp(consolidated, 0.001, ceiling);
            synapse.consolidatedWeight = sign * consolidated * effects[index].weightScale;
            synapse.weight = sign * std::clamp(
                std::abs(synapse.weight) * effects[index].weightScale,
                0.001, ceiling);
        }
    }
}

void PersistentNervousSystem::updateAssemblies(const SensorFrame& frame) {
    throughput_.record(tatarus::ThroughputDomain::Cognition, assemblies_.size(), sizeof(Assembly), true, true);
    throughput_.recordRead<Neuron>(tatarus::ThroughputDomain::Neuron, neurons_.size());
    const auto nonzero = [](const auto& values) {
        return std::any_of(values.begin(), values.end(), [](const auto value) {
            return std::abs(static_cast<double>(value)) > 1e-12;
        });
    };
    const bool stimulusPresent =
        nonzero(frame.visionEvents)
        || nonzero(frame.audioSamples)
        || nonzero(frame.touch)
        || nonzero(frame.vestibularEvents)
        || nonzero(frame.spatialContext)
        || !frame.textBytes.empty();
    std::uint64_t signature = 14695981039346656037ULL;
    const auto hashVector = [&signature](const auto& values) {
        const auto size = values.size();
        signature = mixHash(signature, &size, sizeof(size));
        if (!values.empty()) {
            signature = mixHash(
                signature,
                values.data(),
                values.size() * sizeof(values.front()));
        }
    };
    hashVector(frame.visionEvents);
    hashVector(frame.audioSamples);
    hashVector(frame.touch);
    hashVector(frame.vestibularEvents);
    hashVector(frame.spatialContext);
    hashVector(frame.textBytes);
    hashVector(frame.contextEvents);
    if (!stimulusPresent) {
        stimulusWasPresent_ = false;
        stimulusAgeSteps_ = 0;
        std::fill(
            assemblyAccumulator_.begin(),
            assemblyAccumulator_.end(),
            0.0);
    } else if (!stimulusWasPresent_ || signature != stimulusSignature_) {
        stimulusWasPresent_ = true;
        stimulusSignature_ = signature;
        stimulusAgeSteps_ = 1;
        std::fill(
            assemblyAccumulator_.begin(),
            assemblyAccumulator_.end(),
            0.0);
        for (std::size_t index = 0; index < neurons_.size(); ++index) {
            assemblyBaseline_[index] = neurons_[index].dendriteMv;
        }
    } else {
        ++stimulusAgeSteps_;
    }
    const int samplingAge = std::max(
        2,
        static_cast<int>(std::round(22.0 / config_.dtMs)));
    const int first =
        config_.sensoryNeurons;
    const int last =
        config_.sensoryNeurons + config_.excitatoryNeurons
        + config_.inhibitoryNeurons + config_.contextNeurons;
    std::vector<double> pattern(
        static_cast<std::size_t>(last - first),
        0.0);
    int active = 0;
    for (int neuron = first; neuron < last; ++neuron) {
        const auto index = static_cast<std::size_t>(neuron);
        const double evokedRate = std::max(
            0.0,
            neurons_[index].filteredRateHz - neurons_[index].slowBaselineHz);
        const double dendriticActivation = std::clamp(
            (neurons_[index].dendriteMv - assemblyBaseline_[index]) / 4.0,
            -1.0,
            1.0);
        const double rateActivation = std::clamp(
            evokedRate / std::max(1.0, config_.targetRateHz),
            0.0,
            1.0);
        const double instantaneous = spikes_[index]
            ? 1.0
            : (
                std::abs(dendriticActivation) > rateActivation
                    ? dendriticActivation
                    : rateActivation);
        assemblyAccumulator_[index] += instantaneous;
        const double activation =
            assemblyAccumulator_[index]
            / static_cast<double>(std::max(1, stimulusAgeSteps_));
        pattern[static_cast<std::size_t>(neuron - first)] = activation;
        if (std::abs(activation) > 0.02) {
            ++active;
        }
    }
    metrics_.activeAssembly = -1;
    for (auto& assembly : assemblies_) {
        assembly.activation *= 0.96;
    }
    if (
        !stimulusPresent
        || stimulusAgeSteps_ != samplingAge
        || active < 2) {
        return;
    }
    double bestSimilarity = -1.0;
    int bestIndex = -1;
    for (std::size_t index = 0; index < assemblies_.size(); ++index) {
        const auto& prototype = assemblies_[index].prototype;
        double dot = 0.0;
        double normPattern = 0.0;
        double normPrototype = 0.0;
        for (std::size_t feature = 0; feature < pattern.size(); ++feature) {
            dot += pattern[feature] * prototype[feature];
            normPattern += pattern[feature] * pattern[feature];
            normPrototype += prototype[feature] * prototype[feature];
        }
        const double similarity =
            dot / (std::sqrt(normPattern * normPrototype) + 1e-12);
        if (similarity > bestSimilarity) {
            bestSimilarity = similarity;
            bestIndex = static_cast<int>(index);
        }
    }
    int activatedIndex = -1;
    if (
        bestIndex < 0
        || (
            bestSimilarity < config_.assemblySimilarityThreshold
            && assemblies_.size()
                < static_cast<std::size_t>(config_.maximumAssemblies))) {
        if (!learningEnabled_) return;
        Assembly assembly;
        assembly.id =
            assemblies_.empty() ? 1 : assemblies_.back().id + 1;
        assembly.prototype = pattern;
        assembly.activation = 1.0;
        assembly.observations = 1;
        assembly.lastActiveStep = metrics_.step;
        assemblies_.push_back(std::move(assembly));
        metrics_.activeAssembly =
            static_cast<int>(assemblies_.size() - 1);
        activatedIndex = metrics_.activeAssembly;
    } else {
        auto& assembly = assemblies_[static_cast<std::size_t>(bestIndex)];
        if (learningEnabled_) {
            const double rate =
                1.0 / static_cast<double>(std::min<std::uint64_t>(
                    assembly.observations + 1,
                    100));
            for (std::size_t feature = 0; feature < pattern.size(); ++feature) {
                assembly.prototype[feature] +=
                    rate * (pattern[feature] - assembly.prototype[feature]);
            }
            ++assembly.observations;
        }
        assembly.activation = 1.0;
        assembly.lastActiveStep = metrics_.step;
        metrics_.activeAssembly = bestIndex;
        activatedIndex = bestIndex;
    }

    if (activatedIndex >= 0 && prospectiveMemory_) {
        const auto errorBefore = prospectiveMemory_->metrics().predictionError;
        const auto& activated = assemblies_[static_cast<std::size_t>(activatedIndex)];
        noteSpatialAssembly(activated.id);
        if (learningEnabled_) {
            prospectiveMemory_->observe(
                activated.id,
                activated.prototype,
                metrics_.step,
                config_.dtMs,
                frame.reward,
                frame.novelty);
        } else {
            prospectiveMemory_->recall(
                activated.id,
                activated.prototype,
                metrics_.step,
                config_.dtMs);
        }
        const auto errorAfter = prospectiveMemory_->metrics().predictionError;
        if (errorAfter > errorBefore + 1e-9) {
            // Unexpected continuations raise the same novelty-oriented
            // neuromodulatory channel that already controls plasticity.
            acetylcholine_ += 0.20 * errorAfter;
        }
    } else if (activatedIndex >= 0) {
        const auto& activated = assemblies_[static_cast<std::size_t>(activatedIndex)];
        noteSpatialAssembly(activated.id);
    }
}

void PersistentNervousSystem::updateHomeostasisAndEnergy() {
    throughput_.recordReadWrite<Neuron>(tatarus::ThroughputDomain::Neuron, neurons_.size());
    std::vector<double> currentRates;
    currentRates.reserve(neurons_.size());
    for (const auto& neuron : neurons_) currentRates.push_back(neuron.filteredRateHz);
    biologicalSubstrate_->updateGliaAndVasculature(
        spikes_, currentRates, config_.dtMs);

    const double rateAlpha =
        1.0 - std::exp(-config_.dtMs / config_.homeostasisTauMs);
    for (std::size_t index = 0; index < neurons_.size(); ++index) {
        auto& neuron = neurons_[index];
        if (!neuron.active) {
            neuron.filteredRateHz = 0.0;
            continue;
        }
        const double instantaneousRate =
            spikes_[index] ? 1000.0 / config_.dtMs : 0.0;
        const double fastAlpha =
            1.0 - std::exp(-config_.dtMs / 20.0);
        neuron.fastRateHz +=
            fastAlpha * (instantaneousRate - neuron.fastRateHz);
        neuron.filteredRateHz +=
            rateAlpha * (instantaneousRate - neuron.filteredRateHz);
        const double baselineAlpha =
            1.0 - std::exp(-config_.dtMs / 5000.0);
        neuron.slowBaselineHz += baselineAlpha
            * (neuron.filteredRateHz - neuron.slowBaselineHz);
        if (learningEnabled_ && config_.homeostasisEnabled) {
            neuron.homeostaticBiasMv = std::clamp(
                neuron.homeostaticBiasMv
                    + config_.homeostasisGain
                        * (neuron.filteredRateHz - config_.targetRateHz)
                        * config_.dtMs,
                -12.0,
                12.0);
        }
        if (config_.energyRegulationEnabled) {
            const double vascularSupport =
                biologicalSubstrate_->energyRecoveryScale(index);
            neuron.energy = std::clamp(
                neuron.energy
                    + config_.energyRecoveryPerMs * vascularSupport,
                0.0,
                1.0);
        } else {
            neuron.energy = 1.0;
        }
    }
}

void PersistentNervousSystem::updateStructuralPlasticity() {
    throughput_.recordReadWrite<Synapse>(tatarus::ThroughputDomain::Synapse, synapses_.size());
    if (!learningEnabled_ || !config_.structuralPlasticityEnabled) return;
    const std::uint64_t interval = static_cast<std::uint64_t>(std::max(
        1.0,
        std::round(config_.structuralIntervalMs / config_.dtMs)));
    if (metrics_.step == 0
        || metrics_.step - lastStructuralStep_ < interval) {
        return;
    }
    lastStructuralStep_ = metrics_.step;
    int pruned = 0;
    for (std::size_t index = 0; index < synapses_.size(); ++index) {
        auto& synapse = synapses_[index];
        if (!synapse.active) continue;
        const double agePressure = std::clamp(
            static_cast<double>(synapse.ageSteps > interval * 2
                ? synapse.ageSteps - interval * 2
                : 0U)
                / static_cast<double>(std::max<std::uint64_t>(1U, interval * 8U)),
            0.0,
            1.0);
        const double usageProtection = std::clamp(
            synapse.usage / std::max(0.001, config_.pruneUsageThreshold * 8.0),
            0.0,
            1.0);
        const double weightProtection = std::clamp(
            std::abs(synapse.weight)
                / std::max(0.001, config_.pruneWeightThreshold * 2.0),
            0.0,
            1.0);
        const double maintenanceDrive =
            biologicalSubstrate_->microglialPruningDrive(
                index, agePressure, usageProtection, weightProtection);
        if (synapse.ageSteps > interval * 2
            && maintenanceDrive > 0.24) {
            synapse.active = false;
            biologicalSubstrate_->recordMicroglialPruning(index);
            ++pruned;
        }
    }
    std::uniform_int_distribution<int> preChoice(
        0,
        config_.neuronCount() - config_.modulatoryNeurons - 1);
    std::uniform_int_distribution<int> postChoice(
        config_.sensoryNeurons,
        config_.neuronCount() - config_.modulatoryNeurons - 1);
    int grown = 0;
    const std::size_t dormantCount = synapses_.size();
    for (std::size_t index = 0;
         index < dormantCount
            && grown < config_.maximumNewSynapsesPerInterval;
         ++index) {
        const auto& dormant = synapses_[index];
        const double consolidatedStrength = std::clamp(
            std::abs(dormant.consolidatedWeight) / 0.20, 0.0, 1.0);
        const double usageEvidence = std::clamp(dormant.usage / 0.20, 0.0, 1.0);
        const double repairDrive = biologicalSubstrate_->microglialRepairDrive(
            index, consolidatedStrength, usageEvidence);
        if (dormant.active
            || dormant.pre >= neurons_.size()
            || dormant.post >= neurons_.size()
            || !neurons_[dormant.pre].active
            || !neurons_[dormant.post].active
            || std::abs(dormant.consolidatedWeight) < 0.035
            || dormant.usage < 0.01
            || repairDrive < 0.34
            || connectionExists(
                static_cast<int>(dormant.pre),
                static_cast<int>(dormant.post))) {
            continue;
        }
        if (!biologicalSubstrate_->canAllocateSynapticMaterial()) {
            biologicalSubstrate_->recordGrowthLimited();
            break;
        }
        Synapse replacement;
        replacement.parentSynapse = static_cast<std::int64_t>(index);
        replacement.pre = dormant.pre;
        replacement.post = dormant.post;
        replacement.receptor = dormant.receptor;
        replacement.weight = dormant.consolidatedWeight;
        replacement.consolidatedWeight = replacement.weight;
        replacement.delaySteps = biologicalSubstrate_->conductionDelaySteps(
            replacement.pre, replacement.post, config_.dtMs);
        synapses_.push_back(replacement);
        biologicalSubstrate_->recordMicroglialRepair(index);
        ++grown;
    }
    int attempts = 0;
    while (
        grown < config_.maximumNewSynapsesPerInterval
        && attempts < config_.maximumNewSynapsesPerInterval * 40) {
        if (!biologicalSubstrate_->canAllocateSynapticMaterial()) {
            biologicalSubstrate_->recordGrowthLimited();
            break;
        }
        ++attempts;
        const int pre = preChoice(random_);
        const int post = postChoice(random_);
        if (pre == post || connectionExists(pre, post)) continue;
        const double coactivity =
            preTrace_[static_cast<std::size_t>(pre)]
            * postTrace_[static_cast<std::size_t>(post)];
        const bool sensoryMotor =
            roleForIndex(pre) == PopulationRole::Sensory
            && roleForIndex(post) == PopulationRole::Motor;
        const double repairDrive = sensoryMotor
            ? 0.20
                + 0.15 * std::abs(dopamine_)
                + 0.10 * acetylcholine_
            : 0.0;
        std::uniform_real_distribution<double> acceptance(0.0, 1.0);
        if (acceptance(random_) > std::clamp(
                0.05 + 0.2 * coactivity + repairDrive,
                0.0,
                0.9)) {
            continue;
        }
        Synapse synapse;
        synapse.pre = static_cast<std::uint32_t>(pre);
        synapse.post = static_cast<std::uint32_t>(post);
        synapse.delaySteps = biologicalSubstrate_->conductionDelaySteps(
            synapse.pre, synapse.post, config_.dtMs);
        const bool inhibitory =
            roleForIndex(pre) == PopulationRole::Inhibitory;
        synapse.weight = inhibitory
            ? -0.035
            : (sensoryMotor ? 0.30 : 0.03);
        synapse.consolidatedWeight = synapse.weight;
        synapse.receptor =
            inhibitory ? ReceptorType::GabaA : ReceptorType::Ampa;
        synapses_.push_back(synapse);
        ++grown;
    }
    metrics_.structuralGrowth += grown;
    metrics_.structuralPruning += pruned;
    rebuildOutgoing();
    synchronizeBiologicalSubstrate();
}

MotorAction PersistentNervousSystem::decodeAction() const {
    MotorAction action;
    const int motorStart =
        config_.sensoryNeurons + config_.excitatoryNeurons
        + config_.inhibitoryNeurons + config_.contextNeurons;
    const int half = config_.motorNeurons / 2;
    double left = 0.0;
    double right = 0.0;
    for (int offset = 0; offset < config_.motorNeurons; ++offset) {
        const double rate = neurons_[
            static_cast<std::size_t>(motorStart + offset)].fastRateHz;
        if (offset < half) left += rate;
        else right += rate;
    }
    left /= std::max(1, half);
    right /= std::max(1, config_.motorNeurons - half);
    double leftPotential = 0.0;
    double rightPotential = 0.0;
    for (int offset = 0; offset < config_.motorNeurons; ++offset) {
        const auto& neuron = neurons_[
            static_cast<std::size_t>(motorStart + offset)];
        const double normalized = std::clamp(
            (neuron.dendriteMv - config_.restingMv)
                / (config_.thresholdMv - config_.restingMv),
            0.0,
            1.0);
        if (offset < half) leftPotential += normalized;
        else rightPotential += normalized;
    }
    leftPotential /= std::max(1, half);
    rightPotential /= std::max(1, config_.motorNeurons - half);
    action.movement = std::tanh(
        (right - left) / config_.motorRateScaleHz
            + 0.25 * (rightPotential - leftPotential));
    action.confidence =
        std::clamp((left + right) / 40.0, 0.0, 1.0);
    action.attention =
        std::clamp(acetylcholine_ / 2.0, 0.0, 1.0);
    const int modStart =
        config_.neuronCount() - config_.modulatoryNeurons;
    double modRate = 0.0;
    for (int index = modStart; index < config_.neuronCount(); ++index) {
        modRate += neurons_[static_cast<std::size_t>(index)].filteredRateHz;
    }
    action.vocalization = std::tanh(
        modRate / std::max(1, config_.modulatoryNeurons) / 20.0);

    // Four independently observable motor pools provide a stable embodied
    // readout for north/east/south/west controllers. This does not inject a
    // planner into the substrate: each value is computed only from the live
    // firing and dendritic state of its motor-neuron pool.
    for (std::size_t direction = 0; direction < action.directionalActivity.size(); ++direction) {
        double rate = 0.0;
        double potential = 0.0;
        int count = 0;
        for (int offset = static_cast<int>(direction); offset < config_.motorNeurons; offset += 4) {
            const auto& neuron = neurons_[static_cast<std::size_t>(motorStart + offset)];
            rate += neuron.fastRateHz;
            potential += std::clamp(
                (neuron.dendriteMv - config_.restingMv)
                    / (config_.thresholdMv - config_.restingMv),
                0.0,
                1.0);
            ++count;
        }
        const double divisor = static_cast<double>(std::max(1, count));
        const double meanRate = rate / divisor;
        const double meanPotential = potential / divisor;
        action.directionalActivity[direction] = std::clamp(
            0.78 * (1.0 - std::exp(-meanRate / config_.motorRateScaleHz))
                + 0.22 * meanPotential,
            0.0,
            1.0);
    }

    const auto selected = std::max_element(
        action.directionalActivity.begin(),
        action.directionalActivity.end());
    action.selectedDirection = static_cast<std::uint32_t>(
        std::distance(action.directionalActivity.begin(), selected));
    auto ordered = action.directionalActivity;
    std::sort(ordered.begin(), ordered.end(), std::greater<>());
    action.confidence = std::clamp(
        std::max(action.confidence, ordered[0] - ordered[1]),
        0.0,
        1.0);
    return action;
}

void PersistentNervousSystem::updateMetrics() {
    throughput_.recordRead<Synapse>(tatarus::ThroughputDomain::Synapse, synapses_.size());
    throughput_.recordRead<Neuron>(tatarus::ThroughputDomain::Neuron, neurons_.size());
    metrics_.activeSynapses = 0;
    metrics_.meanEligibility = 0.0;
    metrics_.meanResource = 0.0;
    metrics_.meanRateHz = 0.0;
    metrics_.meanEnergy = 0.0;
    metrics_.finite = true;
    for (const auto& synapse : synapses_) {
        if (!synapse.active) continue;
        ++metrics_.activeSynapses;
        metrics_.meanEligibility += synapse.eligibility;
        metrics_.meanResource += synapse.resource;
        metrics_.finite =
            metrics_.finite
            && std::isfinite(synapse.weight)
            && std::isfinite(synapse.eligibility)
            && std::isfinite(synapse.resource);
    }
    if (metrics_.activeSynapses > 0) {
        metrics_.meanEligibility /= metrics_.activeSynapses;
        metrics_.meanResource /= metrics_.activeSynapses;
    }
    for (const auto& neuron : neurons_) {
        metrics_.meanRateHz += neuron.filteredRateHz;
        metrics_.meanEnergy += neuron.energy;
        metrics_.finite =
            metrics_.finite
            && std::isfinite(neuron.somaMv)
            && std::isfinite(neuron.dendriteMv)
            && std::isfinite(neuron.energy);
    }
    metrics_.meanRateHz /= neurons_.size();
    metrics_.meanEnergy /= neurons_.size();
    metrics_.assemblyCount = static_cast<int>(assemblies_.size());
    metrics_.dopamine = dopamine_;
    metrics_.acetylcholine = acetylcholine_;
}

MotorAction PersistentNervousSystem::step(const SensorFrame& frame) {
    ++throughput_.neuralTicks;
    if (
        !std::isfinite(frame.temperature)
        || !std::isfinite(frame.internalEnergy)
        || !std::isfinite(frame.reward)
        || !std::isfinite(frame.novelty)
        || !std::all_of(
            frame.visionEvents.begin(),
            frame.visionEvents.end(),
            [](double value) { return std::isfinite(value); })
        || !std::all_of(
            frame.audioSamples.begin(),
            frame.audioSamples.end(),
            [](double value) { return std::isfinite(value); })
        || !std::all_of(
            frame.touch.begin(),
            frame.touch.end(),
            [](double value) { return std::isfinite(value); })
        || !std::all_of(
            frame.vestibularEvents.begin(),
            frame.vestibularEvents.end(),
            [](double value) { return std::isfinite(value); })
        || !std::all_of(
            frame.spatialContext.begin(),
            frame.spatialContext.end(),
            [](double value) { return std::isfinite(value); })
        || !std::all_of(
            frame.contextEvents.begin(),
            frame.contextEvents.end(),
            [](double value) { return std::isfinite(value); })) {
        throw std::invalid_argument("Sensorframe enthält nichtendliche Werte");
    }
    updateSpatialCue(frame);
    std::vector<double> somaDrive(neurons_.size(), 0.0);
    std::vector<double> dendriteDrive(neurons_.size(), 0.0);
    if (prospectiveMemory_) {
        prospectiveMemory_->tick(metrics_.step, config_.dtMs);
    }
    deliverAxonEvents(frame);
    encodeSensors(frame, somaDrive, dendriteDrive);
    updateNeurons(somaDrive, dendriteDrive);
    scheduleSpikeEvents();
    updatePlasticity(frame);
    updateAssemblies(frame);
    updateHomeostasisAndEnergy();
    if (physiologicalSubstrate_) {
        std::vector<double> somaMv;
        std::vector<double> dendriteMv;
        std::vector<double> ratesHz;
        std::vector<double> energy;
        somaMv.reserve(neurons_.size());
        dendriteMv.reserve(neurons_.size());
        ratesHz.reserve(neurons_.size());
        energy.reserve(neurons_.size());
        for (const auto& neuron : neurons_) {
            somaMv.push_back(neuron.somaMv);
            dendriteMv.push_back(neuron.dendriteMv);
            ratesHz.push_back(neuron.filteredRateHz);
            energy.push_back(neuron.energy);
        }
        physiologicalSubstrate_->step(
            frame, spikes_, somaMv, dendriteMv, ratesHz, energy);
    }
    updateStructuralPlasticity();
    ++metrics_.step;
    updateMetrics();
    biologicalSubstrate_->updateTissueMechanics(
        static_cast<std::size_t>(std::max(0, metrics_.activeSynapses)),
        physiologyMetrics().meanProteinPool,
        physiologyMetrics().extracellularVolumeFraction,
        config_.dtMs);
    return decodeAction();
}

std::vector<MotorAction> PersistentNervousSystem::run(
    const std::vector<SensorFrame>& frames) {
    std::vector<MotorAction> actions;
    actions.reserve(frames.size());
    for (const auto& frame : frames) {
        actions.push_back(step(frame));
    }
    return actions;
}

bool PersistentNervousSystem::dalePrincipleHolds() const {
    for (const auto& synapse : synapses_) {
        if (!synapse.active) continue;
        const bool inhibitory =
            neurons_[synapse.pre].role == PopulationRole::Inhibitory;
        if (inhibitory && synapse.weight > 0.0) return false;
        if (!inhibitory && synapse.weight < 0.0) return false;
    }
    return true;
}

RepresentationState PersistentNervousSystem::inspect() const {
    throughput_.recordRead<Neuron>(tatarus::ThroughputDomain::Neuron, neurons_.size());
    throughput_.recordRead<Synapse>(tatarus::ThroughputDomain::Synapse, synapses_.size());
    throughput_.record(tatarus::ThroughputDomain::Cognition, assemblies_.size(), sizeof(Assembly), true, false);
    RepresentationState state;
    state.step = metrics_.step;
    state.activeAssembly = metrics_.activeAssembly;
    state.neurons.reserve(neurons_.size());
    for (const auto& neuron : neurons_) {
        state.neurons.push_back(NeuronStateView{
            neuron.role,
            neuron.subtype,
            neuron.somaMv,
            neuron.dendriteMv,
            neuron.filteredRateHz,
            neuron.fastRateHz,
            neuron.energy,
            neuron.spikeCount,
            neuron.active});
    }
    state.synapses.reserve(synapses_.size());
    for (std::size_t index = 0; index < synapses_.size(); ++index) {
        const auto& synapse = synapses_[index];
        state.synapses.push_back(SynapseStateView{
            index,
            synapse.parentSynapse,
            synapse.pre,
            synapse.post,
            synapse.receptor,
            synapse.weight,
            synapse.consolidatedWeight,
            synapse.eligibility,
            synapse.resource,
            synapse.usage,
            synapse.active});
    }
    state.assemblies.reserve(assemblies_.size());
    for (const auto& assembly : assemblies_) {
        state.assemblies.push_back(AssemblyStateView{
            assembly.id,
            assembly.prototype,
            assembly.activation,
            assembly.observations,
            assembly.lastActiveStep});
    }
    return state;
}

BiologicalSpatialState PersistentNervousSystem::inspectSpatial() const {
    throughput_.recordRead<Neuron>(tatarus::ThroughputDomain::Neuron, neurons_.size());
    throughput_.recordRead<Synapse>(tatarus::ThroughputDomain::Synapse, synapses_.size());
    BiologicalSpatialState state = biologicalSubstrate_->inspectSpatial();
    state.step = metrics_.step;
    state.dtMs = config_.dtMs;

    const std::size_t neuronCount = std::min(
        state.neurons.size(), neurons_.size());
    for (std::size_t index = 0; index < neuronCount; ++index) {
        const auto& source = neurons_[index];
        auto& target = state.neurons[index];
        target.role = source.role;
        target.subtype = source.subtype;
        target.somaMv = source.somaMv;
        target.dendriteMv = source.dendriteMv;
        target.filteredRateHz = source.filteredRateHz;
        target.energy = source.energy;
        target.spikeCount = source.spikeCount;
        target.spiking = index < spikes_.size() && spikes_[index] != 0;
        target.active = source.active;
    }

    const std::size_t axonCount = std::min(
        state.axons.size(), synapses_.size());
    for (std::size_t index = 0; index < axonCount; ++index) {
        const auto& source = synapses_[index];
        auto& target = state.axons[index];
        target.pre = source.pre;
        target.post = source.post;
        target.receptor = source.receptor;
        target.weight = source.weight;
        target.eligibility = source.eligibility;
        target.resource = source.resource;
        target.usage = source.usage;
        target.active = source.active;
    }

    if (!axonQueue_.empty()) {
        const std::size_t queueSize = axonQueue_.size();
        const std::size_t currentSlot = static_cast<std::size_t>(
            metrics_.step % queueSize);
        for (std::size_t slot = 0; slot < queueSize; ++slot) {
            const std::size_t remaining =
                (slot + queueSize - currentSlot) % queueSize;
            for (const auto& event : axonQueue_[slot]) {
                if (event.synapse >= state.axons.size()) continue;
                const auto& axon = state.axons[event.synapse];
                const double duration = std::max(
                    1.0,
                    std::round(axon.effectiveDelayMs
                        / std::max(0.05, config_.dtMs)));
                state.signals.push_back(SpatialAxonSignalView{
                    event.synapse,
                    std::clamp(
                        1.0 - static_cast<double>(remaining) / duration,
                        0.0,
                        1.0),
                    event.amplitude});
            }
        }
    }
    return state;
}

PhysiologicalState PersistentNervousSystem::inspectPhysiology() const {
    return physiologicalSubstrate_->inspect();
}

std::uint64_t PersistentNervousSystem::stateHash() const {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](const auto& value) {
        hash = mixHash(hash, &value, sizeof(value));
    };
    add(metrics_.step);
    for (const auto& neuron : neurons_) {
        add(neuron.role);
        add(neuron.subtype);
        add(neuron.somaMv);
        add(neuron.dendriteMv);
        add(neuron.gAmpa);
        add(neuron.gNmda);
        add(neuron.gGabaA);
        add(neuron.gGabaB);
        add(neuron.adaptationMv);
        add(neuron.homeostaticBiasMv);
        add(neuron.filteredRateHz);
        add(neuron.fastRateHz);
        add(neuron.slowBaselineHz);
        add(neuron.energy);
        add(neuron.excitability);
        add(neuron.refractorySteps);
        add(neuron.spikeCount);
        add(neuron.active);
    }
    for (const auto& synapse : synapses_) {
        add(synapse.pre);
        add(synapse.parentSynapse);
        add(synapse.post);
        add(synapse.receptor);
        add(synapse.weight);
        add(synapse.consolidatedWeight);
        add(synapse.eligibility);
        add(synapse.resource);
        add(synapse.facilitation);
        add(synapse.usage);
        add(synapse.delaySteps);
        add(synapse.ageSteps);
        add(synapse.active);
    }
    for (const auto& trace : {&preTrace_, &postTrace_}) {
        hash = mixHash(
            hash,
            trace->data(),
            trace->size() * sizeof(double));
    }
    if (!assemblyAccumulator_.empty()) {
        hash = mixHash(
            hash,
            assemblyAccumulator_.data(),
            assemblyAccumulator_.size() * sizeof(double));
    }
    if (!assemblyBaseline_.empty()) {
        hash = mixHash(
            hash,
            assemblyBaseline_.data(),
            assemblyBaseline_.size() * sizeof(double));
    }
    for (const auto& queue : axonQueue_) {
        const auto size = queue.size();
        add(size);
        for (const auto& event : queue) {
            add(event.synapse);
            add(event.amplitude);
        }
    }
    for (const auto& assembly : assemblies_) {
        add(assembly.id);
        if (!assembly.prototype.empty()) {
            hash = mixHash(
                hash,
                assembly.prototype.data(),
                assembly.prototype.size() * sizeof(double));
        }
        add(assembly.activation);
        add(assembly.observations);
        add(assembly.lastActiveStep);
    }
    add(dopamine_);
    add(acetylcholine_);
    add(lastStructuralStep_);
    add(stimulusSignature_);
    add(stimulusAgeSteps_);
    add(stimulusWasPresent_);
    add(recognizedPlaceKey_);
    add(recognizedAssemblyId_);
    add(pendingSpatialPlaceKey_);
    add(pendingSpatialAction_);
    add(pendingSpatialActionValid_);
    add(spatialActionUpdates_);
    add(spatialNovelDiscoveries_);
    add(spatialRevisits_);
    add(spatialSuccessfulConsolidations_);
    add(spatialFailedConsolidations_);
    add(lastSpatialCueSignature_);
    add(spatialCueWasPresent_);
    for (const auto& engram : spatialActionMemory_) {
        add(engram.placeKey);
        add(engram.assemblyId);
        add(engram.visits);
        for (const auto value : engram.actionObservations) add(value);
        for (const auto value : engram.rewardValue) add(value);
        for (const auto value : engram.noveltyValue) add(value);
        for (const auto value : engram.frontierValue) add(value);
        for (const auto value : engram.successValue) add(value);
        for (const auto value : engram.routeValue) add(value);
        for (const auto value : engram.avoidanceValue) add(value);
        for (const auto value : engram.failedEpisodeObservations) add(value);
    }
    for (const auto& trace : spatialEligibilityTrace_) {
        add(trace.placeKey);
        add(trace.action);
        add(trace.eligibility);
    }
    for (const auto& step : spatialEpisodeTrace_) {
        add(step.placeKey);
        add(step.action);
        add(step.reward);
        add(step.novelty);
    }
    if (biologicalSubstrate_) {
        const auto biologicalHash = biologicalSubstrate_->stateHash();
        add(biologicalHash);
    }
    if (prospectiveMemory_) {
        const auto prospectiveHash = prospectiveMemory_->stateHash();
        add(prospectiveHash);
    }
    if (physiologicalSubstrate_) {
        const auto physiologyHash = physiologicalSubstrate_->stateHash();
        add(physiologyHash);
    }
    std::ostringstream randomState;
    randomState << random_;
    const auto randomText = randomState.str();
    hash = mixHash(hash, randomText.data(), randomText.size());
    return hash;
}

void PersistentNervousSystem::saveSnapshot(
    const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Snapshotdatei konnte nicht erstellt werden");
    }
    const std::array<char, 8> magic{'A','G','N','S','V','1','0','\0'};
    output.write(magic.data(), magic.size());
    writePod(output, config_);
    writePodVector(output, neurons_);
    writePodVector(output, synapses_);
    writePodVector(output, spikes_);
    writePodVector(output, preTrace_);
    writePodVector(output, postTrace_);
    writePodVector(output, assemblyAccumulator_);
    writePodVector(output, assemblyBaseline_);
    writePod(output, metrics_);
    writePod(output, dopamine_);
    writePod(output, acetylcholine_);
    writePod(output, lastStructuralStep_);
    writePod(output, stimulusSignature_);
    writePod(output, stimulusAgeSteps_);
    writePod(output, stimulusWasPresent_);
    const std::uint64_t queueSize = axonQueue_.size();
    writePod(output, queueSize);
    for (const auto& queue : axonQueue_) {
        writePodVector(output, queue);
    }
    const std::uint64_t assemblyCount = assemblies_.size();
    writePod(output, assemblyCount);
    for (const auto& assembly : assemblies_) {
        writePod(output, assembly.id);
        writePodVector(output, assembly.prototype);
        writePod(output, assembly.activation);
        writePod(output, assembly.observations);
        writePod(output, assembly.lastActiveStep);
    }
    std::ostringstream randomState;
    randomState << random_;
    const std::string randomText = randomState.str();
    const std::uint64_t randomLength = randomText.size();
    writePod(output, randomLength);
    output.write(
        randomText.data(),
        static_cast<std::streamsize>(randomText.size()));
    if (biologicalSubstrate_) biologicalSubstrate_->save(output);
    if (prospectiveMemory_) prospectiveMemory_->save(output);
    if (physiologicalSubstrate_) physiologicalSubstrate_->save(output);

    // Optional trailing block keeps snapshots from all pre-spatial-memory
    // releases readable.  Older snapshots simply end after the physiology
    // block; new snapshots append this nervous-system-owned engram memory.
    const std::array<char, 8> spatialMagic{'S','P','A','T','V','3','0','\0'};
    output.write(spatialMagic.data(), spatialMagic.size());
    writePod(output, recognizedPlaceKey_);
    writePod(output, recognizedAssemblyId_);
    writePod(output, pendingSpatialPlaceKey_);
    writePod(output, pendingSpatialAction_);
    writePod(output, pendingSpatialActionValid_);
    writePod(output, spatialActionUpdates_);
    writePod(output, spatialNovelDiscoveries_);
    writePod(output, spatialRevisits_);
    writePod(output, spatialSuccessfulConsolidations_);
    writePod(output, spatialFailedConsolidations_);
    writePod(output, lastSpatialCueSignature_);
    writePod(output, spatialCueWasPresent_);
    const std::uint64_t spatialCount = spatialActionMemory_.size();
    writePod(output, spatialCount);
    for (const auto& engram : spatialActionMemory_) {
        writePod(output, engram.placeKey);
        writePod(output, engram.assemblyId);
        writePod(output, engram.visits);
        for (const auto value : engram.actionObservations) writePod(output, value);
        for (const auto value : engram.rewardValue) writePod(output, value);
        for (const auto value : engram.noveltyValue) writePod(output, value);
        for (const auto value : engram.frontierValue) writePod(output, value);
        for (const auto value : engram.successValue) writePod(output, value);
        for (const auto value : engram.routeValue) writePod(output, value);
        for (const auto value : engram.avoidanceValue) writePod(output, value);
        for (const auto value : engram.failedEpisodeObservations) writePod(output, value);
    }
    const std::uint64_t traceCount = spatialEligibilityTrace_.size();
    writePod(output, traceCount);
    for (const auto& trace : spatialEligibilityTrace_) {
        writePod(output, trace.placeKey);
        writePod(output, trace.action);
        writePod(output, trace.eligibility);
    }
    const std::uint64_t episodeTraceCount = spatialEpisodeTrace_.size();
    writePod(output, episodeTraceCount);
    for (const auto& step : spatialEpisodeTrace_) {
        writePod(output, step.placeKey);
        writePod(output, step.action);
        writePod(output, step.reward);
        writePod(output, step.novelty);
    }
    if (!output) {
        throw std::runtime_error("Snapshot konnte nicht abgeschlossen werden");
    }
}

void PersistentNervousSystem::loadSnapshot(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Snapshotdatei konnte nicht geöffnet werden");
    }
    std::array<char, 8> magic{};
    input.read(magic.data(), magic.size());
    const std::array<char, 8> expected{'A','G','N','S','V','1','0','\0'};
    if (magic != expected) {
        throw std::runtime_error("Unbekanntes Snapshotformat");
    }
    NervousSystemConfig savedConfig;
    readPod(input, savedConfig);
    savedConfig.validate();
    config_ = savedConfig;
    readPodVector(input, neurons_);
    readPodVector(input, synapses_);
    readPodVector(input, spikes_);
    readPodVector(input, preTrace_);
    readPodVector(input, postTrace_);
    readPodVector(input, assemblyAccumulator_);
    readPodVector(input, assemblyBaseline_);
    readPod(input, metrics_);
    readPod(input, dopamine_);
    readPod(input, acetylcholine_);
    readPod(input, lastStructuralStep_);
    readPod(input, stimulusSignature_);
    readPod(input, stimulusAgeSteps_);
    readPod(input, stimulusWasPresent_);
    std::uint64_t queueSize = 0;
    readPod(input, queueSize);
    if (queueSize == 0 || queueSize > 10000) {
        throw std::runtime_error("Ungültige Axonwarteschlange im Snapshot");
    }
    axonQueue_.resize(static_cast<std::size_t>(queueSize));
    for (auto& queue : axonQueue_) {
        readPodVector(input, queue);
    }
    std::uint64_t assemblyCount = 0;
    readPod(input, assemblyCount);
    if (assemblyCount > static_cast<std::uint64_t>(config_.maximumAssemblies)) {
        throw std::runtime_error("Zu viele Assemblies im Snapshot");
    }
    assemblies_.resize(static_cast<std::size_t>(assemblyCount));
    for (auto& assembly : assemblies_) {
        readPod(input, assembly.id);
        readPodVector(input, assembly.prototype);
        readPod(input, assembly.activation);
        readPod(input, assembly.observations);
        readPod(input, assembly.lastActiveStep);
    }
    std::uint64_t randomLength = 0;
    readPod(input, randomLength);
    if (randomLength > 1'000'000) {
        throw std::runtime_error("RNG-Zustand im Snapshot ist zu groß");
    }
    std::string randomText(static_cast<std::size_t>(randomLength), '\0');
    input.read(
        randomText.data(),
        static_cast<std::streamsize>(randomText.size()));
    std::istringstream randomState(randomText);
    randomState >> random_;
    if (!input || !randomState) {
        throw std::runtime_error("RNG-Zustand im Snapshot ist ungültig");
    }
    createBiologicalSubstrate();
    if (!biologicalSubstrate_->load(
            input, neurons_.size(), config_.restingMv, config_.dtMs)) {
        createBiologicalSubstrate();
    }
    if (!prospectiveMemory_) {
        prospectiveMemory_ = std::make_unique<temporal::ProspectiveMemory>();
    }
    prospectiveMemory_->setThroughputCounters(&throughput_);
    static_cast<void>(prospectiveMemory_->load(input));
    createPhysiologicalSubstrate();
    static_cast<void>(physiologicalSubstrate_->load(input));

    spatialActionMemory_.clear();
    spatialEligibilityTrace_.clear();
    spatialEpisodeTrace_.clear();
    recognizedPlaceKey_ = 0;
    recognizedAssemblyId_ = 0;
    pendingSpatialPlaceKey_ = 0;
    pendingSpatialAction_ = 0;
    pendingSpatialActionValid_ = false;
    spatialActionUpdates_ = 0;
    spatialNovelDiscoveries_ = 0;
    spatialRevisits_ = 0;
    spatialSuccessfulConsolidations_ = 0;
    spatialFailedConsolidations_ = 0;
    lastSpatialCueSignature_ = 0;
    spatialCueWasPresent_ = false;

    const auto spatialStart = input.tellg();
    std::array<char, 8> spatialMagic{};
    input.read(spatialMagic.data(), spatialMagic.size());
    const std::array<char, 8> expectedSpatialV2{'S','P','A','T','V','2','0','\0'};
    const std::array<char, 8> expectedSpatialV3{'S','P','A','T','V','3','0','\0'};
    const bool spatialV2 = spatialMagic == expectedSpatialV2;
    const bool spatialV3 = spatialMagic == expectedSpatialV3;
    if (input && (spatialV2 || spatialV3)) {
        readPod(input, recognizedPlaceKey_);
        readPod(input, recognizedAssemblyId_);
        readPod(input, pendingSpatialPlaceKey_);
        readPod(input, pendingSpatialAction_);
        readPod(input, pendingSpatialActionValid_);
        readPod(input, spatialActionUpdates_);
        readPod(input, spatialNovelDiscoveries_);
        readPod(input, spatialRevisits_);
        readPod(input, spatialSuccessfulConsolidations_);
        if (spatialV3) readPod(input, spatialFailedConsolidations_);
        readPod(input, lastSpatialCueSignature_);
        readPod(input, spatialCueWasPresent_);
        std::uint64_t spatialCount = 0;
        readPod(input, spatialCount);
        if (spatialCount > 100'000ULL) {
            throw std::runtime_error("Zu viele räumliche Engramme im Snapshot");
        }
        spatialActionMemory_.resize(static_cast<std::size_t>(spatialCount));
        for (auto& engram : spatialActionMemory_) {
            readPod(input, engram.placeKey);
            readPod(input, engram.assemblyId);
            readPod(input, engram.visits);
            for (auto& value : engram.actionObservations) readPod(input, value);
            for (auto& value : engram.rewardValue) readPod(input, value);
            for (auto& value : engram.noveltyValue) readPod(input, value);
            for (auto& value : engram.frontierValue) readPod(input, value);
            for (auto& value : engram.successValue) readPod(input, value);
            if (spatialV3) {
                for (auto& value : engram.routeValue) readPod(input, value);
                for (auto& value : engram.avoidanceValue) readPod(input, value);
                for (auto& value : engram.failedEpisodeObservations) readPod(input, value);
            }
        }
        std::uint64_t traceCount = 0;
        readPod(input, traceCount);
        if (traceCount > kMaximumSpatialTraceEntries * 4ULL) {
            throw std::runtime_error("Räumliche Eligibility-Trace im Snapshot ist zu groß");
        }
        spatialEligibilityTrace_.resize(static_cast<std::size_t>(traceCount));
        for (auto& trace : spatialEligibilityTrace_) {
            readPod(input, trace.placeKey);
            readPod(input, trace.action);
            readPod(input, trace.eligibility);
        }
        if (spatialV3) {
            std::uint64_t episodeTraceCount = 0;
            readPod(input, episodeTraceCount);
            if (episodeTraceCount > kMaximumSpatialEpisodeEntries) {
                throw std::runtime_error("Räumliche Episodenspur im Snapshot ist zu groß");
            }
            spatialEpisodeTrace_.resize(static_cast<std::size_t>(episodeTraceCount));
            for (auto& step : spatialEpisodeTrace_) {
                readPod(input, step.placeKey);
                readPod(input, step.action);
                readPod(input, step.reward);
                readPod(input, step.novelty);
            }
        }
    } else {
        // Backward compatibility: snapshots written before SPATV20 end exactly
        // after the physiology block.  An absent block means an empty spatial
        // engram memory, not a corrupt snapshot.
        input.clear();
        if (spatialStart != std::istream::pos_type(-1)) input.seekg(spatialStart);
    }
    if (
        neurons_.size() != static_cast<std::size_t>(config_.neuronCount())
        || spikes_.size() != neurons_.size()
        || preTrace_.size() != neurons_.size()
        || postTrace_.size() != neurons_.size()
        || assemblyAccumulator_.size() != neurons_.size()
        || assemblyBaseline_.size() != neurons_.size()) {
        throw std::runtime_error("Snapshotdimensionen stimmen nicht");
    }
    rebuildOutgoing();
    synchronizeBiologicalSubstrate();
    updateMetrics();
}

void PersistentNervousSystem::writeStateJson(
    const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Statusbericht konnte nicht geschrieben werden");
    }
    const auto& biology = biologicalMetrics();
    output << std::fixed << std::setprecision(9)
        << "{\n"
        << "  \"schema\": \"agns-state-v2\",\n"
        << "  \"step\": " << metrics_.step << ",\n"
        << "  \"state_hash\": \"" << std::hex << std::uppercase
        << stateHash() << std::dec << "\",\n"
        << "  \"neurons\": " << neurons_.size() << ",\n"
        << "  \"active_synapses\": " << metrics_.activeSynapses << ",\n"
        << "  \"assemblies\": " << metrics_.assemblyCount << ",\n"
        << "  \"total_spikes\": " << metrics_.totalSpikes << ",\n"
        << "  \"total_transmissions\": " << metrics_.totalTransmissions << ",\n"
        << "  \"mean_rate_hz\": " << metrics_.meanRateHz << ",\n"
        << "  \"mean_energy\": " << metrics_.meanEnergy << ",\n"
        << "  \"dopamine\": " << metrics_.dopamine << ",\n"
        << "  \"acetylcholine\": " << metrics_.acetylcholine << ",\n"
        << "  \"dendrite_segments\": " << biology.dendriteSegments << ",\n"
        << "  \"astrocytes\": " << biology.astrocytes << ",\n"
        << "  \"capillaries\": " << biology.capillaries << ",\n"
        << "  \"dendritic_spikes\": " << biology.dendriticSpikes << ",\n"
        << "  \"mean_dendritic_calcium\": " << biology.meanDendriticCalcium << ",\n"
        << "  \"mean_astrocyte_calcium\": " << biology.meanAstrocyteCalcium << ",\n"
        << "  \"mean_oxygen\": " << biology.meanOxygen << ",\n"
        << "  \"mean_glucose\": " << biology.meanGlucose << ",\n"
        << "  \"mean_blood_flow\": " << biology.meanBloodFlow << ",\n"
        << "  \"mean_axon_length_um\": " << biology.meanAxonLengthUm << ",\n"
        << "  \"oligodendrocytes\": " << biology.oligodendrocytes << ",\n"
        << "  \"myelin_remodeling_updates\": " << biology.myelinRemodelingUpdates << ",\n"
        << "  \"mean_myelin_coverage\": " << biology.meanMyelinCoverage << ",\n"
        << "  \"mean_conduction_velocity_um_per_ms\": " << biology.meanConductionVelocityUmPerMs << ",\n"
        << "  \"mean_effective_delay_ms\": " << biology.meanEffectiveDelayMs << ",\n"
        << "  \"mean_oligodendrocyte_reserve\": " << biology.meanOligodendrocyteReserve << ",\n"
        << "  \"microglia\": " << biology.microglia << ",\n"
        << "  \"microglial_surveillance_updates\": " << biology.microglialSurveillanceUpdates << ",\n"
        << "  \"microglial_pruning_events\": " << biology.microglialPruningEvents << ",\n"
        << "  \"microglial_repair_events\": " << biology.microglialRepairEvents << ",\n"
        << "  \"microglial_damage_signals\": " << biology.microglialDamageSignals << ",\n"
        << "  \"mean_microglial_activation\": " << biology.meanMicroglialActivation << ",\n"
        << "  \"mean_complement_tag\": " << biology.meanComplementTag << ",\n"
        << "  \"mean_microglial_repair_capacity\": " << biology.meanMicroglialRepairCapacity << ",\n"
        << "  \"mean_inflammatory_tone\": " << biology.meanInflammatoryTone << ",\n"
        << "  \"baseline_active_synapses\": " << biology.baselineActiveSynapses << ",\n"
        << "  \"mechanics_active_synapses\": " << biology.mechanicsActiveSynapses << ",\n"
        << "  \"growth_limited_events\": " << biology.growthLimitedEvents << ",\n"
        << "  \"baseline_tissue_volume_um3\": " << biology.baselineTissueVolumeUm3 << ",\n"
        << "  \"tissue_volume_um3\": " << biology.tissueVolumeUm3 << ",\n"
        << "  \"tissue_volume_ratio\": " << biology.tissueVolumeRatio << ",\n"
        << "  \"linear_expansion\": " << biology.linearExpansion << ",\n"
        << "  \"effective_tissue_mass_ng\": " << biology.effectiveTissueMassNg << ",\n"
        << "  \"net_biomass_change_ng\": " << biology.netBiomassChangeNg << ",\n"
        << "  \"synaptic_material_volume_um3\": " << biology.synapticMaterialVolumeUm3 << ",\n"
        << "  \"solid_packing_fraction\": " << biology.solidPackingFraction << ",\n"
        << "  \"extracellular_space_fraction\": " << biology.extracellularSpaceFraction << ",\n"
        << "  \"tissue_pressure_kpa\": " << biology.tissuePressureKPa << ",\n"
        << "  \"material_reserve\": " << biology.materialReserve << ",\n"
        << "  \"cumulative_material_synthesized_um3\": " << biology.cumulativeMaterialSynthesizedUm3 << ",\n"
        << "  \"cumulative_material_recycled_um3\": " << biology.cumulativeMaterialRecycledUm3 << ",\n"
        << "  \"learned_transitions\": " << prospectiveMetrics().learnedTransitions << ",\n"
        << "  \"observed_transitions\": " << prospectiveMetrics().observedTransitions << ",\n"
        << "  \"prediction_hits\": " << prospectiveMetrics().predictionHits << ",\n"
        << "  \"prediction_misses\": " << prospectiveMetrics().predictionMisses << ",\n"
        << "  \"predicted_assembly_id\": " << prospectiveMetrics().predictedAssemblyId << ",\n"
        << "  \"prediction_confidence\": " << prospectiveMetrics().predictionConfidence << ",\n"
        << "  \"expected_delay_ms\": " << prospectiveMetrics().expectedDelayMs << ",\n"
        << "  \"prediction_error\": " << prospectiveMetrics().predictionError << ",\n"
        << "  \"temporal_surprise\": " << prospectiveMetrics().temporalSurprise << ",\n"
        << "  \"sequence_familiarity\": " << prospectiveMetrics().sequenceFamiliarity << ",\n"
        << "  \"finite\": " << (metrics_.finite ? "true" : "false") << "\n"
        << "}\n";
}

void PersistentNervousSystem::applyDamage(
    double neuronFraction,
    double synapseFraction,
    std::uint64_t seed) {
    static_cast<void>(applyDamageWithReport(
        neuronFraction,
        synapseFraction,
        seed));
}

DamageReport PersistentNervousSystem::applyDamageWithReport(
    double neuronFraction,
    double synapseFraction,
    std::uint64_t seed) {
    if (
        !std::isfinite(neuronFraction)
        || !std::isfinite(synapseFraction)
        || neuronFraction < 0.0
        || neuronFraction > 0.9
        || synapseFraction < 0.0
        || synapseFraction > 0.9) {
        throw std::invalid_argument(
            "Schadensanteile müssen in [0,0.9] liegen");
    }
    std::mt19937_64 damageRandom(seed);
    std::vector<std::size_t> neuronIndices;
    for (std::size_t index = 0; index < neurons_.size(); ++index) {
        if (
            neurons_[index].role != PopulationRole::Sensory
            && neurons_[index].role != PopulationRole::Motor) {
            neuronIndices.push_back(index);
        }
    }
    std::shuffle(
        neuronIndices.begin(),
        neuronIndices.end(),
        damageRandom);
    const std::size_t neuronDamage = static_cast<std::size_t>(
        std::floor(neuronFraction * neuronIndices.size()));
    DamageReport report;
    report.disabledNeurons.reserve(neuronDamage);
    for (std::size_t index = 0; index < neuronDamage; ++index) {
        const auto disabled = neuronIndices[index];
        biologicalSubstrate_->reportNeuronDamage(disabled, 1.0);
        neurons_[disabled].active = false;
        report.disabledNeurons.push_back(disabled);
    }
    std::vector<std::size_t> synapseIndices;
    for (std::size_t index = 0; index < synapses_.size(); ++index) {
        if (synapses_[index].active) synapseIndices.push_back(index);
    }
    std::shuffle(
        synapseIndices.begin(),
        synapseIndices.end(),
        damageRandom);
    const std::size_t synapseDamage = static_cast<std::size_t>(
        std::floor(synapseFraction * synapseIndices.size()));
    report.disabledSynapses.reserve(synapseDamage);
    for (std::size_t index = 0; index < synapseDamage; ++index) {
        const auto disabled = synapseIndices[index];
        biologicalSubstrate_->reportSynapseDamage(disabled, 1.0);
        synapses_[disabled].active = false;
        report.disabledSynapses.push_back(disabled);
    }
    rebuildOutgoing();
    updateMetrics();
    return report;
}

DamageReport PersistentNervousSystem::disableSynapses(
    const std::vector<std::size_t>& synapseIndices) {
    DamageReport report;
    report.disabledSynapses.reserve(synapseIndices.size());
    for (const auto index : synapseIndices) {
        if (index >= synapses_.size()) {
            throw std::out_of_range(
                "Synapsenindex fuer gezielten Schaden ausserhalb des Netzes");
        }
        if (!synapses_[index].active) continue;
        biologicalSubstrate_->reportSynapseDamage(index, 1.0);
        synapses_[index].active = false;
        report.disabledSynapses.push_back(index);
    }
    rebuildOutgoing();
    updateMetrics();
    return report;
}

void PersistentNervousSystem::setStructuralPlasticityEnabled(bool enabled) {
    config_.structuralPlasticityEnabled = enabled;
}

void PersistentNervousSystem::setLearningEnabled(bool enabled) {
    learningEnabled_ = enabled;
    if (!enabled) {
        pendingSpatialActionValid_ = false;
        spatialEligibilityTrace_.clear();
        spatialEpisodeTrace_.clear();
    }
}

void PersistentNervousSystem::setExperimentalIntervention(
    const ExperimentalIntervention& intervention) {
    biologicalSubstrate_->setExperimentalFactors(
        intervention.astrocyteFunction,
        intervention.oxygenSupply,
        intervention.glucoseSupply,
        intervention.myelinIntegrity,
        intervention.microgliaFunction);
    physiologicalSubstrate_->setExperimentalFactors(
        intervention.pumpEfficiency,
        intervention.astrocyteFunction,
        intervention.neuromodulatorGain,
        0.5 * intervention.oxygenSupply + 0.5 * intervention.glucoseSupply,
        intervention.sleepEnabled);
}

ContinuousEnvironment::ContinuousEnvironment(std::uint64_t seed)
    : seed_(seed) {
    const double offset =
        static_cast<double>(seed_ % 17) / 100.0;
    position_ -= offset;
    target_ -= offset * 0.5;
}

SensorFrame ContinuousEnvironment::sense() const {
    SensorFrame frame;
    const double difference = target_ - position_;
    frame.visionEvents = {
        std::clamp(-difference, 0.0, 1.0),
        std::clamp(difference, 0.0, 1.0),
        std::clamp(std::abs(difference), 0.0, 1.0),
        reachedTarget() ? 1.0 : 0.0};
    frame.audioSamples = {
        std::sin(kPi * position_),
        std::cos(kPi * target_)};
    frame.touch = {
        position_ <= -0.99 ? 1.0 : 0.0,
        position_ >= 0.99 ? 1.0 : 0.0};
    if (textIndex_ < textStream_.size()) {
        frame.textBytes.push_back(textStream_[textIndex_]);
    }
    frame.temperature = temperature_;
    frame.internalEnergy =
        std::clamp(1.0 - 0.2 * std::abs(position_), 0.0, 1.0);
    frame.reward = lastReward_;
    frame.novelty = std::clamp(std::abs(difference), 0.0, 1.0);
    return frame;
}

double ContinuousEnvironment::apply(const MotorAction& action) {
    const double previousDistance = std::abs(target_ - position_);
    position_ = std::clamp(
        position_ + 0.035 * std::clamp(action.movement, -1.0, 1.0),
        -1.0,
        1.0);
    const double currentDistance = std::abs(target_ - position_);
    lastReward_ =
        8.0 * (previousDistance - currentDistance)
        - 0.002 * std::abs(action.movement);
    if (currentDistance < 0.05) {
        lastReward_ += 0.1;
    }
    temperature_ =
        0.98 * temperature_ + 0.02 * std::abs(action.movement);
    cumulativeReward_ += lastReward_;
    if (textIndex_ < textStream_.size()) {
        ++textIndex_;
    }
    return lastReward_;
}

void ContinuousEnvironment::injectTextUtf8(const std::string& text) {
    textStream_.assign(text.begin(), text.end());
    textIndex_ = 0;
}

bool ContinuousEnvironment::reachedTarget() const {
    return std::abs(target_ - position_) < 0.05;
}

double ContinuousEnvironment::position() const {
    return position_;
}

double ContinuousEnvironment::target() const {
    return target_;
}

double ContinuousEnvironment::cumulativeReward() const {
    return cumulativeReward_;
}

ClosedLoopResult runClosedLoop(
    PersistentNervousSystem& nervousSystem,
    ContinuousEnvironment& environment,
    int steps) {
    if (steps <= 0) {
        throw std::invalid_argument("Closed-loop Schritte müssen positiv sein");
    }
    for (int step = 0; step < steps; ++step) {
        const SensorFrame frame = environment.sense();
        const MotorAction action = nervousSystem.step(frame);
        environment.apply(action);
    }
    ClosedLoopResult result;
    result.steps = steps;
    result.cumulativeReward = environment.cumulativeReward();
    result.finalDistance =
        std::abs(environment.target() - environment.position());
    result.meanEnergy = nervousSystem.metrics().meanEnergy;
    result.assemblies = nervousSystem.metrics().assemblyCount;
    result.activeSynapses = nervousSystem.metrics().activeSynapses;
    result.stateHash = nervousSystem.stateHash();
    return result;
}

}  // namespace tatarus::neuro
