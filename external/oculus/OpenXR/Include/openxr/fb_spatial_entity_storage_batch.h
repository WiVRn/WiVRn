// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   fb_spatial_entity_storage_batch.h
Content     :   Spatial Entity batch storage interface.
Language    :   C99

*************************************************************************************/

#pragma once

#include <openxr/openxr_extension_helpers.h>
#include <openxr/fb_spatial_entity.h>
#include <openxr/fb_spatial_entity_storage.h>

/*
  239 XR_FB_spatial_entity_storage_batch
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_FB_spatial_entity_storage_batch
#define XR_FB_spatial_entity_storage_batch 1

#ifndef XR_FB_spatial_entity
#error "This extension depends on XR_FB_spatial_entity which has not been defined"
#endif // XR_FB_spatial_entity

#ifndef XR_FB_spatial_entity_storage
#error "This extension depends on XR_FB_spatial_entity_storage which has not been defined"
#endif // XR_FB_spatial_entity_storage

#ifndef XR_FB_spatial_entity_storage_batch_EXPERIMENTAL_VERSION
#define XR_FB_spatial_entity_storage_batch_SPEC_VERSION 1
#define XR_FB_SPATIAL_ENTITY_STORAGE_BATCH_EXTENSION_NAME "XR_FB_spatial_entity_storage_batch"
#endif // !XR_FB_spatial_entity_storage_batch_EXPERIMENTAL_VERSION

XR_STRUCT_ENUM(XR_TYPE_SPACE_LIST_SAVE_INFO_FB, 1000238000);
XR_STRUCT_ENUM(XR_TYPE_EVENT_DATA_SPACE_LIST_SAVE_COMPLETE_FB, 1000238001);

typedef struct XrSpaceListSaveInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    uint32_t spaceCount;
    XrSpace* spaces;
    XrSpaceStorageLocationFB location;
} XrSpaceListSaveInfoFB;

typedef struct XrEventDataSpaceListSaveCompleteFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
    XrResult result;
} XrEventDataSpaceListSaveCompleteFB;

typedef XrResult(XRAPI_PTR* PFN_xrSaveSpaceListFB)(
    XrSession session,
    const XrSpaceListSaveInfoFB* info,
    XrAsyncRequestIdFB* requestId);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL xrSaveSpaceListFB(
    XrSession session,
    const XrSpaceListSaveInfoFB* info,
    XrAsyncRequestIdFB* request);

#endif // XR_EXTENSION_PROTOTYPES
#endif // XR_NO_PROTOTYPES
#endif // XR_FB_spatial_entity_storage_batch


#ifdef __cplusplus
}
#endif
