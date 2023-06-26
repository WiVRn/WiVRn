// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   fb_spatial_entity_sharing.h
Content     :   spatial entity interface.
Language    :   C99

*************************************************************************************/

#pragma once

#include <openxr/openxr_extension_helpers.h>
#include <openxr/fb_spatial_entity.h>
#include <openxr/fb_spatial_entity_storage_batch.h>
#include <openxr/fb_spatial_entity_user.h>

/*
  170 XR_FB_spatial_entity_sharing
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_FB_spatial_entity_sharing
#define XR_FB_spatial_entity_sharing 1

#ifndef XR_FB_spatial_entity
#error "This extension depends on XR_FB_spatial_entity which has not been defined"
#endif // XR_FB_spatial_entity

#ifndef XR_FB_spatial_entity_storage
#error "This extension depends on XR_FB_spatial_entity_storage which has not been defined"
#endif // XR_FB_spatial_entity_storage

// XR_FB_spatial_entity_storage_batch is only a dependency in Experimental Version 5.
// In production Share has been properly decoupled from batch_storage, although the dependency on
// storage remains due to this extending XrSpaceStorageLocationFB.
#if defined(XR_FB_spatial_entity_sharing_EXPERIMENTAL_VERSION) && \
    XR_FB_spatial_entity_sharing_EXPERIMENTAL_VERSION == 5
#ifndef XR_FB_spatial_entity_storage_batch
#error "This extension depends on XR_FB_spatial_entity_storage_batch which has not been defined"
#endif // XR_FB_spatial_entity_storage_batch
#endif // XR_FB_spatial_entity_sharing_EXPERIMENTAL_VERSION == 5

#ifndef XR_FB_spatial_entity_sharing_EXPERIMENTAL_VERSION
#ifndef XR_FB_spatial_entity_user
#error "This extension depends on XR_FB_spatial_entity_user which has not been defined"
#endif // XR_FB_spatial_entity
#endif // XR_FB_spatial_entity_sharing_EXPERIMENTAL_VERSION

#ifndef XR_FB_spatial_entity_sharing_EXPERIMENTAL_VERSION
#define XR_FB_spatial_entity_sharing_SPEC_VERSION 1
#define XR_FB_SPATIAL_ENTITY_SHARING_EXTENSION_NAME "XR_FB_spatial_entity_sharing"
#endif // !defined(XR_FB_spatial_entity_sharing_EXPERIMENTAL_VERSION)

static const XrSpaceComponentTypeFB XR_SPACE_COMPONENT_TYPE_SHARABLE_FB = (XrSpaceComponentTypeFB)2;
static const XrSpaceStorageLocationFB XR_SPACE_STORAGE_LOCATION_CLOUD_FB =
    (XrSpaceStorageLocationFB)2;

XR_STRUCT_ENUM(XR_TYPE_SPACE_SHARE_INFO_FB, 1000169001);
XR_STRUCT_ENUM(XR_TYPE_EVENT_DATA_SPACE_SHARE_COMPLETE_FB, 1000169002);

XR_RESULT_ENUM(XR_ERROR_SPACE_MAPPING_INSUFFICIENT_FB, -1000169000);
XR_RESULT_ENUM(XR_ERROR_SPACE_LOCALIZATION_FAILED_FB, -1000169001);
XR_RESULT_ENUM(XR_ERROR_SPACE_NETWORK_TIMEOUT_FB, -1000169002);
XR_RESULT_ENUM(XR_ERROR_SPACE_NETWORK_REQUEST_FAILED_FB, -1000169003);

// This is defined here unless using experimental version 1 of
// XR_FB_spatial_entity_storage_batch.
#if !defined(XR_FB_spatial_entity_storage_batch_EXPERIMENTAL_VERSION) || \
    XR_FB_spatial_entity_storage_batch_EXPERIMENTAL_VERSION > 1
XR_RESULT_ENUM(XR_ERROR_SPACE_CLOUD_STORAGE_DISABLED_FB, -1000169004);
#endif

typedef struct XrSpaceShareInfoFB {
    XrStructureType type; // XR_TYPE_SPACE_SHARE_INFO_FB
    const void* XR_MAY_ALIAS next;
    uint32_t spaceCount;
    XrSpace* spaces;
    uint32_t userCount;
    XrSpaceUserFB* users;
} XrSpaceShareInfoFB;

typedef struct XrEventDataSpaceShareCompleteFB {
    XrStructureType type; // XR_TYPE_EVENT_DATA_SPACE_SHARE_COMPLETE_FB
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
    XrResult result;
} XrEventDataSpaceShareCompleteFB;

typedef XrResult(XRAPI_PTR* PFN_xrShareSpacesFB)(
    XrSession session,
    const XrSpaceShareInfoFB* info,
    XrAsyncRequestIdFB* requestId);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

// xrShareSpacesFB triggers the share flow for an Anchor. This flow can
// potentially take a long time to complete since explicit user action is required to
// authorize the sharing action, and once authorized a request is sent to the cloud.
XRAPI_ATTR XrResult XRAPI_CALL
xrShareSpacesFB(XrSession session, const XrSpaceShareInfoFB* info, XrAsyncRequestIdFB* requestId);

#endif // XR_EXTENSION_PROTOTYPES
#endif // XR_NO_PROTOTYPES

#endif // XR_FB_spatial_entity_sharing


#ifdef __cplusplus
}
#endif
