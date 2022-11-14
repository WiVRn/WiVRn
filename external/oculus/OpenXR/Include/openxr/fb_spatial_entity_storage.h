// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   fb_spatial_entity_storage.h
Content     :   spatial entity interface.
Language    :   C99

*************************************************************************************/

#pragma once

#include <openxr/fb_spatial_entity.h>

/*
  159 XR_FB_spatial_entity_storage
*/

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XR_FB_spatial_entity_storage
#define XR_FB_spatial_entity_storage 1

#ifndef XR_FB_spatial_entity
#error "This extension depends XR_FB_spatial_entity which has not been defined"
#endif

#define XR_FBX2_spatial_entity_storage_SPEC_VERSION 2
#define XR_FBX2_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME "XR_FBX2_spatial_entity_storage"

#ifndef XR_FB_spatial_entity_storage_EXPERIMENTAL_VERSION
#define XR_FB_spatial_entity_storage_SPEC_VERSION 1
#define XR_FB_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME "XR_FB_spatial_entity_storage"
#elif XR_FB_spatial_entity_storage_EXPERIMENTAL_VERSION == 2
#define XR_FB_spatial_entity_storage_SPEC_VERSION XR_FBX2_spatial_entity_storage_SPEC_VERSION
#define XR_FB_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME XR_FBX2_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME
#else
#error "unknown experimental version for XR_FB_spatial_entity_storage"
#endif

// In order to persist XrSpaces between application uses the XR_SPACE_COMPONENT_TYPE_STORABLE_FB
// component can be enabled on a space that supports this functionality.  If this
// component has been enabled it allows for application developers to access the ability to
// save and erase persisted XrSpaces.

// Storage location to be used to store, erase, and query spaces from
typedef enum XrSpaceStorageLocationFB {
    XR_SPACE_STORAGE_LOCATION_INVALID_FB = 0,
    XR_SPACE_STORAGE_LOCATION_LOCAL_FB = 1,
    XR_SPACE_STORAGE_LOCATION_MAX_ENUM_FB = 0x7FFFFFFF
} XrSpaceStorageLocationFB;

// Whether data should be persisted indefinitely or otherwise
typedef enum XrSpacePersistenceModeFB {
    XR_SPACE_PERSISTENCE_MODE_INVALID_FB = 0,
    XR_SPACE_PERSISTENCE_MODE_INDEFINITE_FB = 1,
    XR_SPACE_PERSISTENCE_MODE_MAX_ENUM_FB = 0x7FFFFFFF
} XrSpacePersistenceModeFB;

// Space save information used by xrSaveSpaceFB
static const XrStructureType XR_TYPE_SPACE_SAVE_INFO_FB = (XrStructureType)1000158000;
typedef struct XrSpaceSaveInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpace space;
    XrSpaceStorageLocationFB location;
    XrSpacePersistenceModeFB persistenceMode;
} XrSpaceSaveInfoFB;

// Space erase information used by xrEraseSpaceFB
static const XrStructureType XR_TYPE_SPACE_ERASE_INFO_FB = (XrStructureType)1000158001;
typedef struct XrSpaceEraseInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpace space;
    XrSpaceStorageLocationFB location;
} XrSpaceEraseInfoFB;

// Save Result
// The save result event contains the success of the save/write operation to the
// specified location as well as the XrSpace handle on which the save operation was attempted
// on in addition to the unique Uuid and the triggered async request id from the initial
// calling function
static const XrStructureType XR_TYPE_EVENT_DATA_SPACE_SAVE_COMPLETE_FB =
    (XrStructureType)1000158106;
typedef struct XrEventDataSpaceSaveCompleteFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
    XrResult result;
    XrSpace space;
    XrUuidEXT uuid;
    XrSpaceStorageLocationFB location;
} XrEventDataSpaceSaveCompleteFB;

// Erase Result
// The erase result event contains the success of the erase operation from the specified storage
// location.  It will also provide the uuid of the anchor and the async request id from the initial
// calling function
static const XrStructureType XR_TYPE_EVENT_DATA_SPACE_ERASE_COMPLETE_FB =
    (XrStructureType)1000158107;
typedef struct XrEventDataSpaceEraseCompleteFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
    XrResult result;
    XrSpace space;
    XrUuidEXT uuid;
    XrSpaceStorageLocationFB location;
} XrEventDataSpaceEraseCompleteFB;

typedef XrResult(XRAPI_PTR* PFN_xrSaveSpaceFB)(
    XrSession session,
    const XrSpaceSaveInfoFB* info,
    XrAsyncRequestIdFB* requestId);

typedef XrResult(XRAPI_PTR* PFN_xrEraseSpaceFB)(
    XrSession session,
    const XrSpaceEraseInfoFB* info,
    XrAsyncRequestIdFB* requestId);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL
xrSaveSpaceFB(XrSession session, const XrSpaceSaveInfoFB* info, XrAsyncRequestIdFB* requestId);

XRAPI_ATTR XrResult XRAPI_CALL
xrEraseSpaceFB(XrSession session, const XrSpaceEraseInfoFB* info, XrAsyncRequestIdFB* requestId);

#endif // XR_EXTENSION_PROTOTYPES
#endif // !XR_NO_PROTOTYPES
#endif // XR_FB_spatial_entity_storage

// =============================================================================
// Begin Backwards Compatibility (DEPRECATED)
// =============================================================================

// Separate include guard for experimental backwards compatibility,
// to make sure this still gets included even when the extension
// is included in openxr.h
#ifndef XR_FBX_spatial_entity_storage
#define XR_FBX_spatial_entity_storage 1

