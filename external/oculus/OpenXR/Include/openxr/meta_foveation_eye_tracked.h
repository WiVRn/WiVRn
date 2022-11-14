// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************
Filename    :   meta_foveation_eye_tracked.h
Content     :   Eye tracked foveated rendering extension.
                already merged: https://gitlab.khronos.org/openxr/openxr/-/merge_requests/2239
Language    :   C99
*************************************************************************************/

#pragma once

#include <openxr/openxr_extension_helpers.h>

/*
  Extension 201 XR_META_foveation_eye_tracked
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_META_foveation_eye_tracked


#define XR_META_foveation_eye_tracked 1
#define XR_FOVEATION_CENTER_SIZE_META 2
#define XR_META_foveation_eye_tracked_SPEC_VERSION 1
#define XR_META_FOVEATION_EYE_TRACKED_EXTENSION_NAME "XR_META_foveation_eye_tracked"

typedef XrFlags64 XrFoveationEyeTrackedProfileCreateFlagsMETA;

// Flag bits for XrFoveationEyeTrackedProfileCreateFlagsMETA

typedef XrFlags64 XrFoveationEyeTrackedStateFlagsMETA;

// Flag bits for XrFoveationEyeTrackedStateFlagsMETA
static const XrFoveationEyeTrackedStateFlagsMETA XR_FOVEATION_EYE_TRACKED_STATE_VALID_BIT_META =
    0x00000001;

// XrFoveationEyeTrackedProfileCreateInfoMETA extends XrFoveationLevelProfileCreateInfoFB
XR_STRUCT_ENUM(XR_TYPE_FOVEATION_EYE_TRACKED_PROFILE_CREATE_INFO_META, 1000200000);
typedef struct XrFoveationEyeTrackedProfileCreateInfoMETA {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrFoveationEyeTrackedProfileCreateFlagsMETA flags;
} XrFoveationEyeTrackedProfileCreateInfoMETA;

XR_STRUCT_ENUM(XR_TYPE_FOVEATION_EYE_TRACKED_STATE_META, 1000200001);
typedef struct XrFoveationEyeTrackedStateMETA {
    XrStructureType type;
    void* XR_MAY_ALIAS next;
    XrVector2f foveationCenter[XR_FOVEATION_CENTER_SIZE_META];
    XrFoveationEyeTrackedStateFlagsMETA flags;
} XrFoveationEyeTrackedStateMETA;

// XrSystemFoveationEyeTrackedPropertiesMETA extends XrSystemProperties
XR_STRUCT_ENUM(XR_TYPE_SYSTEM_FOVEATION_EYE_TRACKED_PROPERTIES_META, 1000200002);
typedef struct XrSystemFoveationEyeTrackedPropertiesMETA {
    XrStructureType type;
    void* XR_MAY_ALIAS next;
    XrBool32 supportsFoveationEyeTracked;
} XrSystemFoveationEyeTrackedPropertiesMETA;

typedef XrResult(XRAPI_PTR* PFN_xrGetFoveationEyeTrackedStateMETA)(
    XrSession session,
    XrFoveationEyeTrackedStateMETA* foveationState);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrGetFoveationEyeTrackedStateMETA(
    XrSession session,
    XrFoveationEyeTrackedStateMETA* foveationState);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

#endif // XR_META_foveation_eye_tracked


#ifdef __cplusplus
}
#endif
