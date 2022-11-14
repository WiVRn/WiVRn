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

#if defined(XR_FB_eye_tracking_EXPERIMENTAL_VERSION) && \
    defined(XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION)
#error \
    "either XR_FB_eye_tracking_EXPERIMENTAL_VERSION or XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION should be defined"
#endif
#if defined(XR_FB_eye_tracking_EXPERIMENTAL_VERSION) && \
    (XR_FB_eye_tracking_EXPERIMENTAL_VERSION != 1)
#error "unknown experimental version number for XR_FB_eye_tracking_EXPERIMENTAL_VERSION"
#endif
#if defined(XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION) && \
    (XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION != 1)
#error "unknown experimental version number for XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION"
#endif

#define XR_FB_eye_tracking_social 1

#define XR_FB_eye_tracking_social_SPEC_VERSION 1
#define XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME "XR_FB_eye_tracking_social"

#define XR_FBX1_eye_tracking_SPEC_VERSION 1
#define XR_FBX1_EYE_TRACKING_EXTENSION_NAME "XR_FBX1_eye_tracking"
#define XR_FBX1_eye_tracking_social_SPEC_VERSION 1
#define XR_FBX1_EYE_TRACKING_SOCIAL_EXTENSION_NAME "XR_FBX1_eye_tracking_social"

#if defined(XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION) && \
    (XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION == 1)

#undef XR_FB_eye_tracking_social_SPEC_VERSION
#undef XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME
#define XR_FB_eye_tracking_social_SPEC_VERSION XR_FBX1_eye_tracking_social_SPEC_VERSION
#define XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME XR_FBX1_EYE_TRACKING_SOCIAL_EXTENSION_NAME

#elif defined(XR_FB_eye_tracking_EXPERIMENTAL_VERSION) && \
    (XR_FB_eye_tracking_EXPERIMENTAL_VERSION == 1)

#undef XR_FB_eye_tracking_social_SPEC_VERSION
#undef XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME
#define XR_FB_eye_tracking_SPEC_VERSION XR_FBX1_eye_tracking_SPEC_VERSION
#define XR_FB_EYE_TRACKING_EXTENSION_NAME XR_FBX1_EYE_TRACKING_EXTENSION_NAME

#endif

XR_DEFINE_HANDLE(XrEyeTrackerFB)
XR_STRUCT_ENUM(XR_TYPE_EYE_TRACKER_CREATE_INFO_FB, 1000202001);
XR_STRUCT_ENUM(XR_TYPE_EYE_GAZES_INFO_FB, 1000202002);
XR_STRUCT_ENUM(XR_TYPE_EYE_GAZES_FB, 1000202003);

XR_STRUCT_ENUM(XR_TYPE_SYSTEM_EYE_TRACKING_PROPERTIES_FB, 1000202004);

typedef XrFlags64 XrEyeInputFlagsFB;

typedef struct XrSystemEyeTrackingPropertiesFB {
    XrStructureType type;
    void* XR_MAY_ALIAS next;
    XrBool32 supportsEyeTracking;
} XrSystemEyeTrackingPropertiesFB;

typedef struct XrEyeTrackerCreateInfoV2FB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
} XrEyeTrackerCreateInfoV2FB;

typedef struct XrEyeTrackerCreateInfoV1FB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrEyeInputFlagsFB flags;
} XrEyeTrackerCreateInfoV1FB;

typedef struct XrEyeGazesInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpace baseSpace;
    XrTime time;
} XrEyeGazesInfoFB;

typedef struct XrEyeGazeV2FB {
    XrBool32 isValid;
    XrPosef gazePose;
    float gazeConfidence;
} XrEyeGazeV2FB;

typedef struct XrEyeGazeV1FB {
    XrBool32 isValid;
    XrPosef gazePose;
    float gazeConfidence;
} XrEyeGazeV1FB;

typedef enum XrEyePositionFB {
    XR_EYE_POSITION_LEFT_FB = 0,
    XR_EYE_POSITION_RIGHT_FB = 1,
    XR_EYE_POSITION_COUNT_FB = 2,
    XR_EYE_POSITION_MAX_ENUM_FB = 0x7FFFFFFF
} XrEyePositionFB;

