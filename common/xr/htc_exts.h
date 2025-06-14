// clang-format off
#pragma once

#include <openxr/openxr.h>

// XR_HTC_vive_xr_tracker_interaction is a preprocessor guard. Do not pass it to API calls.
#define XR_HTC_vive_xr_tracker_interaction 1
#define XR_HTC_vive_xr_tracker_interaction_SPEC_VERSION 1
#define XR_HTC_VIVE_XR_TRACKER_INTERACTION_EXTENSION_NAME "XR_HTC_vive_xr_tracker_interaction"

// XR_HTC_path_enumeration is a preprocessor guard. Do not pass it to API calls.
#define XR_HTC_path_enumeration 1
#define XR_HTC_path_enumeration_SPEC_VERSION 1
#define XR_HTC_PATH_ENUMERATION_EXTENSION_NAME "XR_HTC_path_enumeration"

typedef struct XrPathsForInteractionProfileEnumerateInfoHTC {
	XrStructureType             type;
	const void* XR_MAY_ALIAS    next;
	XrPath                      interactionProfile;
	XrPath                      userPath;
} XrPathsForInteractionProfileEnumerateInfoHTC;

typedef XrResult (XRAPI_PTR *PFN_xrEnumeratePathsForInteractionProfileHTC)(XrInstance instance, XrPathsForInteractionProfileEnumerateInfoHTC createInfo, uint32_t pathCapacityInput, uint32_t* pathCountOutput, XrPath* paths);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrEnumeratePathsForInteractionProfileHTC(
    XrInstance                                  instance,
    XrPathsForInteractionProfileEnumerateInfoHTC createInfo,
    uint32_t                                    pathCapacityInput,
    uint32_t*                                   pathCountOutput,
    XrPath*                                     paths);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

// clang-format on
