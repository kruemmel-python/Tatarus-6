#pragma once

#include <stdint.h>

#if defined(_WIN32)
#  if defined(TATARUS_SDK_BUILD)
#    define TATARUS_API __declspec(dllexport)
#  else
#    define TATARUS_API __declspec(dllimport)
#  endif
#else
#  define TATARUS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tatarus_mind tatarus_mind;

typedef struct tatarus_observation {
    uint64_t timestamp_ns;
    double acceleration[3];
    double rotation[3];
    double light;
    double sound_level;
    double temperature;
    double battery;
    double novelty;
    double reward;
    double goal_direction[2]; /* robot-local/world x,z target vector in [-1,1] */
    double proximity[3];      /* normalized front,left,right clearance */
} tatarus_observation;

typedef struct tatarus_state {
    uint64_t assembly_id;
    uint64_t predicted_assembly_id;
    double prediction_confidence;
    double prediction_error;
    uint64_t learned_transitions;
    uint64_t experiences;
    uint64_t predictions;
} tatarus_state;

typedef enum tatarus_identity_decision_kind {
    TATARUS_IDENTITY_NEW = 0,
    TATARUS_IDENTITY_MATCHED = 1,
    TATARUS_IDENTITY_PROVISIONAL_ASSOCIATION = 2,
    TATARUS_IDENTITY_PROVISIONAL_IDENTITY = 3,
    TATARUS_IDENTITY_UNCERTAIN = 4
} tatarus_identity_decision_kind;

typedef struct tatarus_identity_observation {
    double features[40];
    int32_t camera_id;
    double timestamp_seconds;
    double quality;
} tatarus_identity_observation;

typedef struct tatarus_identity_state {
    tatarus_identity_decision_kind kind;
    uint64_t entity_id;
    uint64_t active_assembly_id;
    double confidence;
    int32_t hypothesis_evidence;
    int32_t evidence_required;
    int32_t baseline_accepted;
} tatarus_identity_state;

typedef struct tatarus_biology_state {
    int32_t available;
    uint64_t dendritic_segments;
    uint64_t astrocytes;
    uint64_t capillaries;
    uint64_t oligodendrocytes;
    uint64_t microglia;
    uint64_t dendritic_spikes;
    uint64_t myelin_remodeling_updates;
    uint64_t microglial_surveillance_updates;
    uint64_t microglial_pruning_events;
    uint64_t microglial_repair_events;
    uint64_t microglial_damage_signals;
    double oxygen;
    double glucose;
    double flow;
    double mean_axon_length_um;
    double myelin_coverage;
    double conduction_velocity_mps;
    double effective_delay_ms;
    double oligodendrocyte_reserve;
    double microglia_activation;
    double complement_tag;
    double repair_capacity;
    double inflammatory_tone;
} tatarus_biology_state;

typedef struct tatarus_tissue_mechanics_state {
    int32_t available;
    uint64_t baseline_active_synapses;
    uint64_t active_synapses;
    uint64_t growth_limited_events;
    double baseline_tissue_volume_um3;
    double tissue_volume_um3;
    double tissue_volume_ratio;
    double linear_expansion;
    double effective_tissue_mass_ng;
    double net_biomass_change_ng;
    double synaptic_material_volume_um3;
    double solid_packing_fraction;
    double extracellular_space_fraction;
    double tissue_pressure_kpa;
    double material_reserve;
    double cumulative_material_synthesized_um3;
    double cumulative_material_recycled_um3;
} tatarus_tissue_mechanics_state;

typedef struct tatarus_prospection_state {
    int32_t available;
    uint64_t learned_transitions;
    uint64_t observed_transitions;
    uint64_t prediction_hits;
    uint64_t prediction_misses;
    uint64_t predicted_assembly_id;
    uint64_t last_assembly_id;
    double prediction_confidence;
    double expected_delay_ms;
    double prediction_error;
    double temporal_surprise;
    double sequence_familiarity;
    double prospective_activation;
} tatarus_prospection_state;

typedef enum tatarus_sleep_phase {
    TATARUS_SLEEP_WAKE = 0,
    TATARUS_SLEEP_NREM = 1,
    TATARUS_SLEEP_REM = 2
} tatarus_sleep_phase;

