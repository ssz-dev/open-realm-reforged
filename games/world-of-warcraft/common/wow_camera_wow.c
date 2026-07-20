#include "common/wow_camera_wow.h"
#include <math.h>
#include <string.h>

/* Render and collision paths share one angle convention for the camera ray. */
VECTOR3 Wow_CameraForward(LPCVECTOR3 angles) {
    FLOAT yaw = (FLOAT)DEG2RAD(angles->y);
    FLOAT pitch = (FLOAT)DEG2RAD(angles->x);
    return (VECTOR3){ cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), -sinf(pitch) };
}

FLOAT Wow_CameraAnchorZ(FLOAT a, FLOAT b, FLOAT t) { return LerpNumber(a, b, t) + BZ_WOW_CAMERA_ANCHOR_HEIGHT; }

/* Map transitions restore the selected distance without retaining collision from the prior world. */
void Wow_CameraReset(LPWOWCAMERASTATE state, FLOAT desired_distance) {
    memset(state, 0, sizeof(*state));
    state->desired_distance = desired_distance;
    state->allowed_distance = desired_distance;
    state->visual_distance = desired_distance;
    state->initialized = true;
}

/* Collision contracts immediately for safety; snapshot lerp hides contraction while return stays rate-limited. */
FLOAT Wow_CameraUpdate(LPWOWCAMERASTATE state, LPCWOWCAMERAUPDATE input) {
    VECTOR3 forward = Wow_CameraForward(&input->viewangles);
    VECTOR3 direction = Vector3_scale(&forward, -1.0f);
    FLOAT travel = MAX(0.0f, input->desired_distance - BZ_WOW_CAMERA_TRACE_START);
    VECTOR3 trace_start = Vector3_mad(&input->anchor, BZ_WOW_CAMERA_TRACE_START, &direction);
    WOWSWEEPQUERY query = {
        .start = { trace_start.x, trace_start.y, trace_start.z - BZ_WOW_CAMERA_RADIUS },
        .displacement = Vector3_scale(&direction, travel),
        .radius = BZ_WOW_CAMERA_RADIUS,
        .height = BZ_WOW_CAMERA_RADIUS * 2.0f,
        .mode = WOW_SWEEP_CAMERA,
    };
    WOWSWEEPRESULT trace;
    BOOL hit;
    FLOAT allowed, seconds, delta, step;

    if (!state->initialized) Wow_CameraReset(state, input->desired_distance);
    state->desired_distance = input->desired_distance;
    if (travel > 0.0f) CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_CAMERA_SWEEPS, 1);
    hit = travel > 0.0f && CM_WowSweepWorld(&query, &trace);
    if (hit) CM_WowWorldProfileAdd(WOW_WORLD_PROFILE_CAMERA_CLAMPS, 1);
    allowed = hit
        ? MAX(BZ_WOW_CAMERA_TRACE_START,
              BZ_WOW_CAMERA_TRACE_START + travel * trace.fraction - BZ_WOW_CAMERA_SKIN)
        : input->desired_distance;
    allowed = MIN(input->desired_distance, allowed);
    if (allowed < state->allowed_distance || !hit ||
        allowed > state->allowed_distance + BZ_WOW_CAMERA_HYSTERESIS)
        state->allowed_distance = allowed;
    state->blocked = hit;
    if (state->visual_distance > state->allowed_distance) {
        state->visual_distance = state->allowed_distance;
        return state->visual_distance;
    }
    seconds = MAX(0.0f, input->seconds);
    delta = state->allowed_distance - state->visual_distance;
    if (delta <= 0.001f) {
        state->visual_distance = state->allowed_distance;
        return state->visual_distance;
    }
    step = MIN(delta * (1.0f - expf(-BZ_WOW_CAMERA_RETURN_RESPONSE * seconds)),
               BZ_WOW_CAMERA_RETURN_SPEED * seconds);
    state->visual_distance += MAX(0.0f, step);
    return state->visual_distance;
}
