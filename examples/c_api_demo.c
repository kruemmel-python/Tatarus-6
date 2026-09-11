#include <tatarus/c_api.h>

#include <stdio.h>

int main(void) {
    tatarus_mind* mind = tatarus_create(7411);
    if (!mind) {
        fprintf(stderr, "create failed: %s\n", tatarus_last_error());
        return 1;
    }

    tatarus_observation observation = {0};
    observation.battery = 0.9;
    observation.light = 0.5;
    observation.acceleration[2] = 9.81;

    tatarus_state state = {0};
    for (int i = 0; i < 30; ++i) {
        observation.timestamp_ns += 1000000ULL;
        observation.light = (i % 2) ? 0.8 : 0.2;
        observation.sound_level = (i % 2) ? 0.6 : 0.15;
        if (!tatarus_observe(mind, &observation, &state)) {
            fprintf(stderr, "observe failed: %s\n", tatarus_last_error());
            tatarus_destroy(mind);
            return 2;
        }
    }

    tatarus_identity_observation person = {0};
    for (int i = 0; i < 8; ++i) person.features[i] = 0.8;
    for (int i = 8; i < 32; ++i) person.features[i] = 0.56;
    for (int i = 32; i < 40; ++i) person.features[i] = 0.2;
    person.camera_id = 0;
    person.quality = 0.95;

    tatarus_identity_state identity = {0};
    for (int i = 0; i < 3; ++i) {
        person.timestamp_seconds = 1.0 + (double)i;
        if (!tatarus_observe_identity(mind, &person, &identity)) {
            fprintf(stderr, "identity failed: %s\n", tatarus_last_error());
            tatarus_destroy(mind);
            return 3;
        }
    }

    tatarus_biology_state biology = {0};
    tatarus_physiology_state physiology = {0};
    tatarus_prospection_state prospection = {0};
    if (!tatarus_get_biology(mind, &biology)
        || !tatarus_get_physiology(mind, &physiology)
        || !tatarus_get_prospection(mind, &prospection)) {
        fprintf(stderr, "telemetry failed: %s\n", tatarus_last_error());
        tatarus_destroy(mind);
        return 4;
    }
    const uint64_t spatial_json_bytes =
        tatarus_get_spatial_json(mind, NULL, 0);
    const uint64_t live_json_bytes =
        tatarus_get_live_json(mind, NULL, 0);
    const uint64_t physiology_json_bytes =
        tatarus_get_physiology_json(mind, NULL, 0);
    if (spatial_json_bytes == 0 || live_json_bytes == 0
        || physiology_json_bytes == 0) {
        fprintf(stderr, "live JSON failed: %s\n", tatarus_last_error());
        tatarus_destroy(mind);
        return 5;
    }

    printf(
        "assembly=%llu predicted=%llu confidence=%.3f neural_transitions=%llu\n",
        (unsigned long long)state.assembly_id,
        (unsigned long long)state.predicted_assembly_id,
        state.prediction_confidence,
        (unsigned long long)state.learned_transitions);
    printf(
        "identity=%llu identity_confidence=%.3f\n",
        (unsigned long long)identity.entity_id,
        identity.confidence);
    printf(
        "biology dendrites=%llu astro=%llu oligo=%llu micro=%llu myelin=%.3f delay_ms=%.3f\n",
        (unsigned long long)biology.dendritic_segments,
        (unsigned long long)biology.astrocytes,
        (unsigned long long)biology.oligodendrocytes,
        (unsigned long long)biology.microglia,
        biology.myelin_coverage,
        biology.effective_delay_ms);
    printf(
        "physiology K=%.4fmM ATP=%.3f pump=%.3f CREB=%.3f tagged=%llu\n",
        physiology.extracellular_k_mm,
        physiology.atp,
        physiology.pump_activity,
        physiology.creb_activation,
        (unsigned long long)physiology.tagged_synapses);
    printf(
        "prospection transitions=%llu conf=%.3f error=%.3f familiarity=%.3f\n",
        (unsigned long long)prospection.learned_transitions,
        prospection.prediction_confidence,
        prospection.prediction_error,
        prospection.sequence_familiarity);
    printf(
        "live_json spatial_bytes=%llu frame_bytes=%llu physiology_bytes=%llu\n",
        (unsigned long long)spatial_json_bytes,
        (unsigned long long)live_json_bytes,
        (unsigned long long)physiology_json_bytes);

    tatarus_destroy(mind);
    return 0;
}