typedef struct tatarus_physiology_state {
    int32_t available;
    int32_t finite;
    tatarus_sleep_phase sleep_phase;
    uint64_t parvalbumin_neurons;
    uint64_t somatostatin_neurons;
    uint64_t vip_neurons;
    uint64_t tagged_synapses;
    uint64_t l_ltp_events;
    uint64_t l_ltd_events;
    uint64_t sleep_transitions;
    double extracellular_na_mm;
    double extracellular_k_mm;
    double extracellular_ca_mm;
    double extracellular_cl_mm;
    double atp;
    double pump_activity;
    double cumulative_atp_consumed;
    double heat_production_pj;
    double entropy_production_pj_per_k;
    double dopamine;
    double serotonin;
    double noradrenaline;
    double acetylcholine;
    double camp;
    double ip3;
    double hcn_density;
    double kv_density;
    double creb_activation;
    double protein_pool;
    double synaptic_tag;
    double circadian_phase_hours;
    double sleep_pressure;
    double extracellular_volume_fraction;
    double glymphatic_clearance;
    double tissue_waste;
    double gamma_power;
    double theta_power;
} tatarus_physiology_state;

typedef struct tatarus_motor_state {
    int32_t available;
    uint32_t selected_direction;
    double directional_activity[4]; /* north, east, south, west */
    double movement;
    double attention;
    double vocalization;
    double confidence;
} tatarus_motor_state;

typedef struct tatarus_pose3d {
    double position_m[3];
    double orientation_xyzw[4];
} tatarus_pose3d;

typedef struct tatarus_range_reading {
    double direction[3];
    double distance_m;
    double max_range_m;
    double confidence;
    int32_t hit;
    /* Optional NUL-terminated classifier result; may be NULL. */
    const char* semantic_label;
} tatarus_range_reading;

typedef struct tatarus_cartography_update {
    uint64_t environment_id;
    uint64_t accepted_rays;
    uint64_t rejected_rays;
    uint64_t touched_voxels;
    uint64_t newly_mapped_voxels;
    uint64_t occupied_endpoints;
    uint64_t mapped_voxels;
    uint64_t free_voxels;
    uint64_t occupied_voxels;
    uint64_t uncertain_voxels;
    uint64_t frontier_voxels;
    double local_novelty;
    double frontier_ratio;
    double obstacle_proximity[6]; /* +x,-x,+y,-y,+z,-z in scanner frame */
} tatarus_cartography_update;

typedef struct tatarus_intervention {
    double astrocyte_function;
    double oxygen_supply;
    double glucose_supply;
    double pump_efficiency;
    double myelin_integrity;
    double microglia_function;
    double neuromodulator_gain;
    int32_t sleep_enabled;
} tatarus_intervention;

TATARUS_API tatarus_mind* tatarus_create(uint64_t seed);
TATARUS_API tatarus_mind* tatarus_create_sized(
    uint64_t seed,
    uint32_t neuron_count);
TATARUS_API void tatarus_destroy(tatarus_mind* mind);

TATARUS_API int tatarus_observe(
    tatarus_mind* mind,
    const tatarus_observation* observation,
    tatarus_state* state);

/* Atomically maps the scanner frame and presents the exploration event to the
   same persistent nervous system. */
TATARUS_API int tatarus_explore(
    tatarus_mind* mind,
    const tatarus_observation* observation,
    uint64_t environment_id,
    const tatarus_pose3d* pose,
    const tatarus_range_reading* readings,
    uint64_t reading_count,
    tatarus_state* state,
    tatarus_cartography_update* map_update);

TATARUS_API int tatarus_observe_identity(
    tatarus_mind* mind,
    const tatarus_identity_observation* observation,
    tatarus_identity_state* state);

TATARUS_API int tatarus_confirm_last_identity(tatarus_mind* mind, int32_t correct);
TATARUS_API int tatarus_get_biology(tatarus_mind* mind, tatarus_biology_state* state);
TATARUS_API int tatarus_get_tissue_mechanics(
    tatarus_mind* mind,
    tatarus_tissue_mechanics_state* state);
TATARUS_API int tatarus_get_prospection(tatarus_mind* mind, tatarus_prospection_state* state);
TATARUS_API int tatarus_get_physiology(tatarus_mind* mind, tatarus_physiology_state* state);
TATARUS_API int tatarus_get_motor(tatarus_mind* mind, tatarus_motor_state* state);
TATARUS_API int tatarus_begin_action(
    tatarus_mind* mind,
    uint64_t action_id,
    double intensity);