typedef struct XrEyeGazesV2FB {
    XrStructureType type;
    void* XR_MAY_ALIAS next;
    XrEyeGazeV2FB gaze[2];
    XrTime time;
} XrEyeGazesV2FB;

typedef struct XrEyeGazesV1FB {
    XrStructureType type;
    void* XR_MAY_ALIAS next;
    XrEyeGazeV1FB gaze[2];
} XrEyeGazesV1FB;

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrCreateEyeTrackerV1FB(
    XrSession session,
    const XrEyeTrackerCreateInfoV1FB* createInfo,
    XrEyeTrackerFB* eyeTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrCreateEyeTrackerV2FB(
    XrSession session,
    const XrEyeTrackerCreateInfoV2FB* createInfo,
    XrEyeTrackerFB* eyeTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrDestroyEyeTrackerFB(XrEyeTrackerFB eyeTracker);

XRAPI_ATTR XrResult XRAPI_CALL xrGetEyeGazesV1FB(
    XrEyeTrackerFB eyeTracker,
    const XrEyeGazesInfoFB* gazeInfo,
    XrEyeGazesV1FB* eyeGazes);

XRAPI_ATTR XrResult XRAPI_CALL xrGetEyeGazesV2FB(
    XrEyeTrackerFB eyeTracker,
    const XrEyeGazesInfoFB* gazeInfo,
    XrEyeGazesV2FB* eyeGazes);

#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#if !defined(XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION) && \
        !defined(XR_FB_eye_tracking_EXPERIMENTAL_VERSION) ||    \
    defined(XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION) &&  \
        (XR_FB_eye_tracking_social_EXPERIMENTAL_VERSION == 1)

#define XrEyeGazeFB XrEyeGazeV2FB
#define XrEyeGazesFB XrEyeGazesV2FB
#define XrEyeTrackerCreateInfoFB XrEyeTrackerCreateInfoV2FB

typedef XrResult(XRAPI_PTR* PFN_xrCreateEyeTrackerFB)(
    XrSession session,
    const XrEyeTrackerCreateInfoV2FB* createInfo,
    XrEyeTrackerFB* eyeTracker);
typedef XrResult(XRAPI_PTR* PFN_xrDestroyEyeTrackerFB)(XrEyeTrackerFB eyeTracker);
typedef XrResult(XRAPI_PTR* PFN_xrGetEyeGazesFB)(
    XrEyeTrackerFB eyeTracker,
    const XrEyeGazesInfoFB* gazeInfo,
    XrEyeGazesV2FB* eyeGazes);

#elif defined(XR_FB_eye_tracking_EXPERIMENTAL_VERSION) && \
    (XR_FB_eye_tracking_EXPERIMENTAL_VERSION == 1)

// Flag bits for XrEyeInputFlagsFB
static const XrEyeInputFlagsFB XR_EYE_INPUT_FLAGS_LEFT_EYE_BIT_FB = 0x00000001;
static const XrEyeInputFlagsFB XR_EYE_INPUT_FLAGS_RIGHT_EYE_BIT_FB = 0x00000002;

#define XrEyeGazeFB XrEyeGazeV1FB
#define XrEyeGazesFB XrEyeGazesV1FB
#define XrEyeTrackerCreateInfoFB XrEyeTrackerCreateInfoV1FB

typedef XrResult(XRAPI_PTR* PFN_xrCreateEyeTrackerFB)(
    XrSession session,
    const XrEyeTrackerCreateInfoV1FB* createInfo,
    XrEyeTrackerFB* eyeTracker);
typedef XrResult(XRAPI_PTR* PFN_xrDestroyEyeTrackerFB)(XrEyeTrackerFB eyeTracker);
typedef XrResult(XRAPI_PTR* PFN_xrGetEyeGazesFB)(
    XrEyeTrackerFB eyeTracker,
    const XrEyeGazesInfoFB* gazeInfo,
    XrEyeGazesV1FB* eyeGazes);

#endif // versions

#endif // XR_FB_eye_tracking

#ifdef __cplusplus
}
#endif
