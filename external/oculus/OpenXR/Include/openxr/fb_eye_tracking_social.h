// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************
Filename    :   fb_eye_tracking.h
Content     :   Eye tracking APIs.
Language    :   C99
*************************************************************************************/

#pragma once

#include <openxr/openxr.h>
#include <openxr/openxr_extension_helpers.h>

/*
  203 XR_FB_eye_tracking_social
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_FB_eye_tracking_social
#define XR_FB_eye_tracking_social 1

#define XR_FB_eye_tracking_social_SPEC_VERSION 1
#define XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME "XR_FB_eye_tracking_social"

XR_DEFINE_HANDLE(XrEyeTrackerFB)
XR_STRUCT_ENUM(XR_TYPE_EYE_TRACKER_CREATE_INFO_FB, 1000202001);
XR_STRUCT_ENUM(XR_TYPE_EYE_GAZES_INFO_FB, 1000202002);
XR_STRUCT_ENUM(XR_TYPE_EYE_GAZES_FB, 1000202003);

XR_STRUCT_ENUM(XR_TYPE_SYSTEM_EYE_TRACKING_PROPERTIES_FB, 1000202004);

typedef struct XrSystemEyeTrackingPropertiesFB {
    XrStructureType type;
    void* XR_MAY_ALIAS next;
    XrBool32 supportsEyeTracking;
} XrSystemEyeTrackingPropertiesFB;

typedef struct XrEyeTrackerCreateInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
} XrEyeTrackerCreateInfoFB;

typedef struct XrEyeGazesInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpace baseSpace;
    XrTime time;
} XrEyeGazesInfoFB;

typedef struct XrEyeGazeFB {
    XrBool32 isValid;
    XrPosef gazePose;
    float gazeConfidence;
} XrEyeGazeFB;
#define XrEyeGazeV2FB XrEyeGazeFB

typedef enum XrEyePositionFB {
    XR_EYE_POSITION_LEFT_FB = 0,
    XR_EYE_POSITION_RIGHT_FB = 1,
    XR_EYE_POSITION_COUNT_FB = 2,
    XR_EYE_POSITION_MAX_ENUM_FB = 0x7FFFFFFF
} XrEyePositionFB;

typedef struct XrEyeGazesFB {
    XrStructureType type;
    void* XR_MAY_ALIAS next;
    XrEyeGazeFB gaze[2];
    XrTime time;
} XrEyeGazesFB;

typedef XrResult(XRAPI_PTR* PFN_xrCreateEyeTrackerFB)(
    XrSession session,
    const XrEyeTrackerCreateInfoFB* createInfo,
    XrEyeTrackerFB* eyeTracker);
typedef XrResult(XRAPI_PTR* PFN_xrDestroyEyeTrackerFB)(XrEyeTrackerFB eyeTracker);
typedef XrResult(XRAPI_PTR* PFN_xrGetEyeGazesFB)(
    XrEyeTrackerFB eyeTracker,
    const XrEyeGazesInfoFB* gazeInfo,
    XrEyeGazesFB* eyeGazes);

#endif // XR_FB_eye_tracking


#ifdef __cplusplus
}
#endif
