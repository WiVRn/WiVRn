// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   fb_scene_capture.h
Content     :   Scene Capture functionality.
Language    :   C99

*************************************************************************************/

#pragma once

#include <openxr/fb_spatial_entity.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Extension 199

#ifndef XR_FB_scene_capture
#define XR_FB_scene_capture 1


#ifndef XR_FB_scene_capture_EXPERIMENTAL_VERSION
#define XR_FB_scene_capture_SPEC_VERSION 1
#define XR_FB_SCENE_CAPTURE_EXTENSION_NAME "XR_FB_scene_capture"
#else
#error "unknown experimental version number for XR_FB_scene_capture_EXPERIMENTAL_VERSION"
#endif

// Events.
static const XrStructureType XR_TYPE_EVENT_DATA_SCENE_CAPTURE_COMPLETE_FB =
    (XrStructureType)1000198001;

// Struct of scene capture complete event
// The event struct contains the result of the scene capture operation
typedef struct XrEventDataSceneCaptureCompleteFB {
    XrStructureType type; // XR_TYPE_EVENT_DATA_SCENE_CAPTURE_COMPLETE_FB
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
    XrResult result;
} XrEventDataSceneCaptureCompleteFB;

// Struct for requesting scene capture.
static const XrStructureType XR_TYPE_SCENE_CAPTURE_REQUEST_INFO_FB = (XrStructureType)1000198050;
typedef struct XrSceneCaptureRequestInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;

    // Length of request buffer.
    uint32_t requestByteCount;

    // Request buffer.
    const char* request;
} XrSceneCaptureRequestInfoFB;

// Request scene capture to the system.
typedef XrResult(XRAPI_PTR* PFN_xrRequestSceneCaptureFB)(
    XrSession session,
    const XrSceneCaptureRequestInfoFB* request,
    XrAsyncRequestIdFB* requestId);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

// Request scene capture to the system.
XRAPI_ATTR XrResult XRAPI_CALL xrRequestSceneCaptureFB(
    XrSession session,
    const XrSceneCaptureRequestInfoFB* request,
    XrAsyncRequestIdFB* requestId);

#endif // XR_EXTENSION_PROTOTYPES
#endif // !XR_NO_PROTOTYPES


#endif // XR_FB_scene_capture

#ifdef __cplusplus
}
#endif