// Conditionally define experimental versions, since they are not getting defined
// if the extension is included from openxr.h
#ifndef XR_FBX2_spatial_entity_storage_SPEC_VERSION
#define XR_FBX2_spatial_entity_storage_SPEC_VERSION 2
#define XR_FBX2_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME "XR_FBX2_spatial_entity_storage"
#endif

#ifdef XR_FB_spatial_entity_storage_EXPERIMENTAL_VERSION

#if XR_FB_spatial_entity_storage_EXPERIMENTAL_VERSION >= 2


static const XrStructureType XR_TYPE_EVENT_SPATIAL_ENTITY_STORAGE_SAVE_RESULT_FBX2 =
    (XrStructureType)1000077103;
typedef struct XrEventSpatialEntityStorageSaveResultFBX2 {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrResult result;
    XrSpace space;
    XrSpatialEntityUuidFBX2 uuid;
    XrAsyncRequestIdFB request;
} XrEventSpatialEntityStorageSaveResultFBX2;

static const XrStructureType XR_TYPE_EVENT_SPATIAL_ENTITY_STORAGE_ERASE_RESULT_FBX2 =
    (XrStructureType)1000077104;
typedef struct XrEventSpatialEntityStorageEraseResultFBX2 {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrResult result;
    XrSpaceStorageLocationFB location;
    XrSpatialEntityUuidFBX2 uuid;
    XrAsyncRequestIdFB request;
} XrEventSpatialEntityStorageEraseResultFBX2;


typedef XrResult(XRAPI_PTR* PFN_xrSpatialEntitySaveSpaceFBX2)(
    XrSession session,
    const XrSpaceSaveInfoFB* info,
    XrAsyncRequestIdFB* requestId);

typedef XrResult(XRAPI_PTR* PFN_xrSpatialEntityEraseSpaceFBX2)(
    XrSession session,
    const XrSpaceEraseInfoFB* info,
    XrAsyncRequestIdFB* requestId);


#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL xrSpatialEntitySaveSpaceFB(
    XrSession session,
    const XrSpaceSaveInfoFB* info,
    XrAsyncRequestIdFB* requestId);

XRAPI_ATTR XrResult XRAPI_CALL xrSpatialEntityEraseSpaceFB(
    XrSession session,
    const XrSpaceEraseInfoFB* info,
    XrAsyncRequestIdFB* requestId);


#endif // XR_EXTENSION_PROTOTYPES
#endif // !XR_NO_PROTOTYPES

#endif // XR_FB_spatial_entity_storage_EXPERIMENTAL_VERSION >= 2

#if XR_FB_spatial_entity_storage_EXPERIMENTAL_VERSION == 2

#define XrSpatialEntityStorageLocationFB XrSpaceStorageLocationFB
#define XR_SPATIAL_ENTITY_STORAGE_LOCATION_INVALID_FB XR_SPACE_STORAGE_LOCATION_INVALID_FB
#define XR_SPATIAL_ENTITY_STORAGE_LOCATION_LOCAL_FB XR_SPACE_STORAGE_LOCATION_LOCAL_FB
#define XR_SPATIAL_ENTITY_STORAGE_LOCATION_MAX_ENUM_FB XR_SPACE_STORAGE_LOCATION_MAX_ENUM_FB

#define XrSpatialEntityStoragePersistenceModeFB XrSpacePersistenceModeFB
#define XR_SPATIAL_ENTITY_STORAGE_PERSISTENCE_MODE_INVALID_FB XR_SPACE_PERSISTENCE_MODE_INVALID_FB
#define XR_SPATIAL_ENTITY_STORAGE_PERSISTENCE_MODE_INDEFINITE_HIGH_PRI_FB \
    XR_SPACE_PERSISTENCE_MODE_INDEFINITE_FB
#define XR_SPATIAL_ENTITY_STORAGE_PERSISTENCE_MODE_MAX_ENUM_FB XR_SPACE_PERSISTENCE_MODE_MAX_ENUM_FB

#define XR_TYPE_SPATIAL_ENTITY_STORAGE_SAVE_INFO_FB XR_TYPE_SPACE_SAVE_INFO_FB
#define XrSpatialEntityStorageSaveInfoFB XrSpaceSaveInfoFB

#define XR_TYPE_SPATIAL_ENTITY_STORAGE_ERASE_INFO_FB XR_TYPE_SPACE_ERASE_INFO_FB
#define XrSpatialEntityStorageEraseInfoFB XrSpaceEraseInfoFB


#define XR_TYPE_EVENT_SPATIAL_ENTITY_STORAGE_SAVE_RESULT_FB \
    XR_TYPE_EVENT_SPATIAL_ENTITY_STORAGE_SAVE_RESULT_FBX2
#define XrEventSpatialEntityStorageSaveResultFB XrEventSpatialEntityStorageSaveResultFBX2

#define XR_TYPE_EVENT_SPATIAL_ENTITY_STORAGE_ERASE_RESULT_FB \
    XR_TYPE_EVENT_SPATIAL_ENTITY_STORAGE_ERASE_RESULT_FBX2
#define XrEventSpatialEntityStorageEraseResultFB XrEventSpatialEntityStorageEraseResultFBX2


#define PFN_xrSpatialEntitySaveSpaceFB PFN_xrSpatialEntitySaveSpaceFBX2
#define PFN_xrSpatialEntityEraseSpaceFB PFN_xrSpatialEntityEraseSpaceFBX2

#endif // XR_FB_spatial_entity_storage_EXPERIMENTAL_VERSION == 2


#endif // XR_FB_spatial_entity_storage_EXPERIMENTAL_VERSION

#endif // XR_FBX_spatial_entity_storage
// =============================================================================
// End Backwards Compatibility (DEPRECATED)
// =============================================================================

#ifdef __cplusplus
}
#endif