TATARUS_API int tatarus_end_action(
    tatarus_mind* mind,
    uint64_t action_id,
    double reward,
    double success,
    double novelty);
/* Consolidates the complete neural place/action trace at an episode boundary. */
TATARUS_API int tatarus_end_episode(tatarus_mind* mind, int32_t reached_goal);
TATARUS_API int tatarus_set_learning_enabled(tatarus_mind* mind, int32_t enabled);
TATARUS_API int tatarus_set_navigation_context(tatarus_mind* mind, uint64_t context_id);
TATARUS_API int tatarus_set_intervention(
    tatarus_mind* mind,
    const tatarus_intervention* intervention);
TATARUS_API int tatarus_apply_damage(
    tatarus_mind* mind,
    double neuron_fraction,
    double synapse_fraction,
    uint64_t seed);

// JSON snapshot helpers use a two-call buffer protocol. Pass a null buffer or
// zero capacity to obtain the required byte count including the trailing NUL.
// The return value is zero on error, otherwise the required byte count.
TATARUS_API uint64_t tatarus_get_spatial_json(
    tatarus_mind* mind,
    char* buffer,
    uint64_t capacity);
TATARUS_API uint64_t tatarus_get_live_json(
    tatarus_mind* mind,
    char* buffer,
    uint64_t capacity);
TATARUS_API uint64_t tatarus_get_physiology_json(
    tatarus_mind* mind,
    char* buffer,
    uint64_t capacity);
TATARUS_API uint64_t tatarus_get_environment_map_json(
    tatarus_mind* mind,
    uint64_t environment_id,
    char* buffer,
    uint64_t capacity);
TATARUS_API int tatarus_clear_environment_map(
    tatarus_mind* mind,
    uint64_t environment_id);

TATARUS_API int tatarus_save(tatarus_mind* mind, const char* directory);
TATARUS_API int tatarus_load(tatarus_mind* mind, const char* directory);
TATARUS_API const char* tatarus_last_error(void);

/* ========================================================================= */
/* TATARUS 4 SYNTHETIC ORGANISM C ABI                                       */
/* ========================================================================= */

typedef struct tatarus_organism tatarus_organism;

typedef struct tatarus_atmospheric_environment {
    double total_pressure_kpa;
    double o2_fraction;
    double co2_fraction;
    double n2_fraction;
    double temperature_c;
    double relative_humidity;
    double dust_ppm;
} tatarus_atmospheric_environment;

typedef struct tatarus_organism_telemetry_c {
    int32_t available;
    uint64_t step_count;
    double simulated_time_seconds;

    /* Circulation */
    double total_blood_volume_l;
    double mean_arterial_pressure_mm_hg;
    double systolic_pressure_mm_hg;
    double diastolic_pressure_mm_hg;
    double central_venous_pressure_mm_hg;
    double arterial_po2_mm_hg;
    double arterial_pco2_mm_hg;
    double arterial_oxygen_saturation;
    double arterial_ph;
    double plasma_glucose_mm;
    double plasma_na_mm;
    double plasma_k_mm;
    double plasma_ca_mm;
    double blood_temperature_c;

    /* Heart */
    double heart_rate_bpm;
    double stroke_volume_ml;
    double cardiac_output_l_per_min;
    double ejection_fraction_lv;
    double left_ventricle_pressure_mm_hg;
    double myocardial_o2_consumption_ml_per_min;
    double ischemic_stress_index;

    /* Lung */
    double respiration_rate_bpm;
    double tidal_volume_l;
    double minute_ventilation_l_per_min;
    double alveolar_po2_mm_hg;
    double alveolar_pco2_mm_hg;
    double o2_uptake_rate_ml_per_min;

    /* Kidney */
    double glomerular_filtration_rate_ml_per_min;
    double urine_output_rate_ml_per_min;
    double urine_osmolarity_m_osm_per_kg;
    double renin_secretion_rate;
    double left_kidney_gfr_ml_per_min;
    double right_kidney_gfr_ml_per_min;
    double left_kidney_urine_output_ml_per_min;
    double right_kidney_urine_output_ml_per_min;
    double left_kidney_renal_blood_flow_ml_per_min;
    double right_kidney_renal_blood_flow_ml_per_min;

    /* Interoception */
    double visceral_distress;
    double cardiovascular_load;
    double metabolic_depletion;
    double respiratory_hypoxia;
    double fluid_electrolyte_imbalance;
    double sympathetic_tone;
    double parasympathetic_tone;
} tatarus_organism_telemetry_c;

