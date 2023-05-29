// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   fb_spatial_entity_user.h
Content     :   spatial entity interface.
Language    :   C99

*************************************************************************************/

#pragma once

#include <openxr/openxr_extension_helpers.h>
#include <openxr/fb_spatial_entity.h>

/*
  242 XR_FB_spatial_entity_user
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_FB_spatial_entity_user
#define XR_FB_spatial_entity_user 1

#define XR_FB_spatial_entity_user_SPEC_VERSION 1
#define XR_FB_SPATIAL_ENTITY_USER_EXTENSION_NAME "XR_FB_spatial_entity_user"

XR_DEFINE_HANDLE(XrSpaceUserFB);

XR_STRUCT_ENUM(XR_TYPE_SPACE_USER_CREATE_INFO_FB, 1000241001);

typedef uint64_t XrSpaceUserIdFB;
typedef struct XrSpaceUserCreateInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpaceUserIdFB userId;
} XrSpaceUserCreateInfoFB;

typedef XrResult(XRAPI_PTR* PFN_xrCreateSpaceUserFB)(
    XrSession session,
    const XrSpaceUserCreateInfoFB* info,
    XrSpaceUserFB* user);

typedef XrResult(
    XRAPI_PTR* PFN_xrGetSpaceUserIdFB)(const XrSpaceUserFB user, XrSpaceUserIdFB* userId);

typedef XrResult(XRAPI_PTR* PFN_xrDestroySpaceUserFB)(const XrSpaceUserFB user);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

// xrCreateSpaceUserFB creates a XrSpaceUserFB handle with the specified information.
XRAPI_ATTR XrResult XRAPI_CALL
xrCreateSpaceUserFB(XrSession session, const XrSpaceUserCreateInfoFB* info, XrSpaceUserFB* user);

// xrGetSpaceUserIdFB retrieves the userId corresponding to the given XrSpaceUserFB handle.
XRAPI_ATTR XrResult XRAPI_CALL
xrGetSpaceUserIdFB(const XrSpaceUserFB user, XrSpaceUserIdFB* userId);

// XrSpaceUserFB handles are destroyed using xrDestroySpaceUserFB.
XRAPI_ATTR XrResult XRAPI_CALL xrDestroySpaceUserFB(const XrSpaceUserFB user);

#endif // XR_EXTENSION_PROTOTYPES
#endif // XR_NO_PROTOTYPES

#endif // XR_FB_spatial_entity_user

#ifdef __cplusplus
}
#endif
