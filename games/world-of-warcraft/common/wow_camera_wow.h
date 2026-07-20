#ifndef WOW_CAMERA_WOW_H
#define WOW_CAMERA_WOW_H

#include "common/wow_world_query.h"

#define BZ_WOW_CAMERA_ANCHOR_HEIGHT   1.6f
#define BZ_WOW_CAMERA_RADIUS          0.25f
#define BZ_WOW_CAMERA_TRACE_START     0.9f
#define BZ_WOW_CAMERA_SKIN            0.05f
#define BZ_WOW_CAMERA_HYSTERESIS      0.15f
#define BZ_WOW_CAMERA_RETURN_RESPONSE 6.0f
#define BZ_WOW_CAMERA_RETURN_SPEED    8.0f

typedef struct {
    FLOAT desired_distance;
    FLOAT allowed_distance;
    FLOAT visual_distance;
    BOOL initialized;
    BOOL blocked;
} WOWCAMERASTATE;
typedef WOWCAMERASTATE *LPWOWCAMERASTATE;
typedef WOWCAMERASTATE const *LPCWOWCAMERASTATE;

typedef struct {
    VECTOR3 anchor;
    VECTOR3 viewangles;
    FLOAT desired_distance;
    FLOAT seconds;
} WOWCAMERAUPDATE;
typedef WOWCAMERAUPDATE const *LPCWOWCAMERAUPDATE;

VECTOR3 Wow_CameraForward(LPCVECTOR3 angles);
FLOAT Wow_CameraAnchorZ(FLOAT previous_z, FLOAT current_z, FLOAT lerp);
void Wow_CameraReset(LPWOWCAMERASTATE state, FLOAT desired_distance);
FLOAT Wow_CameraUpdate(LPWOWCAMERASTATE state, LPCWOWCAMERAUPDATE input);

#endif