typedef struct tatarus_robot_physical_load {
    double motor_current_a;
    double motor_voltage_v;
    double joint_torque_nm;
    double angular_velocity_rad_per_s;
    double mechanical_power_w;
    double cpu_gpu_power_w;
    double battery_charge_fraction;
    double chassis_temperature_c;
} tatarus_robot_physical_load;

typedef struct tatarus_interoception_state_c {
    double arterial_pressure;
    double venous_pressure;
    double cardiac_load;
    double heart_rate;
    double arterial_o2;
    double arterial_co2;
    double blood_ph;
    double glucose;
    double sodium;
    double potassium;
    double calcium;
    double osmolarity;
    double body_temperature;
    double renal_stress;
    double hypoxia;
    double hypercapnia;
    double visceral_distress;
    double metabolic_stress;
} tatarus_interoception_state_c;

typedef struct tatarus_conservation_audit_c {
    int32_t balanced;
    double max_discrepancy;
} tatarus_conservation_audit_c;

TATARUS_API tatarus_organism* tatarus_organism_create(uint64_t seed);
TATARUS_API tatarus_organism* tatarus_organism_create_sized(uint64_t seed, uint32_t neuron_count);
TATARUS_API void tatarus_organism_destroy(tatarus_organism* organism);

TATARUS_API int tatarus_organism_step(
    tatarus_organism* organism,
    const tatarus_observation* observation,
    const tatarus_atmospheric_environment* atmosphere,
    double dt_seconds,
    tatarus_state* state);

TATARUS_API int tatarus_organism_step_with_load(
    tatarus_organism* organism,
    const tatarus_observation* observation,
    const tatarus_atmospheric_environment* atmosphere,
    const tatarus_robot_physical_load* load,
    double dt_seconds,
    tatarus_state* state);

TATARUS_API int tatarus_organism_explore_step_with_load(
    tatarus_organism* organism,
    const tatarus_observation* observation,
    uint64_t environment_id,
    const tatarus_pose3d* pose,
    const tatarus_range_reading* readings,
    uint64_t reading_count,
    const tatarus_atmospheric_environment* atmosphere,
    const tatarus_robot_physical_load* load,
    double dt_seconds,
    tatarus_state* state,
    tatarus_cartography_update* map_update);

TATARUS_API int tatarus_organism_get_telemetry(
    tatarus_organism* organism,
    tatarus_organism_telemetry_c* telemetry);

TATARUS_API int tatarus_organism_get_interoception(
    tatarus_organism* organism,
    tatarus_interoception_state_c* state);

TATARUS_API uint64_t tatarus_organism_get_json(
    tatarus_organism* organism,
    char* buffer,
    uint64_t capacity);

TATARUS_API int tatarus_organism_audit_conservation(
    tatarus_organism* organism,
    tatarus_conservation_audit_c* audit,
    double tolerance);

TATARUS_API uint64_t tatarus_organism_snapshot_save(
    tatarus_organism* organism,
    char* buffer,
    uint64_t capacity);

TATARUS_API int tatarus_organism_snapshot_load(
    tatarus_organism* organism,
    const char* buffer);

TATARUS_API int tatarus_organism_infuse(
    tatarus_organism* organism,
    double volume_ml,
    double na_mm,
    double k_mm,
    double glucose_mm);

TATARUS_API int tatarus_organism_bleed(
    tatarus_organism* organism,
    double volume_ml);

/* 1.0/1.0 = both kidneys healthy; 0.0/1.0 = left nephrectomy. */
TATARUS_API int tatarus_organism_set_renal_function(
    tatarus_organism* organism,
    double left_fraction,
    double right_fraction);

/* Neural accessors on the exact mind embedded in the coupled organism. */
TATARUS_API int tatarus_organism_get_motor(
    tatarus_organism* organism,
    tatarus_motor_state* state);
