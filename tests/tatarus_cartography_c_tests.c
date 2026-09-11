#include <tatarus/c_api.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static tatarus_observation observation(void) {
    tatarus_observation value = {0};
    value.timestamp_ns = 1000000;
    value.acceleration[2] = 9.81;
    value.temperature = 18.0;
    value.battery = 0.9;
    value.proximity[0] = 1.0;
    value.proximity[1] = 1.0;
    value.proximity[2] = 1.0;
    return value;
}

static tatarus_pose3d pose(void) {
    tatarus_pose3d value = {0};
    value.orientation_xyzw[3] = 1.0;
    return value;
}

static tatarus_range_reading rock_reading(void) {
    tatarus_range_reading value = {0};
    value.direction[0] = 1.0;
    value.distance_m = 2.0;
    value.max_range_m = 5.0;
    value.confidence = 1.0;
    value.hit = 1;
    value.semantic_label = "rock";
    return value;
}

static void assert_map_json(tatarus_mind* mind, uint64_t environment_id) {
    const uint64_t required = tatarus_get_environment_map_json(
        mind, environment_id, NULL, 0);
    assert(required > 1);
    char* json = (char*)malloc((size_t)required);
    assert(json != NULL);
    assert(tatarus_get_environment_map_json(
        mind, environment_id, json, required) == required);
    assert(strstr(json, "tatarus-environment-map-v1") != NULL);
    assert(strstr(json, "semantic_label\":\"rock") != NULL);
    free(json);
}

int main(void) {
    tatarus_mind* mind = tatarus_create(7411);
    assert(mind != NULL);
    tatarus_observation obs = observation();
    tatarus_pose3d scanner_pose = pose();
    tatarus_range_reading ray = rock_reading();
    tatarus_state state = {0};
    tatarus_cartography_update map = {0};
    assert(tatarus_explore(
        mind, &obs, 17, &scanner_pose, &ray, 1, &state, &map));
    assert(state.experiences == 1);
    assert(map.environment_id == 17);
    assert(map.accepted_rays == 1);
    assert(map.mapped_voxels > 1);
    assert(map.occupied_voxels == 1);
    assert_map_json(mind, 17);
    assert(tatarus_clear_environment_map(mind, 17));
    tatarus_destroy(mind);

    tatarus_organism* organism = tatarus_organism_create(7411);
    assert(organism != NULL);
    tatarus_atmospheric_environment atmosphere = {0};
    atmosphere.total_pressure_kpa = 101.325;
    atmosphere.o2_fraction = 0.2095;
    atmosphere.co2_fraction = 0.0004;
    atmosphere.n2_fraction = 0.7901;
    atmosphere.temperature_c = 18.0;
    atmosphere.relative_humidity = 0.3;
    tatarus_robot_physical_load load = {0};
    load.battery_charge_fraction = 0.9;
    load.chassis_temperature_c = 20.0;
    memset(&state, 0, sizeof(state));
    memset(&map, 0, sizeof(map));
    assert(tatarus_organism_explore_step_with_load(
        organism, &obs, 18, &scanner_pose, &ray, 1,
        &atmosphere, &load, 0.01, &state, &map));
    assert(state.experiences == 1);
    assert(map.environment_id == 18);
    assert(map.occupied_voxels == 1);
    assert(tatarus_organism_get_environment_map_json(
        organism, 18, NULL, 0) > 1);
    assert(tatarus_organism_clear_environment_map(organism, 18));
    tatarus_organism_destroy(organism);
    return 0;
}