TATARUS_API int tatarus_organism_begin_action(
    tatarus_organism* organism,
    uint64_t action_id,
    double intensity);
TATARUS_API int tatarus_organism_end_action(
    tatarus_organism* organism,
    uint64_t action_id,
    double reward,
    double success,
    double novelty);
TATARUS_API int tatarus_organism_end_episode(
    tatarus_organism* organism,
    int32_t reached_goal);
TATARUS_API int tatarus_organism_set_learning_enabled(
    tatarus_organism* organism,
    int32_t enabled);
TATARUS_API int tatarus_organism_set_navigation_context(
    tatarus_organism* organism,
    uint64_t context_id);
TATARUS_API int tatarus_organism_set_intervention(
    tatarus_organism* organism,
    const tatarus_intervention* intervention);
TATARUS_API int tatarus_organism_apply_damage(
    tatarus_organism* organism,
    double neuron_fraction,
    double synapse_fraction,
    uint64_t seed);
TATARUS_API uint64_t tatarus_organism_get_spatial_json(
    tatarus_organism* organism,
    char* buffer,
    uint64_t capacity);
TATARUS_API uint64_t tatarus_organism_get_live_json(
    tatarus_organism* organism,
    char* buffer,
    uint64_t capacity);
TATARUS_API uint64_t tatarus_organism_get_physiology_json(
    tatarus_organism* organism,
    char* buffer,
    uint64_t capacity);
TATARUS_API uint64_t tatarus_organism_get_throughput_json(
    tatarus_organism* organism,
    char* buffer,
    uint64_t capacity);
TATARUS_API uint64_t tatarus_organism_get_environment_map_json(
    tatarus_organism* organism,
    uint64_t environment_id,
    char* buffer,
    uint64_t capacity);
TATARUS_API int tatarus_organism_clear_environment_map(
    tatarus_organism* organism,
    uint64_t environment_id);
TATARUS_API int tatarus_organism_reset_throughput(tatarus_organism* organism);

/* Neural painting environment on the exact RobotMind embedded in organism.
 * The native canvas is 512x512 RGB24. Legacy double APIs still accept their
 * original 32x32 arrays and upscale them deterministically for ABI stability. */
TATARUS_API int tatarus_organism_imaginatio_learn(
    tatarus_organism* organism,
    const double* pixels,
    uint64_t pixel_count,
    const char* label);
TATARUS_API int tatarus_organism_imaginatio_recall(
    tatarus_organism* organism,
    const double* cue_pixels,
    uint64_t pixel_count);
TATARUS_API int tatarus_organism_imaginatio_learn_rgb(
    tatarus_organism* organism,
    const double* rgb_pixels,
    uint64_t channel_count,
    const char* label);
TATARUS_API int tatarus_organism_imaginatio_recall_rgb(
    tatarus_organism* organism,
    const double* cue_rgb_pixels,
    uint64_t channel_count);
/* Packed full-resolution path: exactly width*height*3 RGB8 bytes. The current
 * organism canvas is 512x512; patch_side is constrained to [1,8]. */
TATARUS_API int tatarus_organism_imaginatio_learn_rgb8(
    tatarus_organism* organism,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    uint64_t patch_side,
    const char* label);
/* Learns one visible example and updates the bounded V12 category abstraction.
 * Persistent memory contains shape/retina/V1/ventral statistics and bindings,
 * not a source raster, RGB8 prototype, exemplar reservoir or teacher trace. */
TATARUS_API int tatarus_organism_imaginatio_learn_category_rgb8(
    tatarus_organism* organism,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    uint64_t patch_side,
    const char* label,
    const char* category);
/* Offline corpus ingestion. Uses one bounded retinal/neural exposure and
 * persists only the V12 abstract visual/category state. It constructs and
 * retains no source-pixel teacher trace and no source raster. */
TATARUS_API int tatarus_organism_imaginatio_ingest_category_rgb8(
    tatarus_organism* organism,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    uint64_t patch_side,
    const char* label,
    const char* category);
/* Classifies an unknown RGB8 image through retina, optic nerve, V1/V2 and
 * learned ventral category memories. expected_categories is an optional
 * comma-separated top-down context. Returns required JSON bytes including NUL. */
TATARUS_API uint64_t tatarus_organism_imaginatio_recognize_categories_rgb8(
    tatarus_organism* organism,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    const char* expected_categories,
    uint64_t maximum_matches,
    char* buffer,
    uint64_t capacity);
/* Segments a full RGB8 scene into independently foveated object candidates,
 * retrieves persistent object identities and learned semantic categories, and
 * reports simple spatial relations. When learn_objects != 0, unknown external
 * objects acquire persistent unsupervised object engrams. */
TATARUS_API uint64_t tatarus_organism_imaginatio_perceive_scene_rgb8(
    tatarus_organism* organism,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    const char* expected_categories,
    int learn_objects,
    uint64_t maximum_objects,
    char* buffer,
    uint64_t capacity);
TATARUS_API int tatarus_organism_imaginatio_recall_rgb8(
    tatarus_organism* organism,
    const uint8_t* cue_rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height);
TATARUS_API int tatarus_organism_imaginatio_associate_symbol(
    tatarus_organism* organism,
    const double* cue_pixels,
    uint64_t pixel_count,
    const char* symbol);
TATARUS_API int tatarus_organism_imaginatio_associate_symbol_rgb(
    tatarus_organism* organism,
    const double* cue_rgb_pixels,
    uint64_t channel_count,
    const char* symbol);
TATARUS_API int tatarus_organism_imaginatio_associate_symbol_rgb8(
    tatarus_organism* organism,
    const uint8_t* cue_rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    const char* symbol);
TATARUS_API int tatarus_organism_imaginatio_draw_symbol(
    tatarus_organism* organism,
    const char* symbol);
TATARUS_API int tatarus_organism_imaginatio_draw_free(
    tatarus_organism* organism,
    const char* optional_label);
TATARUS_API int tatarus_organism_imaginatio_draw_category(
    tatarus_organism* organism,
    const char* category,
    uint64_t variation_seed);
/* One to five comma-separated categories; stage 5 synthesizes shared geometry
 * and pigment from abstract category feature populations, without source-raster
 * donors. */
TATARUS_API int tatarus_organism_imaginatio_fuse_categories(
    tatarus_organism* organism,
    const char* categories,
    uint64_t variation_seed);
/* Symbols are comma-separated UTF-8 labels. */
TATARUS_API int tatarus_organism_imaginatio_compose(
    tatarus_organism* organism,
    const char* symbols);

/* Stage 6/7 ABI. Enum values intentionally match SceneRelationKind and
 * SceneActionKind in imaginatio.hpp. A scene object may omit layout fields
 * during imagination by clearing the corresponding present-mask bit. */
enum {
    TATARUS_SCENE_HAS_X = 1u << 0,
    TATARUS_SCENE_HAS_Y = 1u << 1,
    TATARUS_SCENE_HAS_SCALE = 1u << 2,
    TATARUS_SCENE_HAS_ROTATION = 1u << 3,
    TATARUS_SCENE_HAS_DEPTH = 1u << 4,
    TATARUS_SCENE_HAS_COMPLETE_LAYOUT = (1u << 0) | (1u << 1) | (1u << 2)
        | (1u << 3) | (1u << 4)
};

typedef struct tatarus_scene_object {
    const char* instance;
    const char* category;
    const char* pose;
    uint32_t present_mask;
    double x;
    double y;
    double scale;
    double rotation;
    double depth;
} tatarus_scene_object;

typedef struct tatarus_scene_relation {
    const char* subject;
    uint32_t relation_kind;
    const char* object;
    double confidence;
} tatarus_scene_relation;

typedef struct tatarus_scene_description {
    const char* name;
    const tatarus_scene_object* objects;
    uint64_t object_count;
    const tatarus_scene_relation* relations;
    uint64_t relation_count;
    double background_red;
    double background_green;
    double background_blue;
} tatarus_scene_description;

typedef struct tatarus_scene_action {
    uint32_t action_kind;
    const char* label;
    const char* actor;
    const char* target;
    double direction_x;
    double direction_y;
    double magnitude;
    double duration;
} tatarus_scene_action;

/* Learns a concrete RGB8 pose example and binds it to an existing category. */
TATARUS_API int tatarus_organism_imaginatio_learn_pose_rgb8(
    tatarus_organism* organism,
    const uint8_t* rgb_pixels,
    uint64_t channel_count,
    uint64_t width,
    uint64_t height,
    uint64_t patch_side,
    const char* label,
    const char* category,
    const char* pose);
TATARUS_API int tatarus_organism_imaginatio_observe_scene(
    tatarus_organism* organism,
    const tatarus_scene_description* scene);
TATARUS_API int tatarus_organism_imaginatio_draw_scene(
    tatarus_organism* organism,
    const tatarus_scene_description* scene,
    uint64_t variation_seed);
TATARUS_API int tatarus_organism_imaginatio_learn_transition(
    tatarus_organism* organism,
    const tatarus_scene_description* before,
    const tatarus_scene_action* action,
    const tatarus_scene_description* after);
TATARUS_API int tatarus_organism_imaginatio_imagine_future(
    tatarus_organism* organism,
    const tatarus_scene_description* initial,
    const tatarus_scene_action* actions,
    uint64_t action_count,
    uint64_t variation_seed);
TATARUS_API int tatarus_organism_imaginatio_reset_canvas(
    tatarus_organism* organism);
TATARUS_API uint64_t tatarus_organism_imaginatio_get_json(
    tatarus_organism* organism,
    char* buffer,
    uint64_t capacity);
/* Compatibility symbol for target-space motor painting. The last IMAGINATIO
 * trace is executed directly on a fresh RGB8 canvas of the requested size;
 * the 512x512 working canvas is not resized or interpolated. Enlarged output
 * contains deterministic inferred pigment texture, not recovered source truth.
 * The returned byte count is width*height*3. Passing buffer=NULL/capacity=0
 * queries the required size. */
TATARUS_API uint64_t tatarus_organism_imaginatio_render_native_rgb8(
    tatarus_organism* organism,
    uint64_t width,
    uint64_t height,
    uint8_t* buffer,
    uint64_t capacity);
TATARUS_API int tatarus_organism_imaginatio_remember_source_resolution(
    tatarus_organism* organism,
    uint64_t width,
    uint64_t height);
TATARUS_API uint64_t tatarus_organism_imaginatio_get_trace_json(
    tatarus_organism* organism,
    char* buffer,
    uint64_t capacity);

/* Optional Hybrid Cortex bridge. These symbols are exported by tatarus4_c when
 * TATARUS_BUILD_CORTEX=ON. The Cortex remains a bounded cognitive layer: these
 * operations never expose direct motor, reward, physiology, neuron or synapse
 * mutation through the C ABI. */
typedef enum tatarus_cortex_command_kind {
    TATARUS_CORTEX_ANALYZE = 1,
    TATARUS_CORTEX_IMAGINE = 2,
    TATARUS_CORTEX_HYBRID = 3,
    TATARUS_CORTEX_EXECUTIVE_PLAN = 4,
    TATARUS_CORTEX_AUTONOMOUS_CYCLE = 5,
    TATARUS_CORTEX_POLL = 6,
    TATARUS_CORTEX_EXECUTE_IMAGINATION = 7
} tatarus_cortex_command_kind;

TATARUS_API int tatarus_organism_cortex_configure(
    tatarus_organism* organism,
    const char* config_path);
TATARUS_API int tatarus_organism_cortex_disable(tatarus_organism* organism);
TATARUS_API int tatarus_organism_cortex_probe(tatarus_organism* organism);
TATARUS_API uint64_t tatarus_organism_cortex_push_goal(
    tatarus_organism* organism,
    const char* goal,
    double priority);
TATARUS_API int tatarus_organism_cortex_complete_goal(
    tatarus_organism* organism,
    uint64_t goal_id,
    int32_t success);
TATARUS_API int tatarus_organism_cortex_cancel_goal(
    tatarus_organism* organism,
    uint64_t goal_id);
TATARUS_API int tatarus_organism_cortex_remember(
    tatarus_organism* organism,
    const char* key,
    const char* value,
    double salience);
TATARUS_API int tatarus_organism_cortex_command(
    tatarus_organism* organism,
    uint32_t command,
    const char* goal);
TATARUS_API uint64_t tatarus_organism_cortex_get_json(
    tatarus_organism* organism,
    char* buffer,
    uint64_t capacity);

TATARUS_API int tatarus_organism_save(
    tatarus_organism* organism,
    const char* directory);
TATARUS_API int tatarus_organism_load(
    tatarus_organism* organism,
    const char* directory);

#ifdef __cplusplus
}
#endif
