// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   fb_spatial_entity.h
Content     :   spatial entity interface.
Language    :   C99

*************************************************************************************/

#pragma once

/*
  114 XR_FB_spatial_entity
*/

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XR_FB_spatial_entity
#define XR_FB_spatial_entity 1

#ifndef XR_FB_spatial_entity_EXPERIMENTAL_VERSION
#define XR_FB_spatial_entity_SPEC_VERSION 1
#define XR_FB_SPATIAL_ENTITY_EXTENSION_NAME "XR_FB_spatial_entity"
#endif

// Potentially long running requests return an async request identifier
// which will be part of messages returned via events. The nature of
// the response event or events depends on the request, but the async
// request identifier, returned immediately at request time, is used
// to associate responses with requests.
typedef uint64_t XrAsyncRequestIdFB;

static const XrResult XR_ERROR_SPACE_COMPONENT_NOT_SUPPORTED_FB = (XrResult)-1000113000;
static const XrResult XR_ERROR_SPACE_COMPONENT_NOT_ENABLED_FB = (XrResult)-1000113001;
static const XrResult XR_ERROR_SPACE_COMPONENT_STATUS_PENDING_FB = (XrResult)-1000113002;
static const XrResult XR_ERROR_SPACE_COMPONENT_STATUS_ALREADY_SET_FB = (XrResult)-1000113003;

// Every XrSpace object represents a spatial entity that may support an
// arbitrary number of component interfaces. If the component interface
// is advertised as supported, and the support is enabled, then functions
// associated with that interface may be used with that XrSpace.
//
// Whether a component interface is enabled at creation depends on the
// method of creation.
//
// The XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB interface must be enabled to use
// xrLocateSpace() on any space created through the Spatial Entity system
// introduced with this extension.
//
// When functions use a space in a context where one or more necessary
// components are not enabled for the space,
// XR_ERROR_SPACE_COMPONENT_NOT_ENABLED_FB must be returned.
//
typedef enum XrSpaceComponentTypeFB {
    // Works with xrLocateSpace, etc.
    XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB = 0,
    // enables save, load, erase, etc.
    XR_SPACE_COMPONENT_TYPE_STORABLE_FB = 1,
        // Bounded 2D component, used in fb_scene extension.
    // XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB = 3,
    // Bounded 3D component, used in fb_scene extension.
    // XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB = 4,
    // Semantic labels component, used in fb_scene extension.
    // XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB = 5,
    // Room layout component, used in fb_scene extension.
    // XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB = 6,
    // Space container component, used in fb_spatial_entity_container extension.
    // XR_SPACE_COMPONENT_TYPE_SPACE_CONTAINER_FB = 7,
        XR_SPACE_COMPONENT_TYPE_MAX_ENUM_FB = 0x7FFFFFFF
} XrSpaceComponentTypeFB;

// Spatial Entity system properties
static const XrStructureType XR_TYPE_SYSTEM_SPATIAL_ENTITY_PROPERTIES_FB =
    (XrStructureType)1000113004;
typedef struct XrSystemSpatialEntityPropertiesFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrBool32 supportsSpatialEntity;
} XrSystemSpatialEntityPropertiesFB;

// Create info struct used when creating a spatial anchor
static const XrStructureType XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_FB = (XrStructureType)1000113003;
typedef struct XrSpatialAnchorCreateInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpace space;
    XrPosef poseInSpace;
    XrTime time;
} XrSpatialAnchorCreateInfoFB;

// Struct used to enable a component
static const XrStructureType XR_TYPE_SPACE_COMPONENT_STATUS_SET_INFO_FB =
    (XrStructureType)1000113007;
typedef struct XrSpaceComponentStatusSetInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpaceComponentTypeFB componentType;
    XrBool32 enabled;
    XrDuration timeout;
} XrSpaceComponentStatusSetInfoFB;

// Struct to hold component status
//
// Next chain provided here for future component interfaces that
// may have more detailed status to provide.
//
// The changePending flag is set immediately upon requesting a
// component enabled change, but the change in the enabled state
// is not reflected in the status until the async request
// completes successfully.
static const XrStructureType XR_TYPE_SPACE_COMPONENT_STATUS_FB = (XrStructureType)1000113001;
typedef struct XrSpaceComponentStatusFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrBool32 enabled;
    XrBool32 changePending;
} XrSpaceComponentStatusFB;

// Event returned once a Spatial Anchor has successfully been created
static const XrStructureType XR_TYPE_EVENT_DATA_SPATIAL_ANCHOR_CREATE_COMPLETE_FB =
    (XrStructureType)1000113005;
typedef struct XrEventDataSpatialAnchorCreateCompleteFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
    XrResult result;
    XrSpace space;
    XrUuidEXT uuid;
} XrEventDataSpatialAnchorCreateCompleteFB;

// The xrSetSpaceComponentStatusFB request delivers the result of the
// request via event. If the request was not successful, the
// component status will be unaltered except that the changePending status
// is cleared.
static const XrStructureType XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB =
    (XrStructureType)1000113006;
typedef struct XrEventDataSpaceSetStatusCompleteFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
    XrResult result;
    XrSpace space;
    XrUuidEXT uuid;
    XrSpaceComponentTypeFB componentType;
    XrBool32 enabled;
} XrEventDataSpaceSetStatusCompleteFB;

typedef XrResult(XRAPI_PTR* PFN_xrCreateSpatialAnchorFB)(
    XrSession session,
    const XrSpatialAnchorCreateInfoFB* info,
    XrAsyncRequestIdFB* requestId);

typedef XrResult(XRAPI_PTR* PFN_xrGetSpaceUuidFB)(XrSpace space, XrUuidEXT* uuid);

typedef XrResult(XRAPI_PTR* PFN_xrEnumerateSpaceSupportedComponentsFB)(
    XrSpace space,
    uint32_t componentTypeCapacityInput,
    uint32_t* componentTypeCountOutput,
    XrSpaceComponentTypeFB* componentTypes);

typedef XrResult(XRAPI_PTR* PFN_xrSetSpaceComponentStatusFB)(
    XrSpace space,
    const XrSpaceComponentStatusSetInfoFB* info,
    XrAsyncRequestIdFB* requestId);

typedef XrResult(XRAPI_PTR* PFN_xrGetSpaceComponentStatusFB)(
    XrSpace space,
    XrSpaceComponentTypeFB componentType,
    XrSpaceComponentStatusFB* status);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

// xrCreateSpatialAnchorFB is used to create a new spatial anchor space
XRAPI_ATTR XrResult XRAPI_CALL xrCreateSpatialAnchorFB(
    XrSession session,
    const XrSpatialAnchorCreateInfoFB* info,
    XrAsyncRequestIdFB* requestId);

// xrGetSpaceUuidFB is used to access the uuid for an XrSpace if supported
XRAPI_ATTR XrResult XRAPI_CALL xrGetSpaceUuidFB(XrSpace space, XrUuidEXT* uuid);

// All the component interfaces that an entity supports can be discovered
// through the xrEnumerateSpaceSupportedComponentsFB() function. The list of
// supported components will not change over the life of the entity.
XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSpaceSupportedComponentsFB(
    XrSpace space,
    uint32_t componentTypeCapacityInput,
    uint32_t* componentTypeCountOutput,
    XrSpaceComponentTypeFB* componentTypes);

// xrSetSpaceComponentStatusFB() enables or disables any supported component
// interface for a space. Some component interfaces may take time to become
// ready. Rather than block, xrSetSpaceComponentStatusFB() returns an async
// request identifier, and its result is returned via event.
//
// XR_ERROR_SPACE_SET_COMPONENT_STATUS_PENDING is returned if xrSetSpaceComponentStatusFB()
// is called on a space that has an xrSetSpaceComponentStatusFB() call pending already.
XRAPI_ATTR XrResult XRAPI_CALL xrSetSpaceComponentStatusFB(
    XrSpace space,
    const XrSpaceComponentStatusSetInfoFB* info,
    XrAsyncRequestIdFB* requestId);

// xrGetSpaceComponentStatusFB is used to determine whether a component interface is
// currently enabled for a space.
XRAPI_ATTR XrResult XRAPI_CALL xrGetSpaceComponentStatusFB(
    XrSpace space,
    XrSpaceComponentTypeFB componentType,
    XrSpaceComponentStatusFB* status);

#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */
#endif // XR_FB_spatial_entity

// =============================================================================
// Begin Backwards Compatibility (DEPRECATED)
// =============================================================================

// Separate include guard for experimental backwards compatibility,
// to make sure this still gets included even when the extension
// is included in openxr.h
#ifndef XR_FBX_spatial_entity
#define XR_FBX_spatial_entity 1

// Conditionally define experimental versions, since they are not getting defined
// if the extension is included from openxr.h

#ifndef XR_FBX2_spatial_entity_SPEC_VERSION
#define XR_FBX2_spatial_entity_SPEC_VERSION 2
#define XR_FBX2_SPATIAL_ENTITY_EXTENSION_NAME "XR_FBX2_spatial_entity"
#endif


#ifdef XR_FB_spatial_entity_EXPERIMENTAL_VERSION

#if XR_FB_spatial_entity_EXPERIMENTAL_VERSION == 2
#undef XR_FB_spatial_entity_SPEC_VERSION
#define XR_FB_spatial_entity_SPEC_VERSION XR_FBX2_spatial_entity_SPEC_VERSION
#undef XR_FB_SPATIAL_ENTITY_EXTENSION_NAME
#define XR_FB_SPATIAL_ENTITY_EXTENSION_NAME XR_FBX2_SPATIAL_ENTITY_EXTENSION_NAME
#else
#error "unknown experimental version for XR_FB_spatial_entity"
#endif


#if XR_FB_spatial_entity_EXPERIMENTAL_VERSION >= 2

#define XR_UUID_SIZE_FBX2 2
typedef struct XrSpatialEntityUuidFBX2 {
    uint64_t value[XR_UUID_SIZE_FBX2];
} XrSpatialEntityUuidFBX2;

static const XrStructureType XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_FBX2 = (XrStructureType)1000011303;

static const XrStructureType XR_TYPE_COMPONENT_ENABLE_REQUEST_FBX2 = (XrStructureType)1000113000;
typedef struct XrComponentEnableRequestFBX2 {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpaceComponentTypeFB componentType;
    XrBool32 enable;
    XrDuration timeout;
} XrComponentEnableRequestFBX2;

static const XrStructureType XR_TYPE_EVENT_DATA_SET_COMPONENT_ENABLE_RESULT_FBX2 =
    (XrStructureType)1000113002;
typedef struct XrEventDataSetComponentEnableResultFBX2 {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
    XrResult result;
    XrSpaceComponentTypeFB componentType;
    XrSpace space;
} XrEventDataSetComponentEnableResultFBX2;

typedef XrResult(XRAPI_PTR* PFN_xrCreateSpatialAnchorFBX2)(
    XrSession session,
    const XrSpatialAnchorCreateInfoFB* info,
    XrSpace* space);

typedef XrResult(
    XRAPI_PTR* PFN_xrGetSpatialEntityUuidFBX2)(XrSpace space, XrSpatialEntityUuidFBX2* uuid);

typedef XrResult(XRAPI_PTR* PFN_xrEnumerateSupportedComponentsFBX2)(
    XrSpace space,
    uint32_t componentTypeCapacityInput,
    uint32_t* componentTypeCountOutput,
    XrSpaceComponentTypeFB* componentTypes);

typedef XrResult(XRAPI_PTR* PFN_xrSetComponentEnabledFBX2)(
    XrSpace space,
    const XrComponentEnableRequestFBX2* info,
    XrAsyncRequestIdFB* requestId);

typedef XrResult(XRAPI_PTR* PFN_xrGetComponentStatusFBX2)(
    XrSpace space,
    XrSpaceComponentTypeFB componentType,
    XrSpaceComponentStatusFB* status);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL
xrGetSpatialEntityUuidFB(XrSpace space, XrSpatialEntityUuidFBX2* uuid);

XRAPI_ATTR XrResult XRAPI_CALL xrEnumerateSupportedComponentsFB(
    XrSpace space,
    uint32_t componentTypeCapacityInput,
    uint32_t* componentTypeCountOutput,
    XrSpaceComponentTypeFB* componentTypes);

XRAPI_ATTR XrResult XRAPI_CALL xrSetComponentEnabledFB(
    XrSpace space,
    const XrComponentEnableRequestFBX2* info,
    XrAsyncRequestIdFB* requestId);

XRAPI_ATTR XrResult XRAPI_CALL xrGetComponentStatusFB(
    XrSpace space,
    XrSpaceComponentTypeFB componentType,
    XrSpaceComponentStatusFB* status);

#endif // XR_EXTENSION_PROTOTYPES
#endif // !XR_NO_PROTOTYPES

#endif // XR_FB_spatial_entity_EXPERIMENTAL_VERSION >= 2

#if XR_FB_spatial_entity_EXPERIMENTAL_VERSION == 2

#define XR_ERROR_COMPONENT_NOT_SUPPORTED_FB XR_ERROR_SPACE_COMPONENT_NOT_SUPPORTED_FB
#define XR_ERROR_COMPONENT_NOT_ENABLED_FB XR_ERROR_SPACE_COMPONENT_NOT_ENABLED_FB
#define XR_ERROR_SET_COMPONENT_ENABLE_PENDING_FB XR_ERROR_SPACE_COMPONENT_STATUS_PENDING_FB
#define XR_ERROR_SET_COMPONENT_ENABLE_ALREADY_ENABLED_FB \
    XR_ERROR_SPACE_COMPONENT_STATUS_ALREADY_SET_FB

#define XrComponentTypeFB XrSpaceComponentTypeFB
#define XR_COMPONENT_TYPE_LOCATABLE_FB XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB
#define XR_COMPONENT_TYPE_STORABLE_FB XR_SPACE_COMPONENT_TYPE_STORABLE_FB
#define XR_COMPONENT_TYPE_MAX_ENUM_FB XR_SPACE_COMPONENT_TYPE_MAX_ENUM_FB

#define XrComponentStatusFB XrSpaceComponentStatusFB
#define XR_TYPE_COMPONENT_STATUS_FB XR_TYPE_SPACE_COMPONENT_STATUS_FB

#define XR_UUID_SIZE_FB XR_UUID_SIZE_FBX2
#define XrSpatialEntityUuidFB XrSpatialEntityUuidFBX2

#define XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_FB XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_FBX2

#define XR_TYPE_COMPONENT_ENABLE_REQUEST_FB XR_TYPE_COMPONENT_ENABLE_REQUEST_FBX2
#define XrComponentEnableRequestFB XrComponentEnableRequestFBX2

#define XR_TYPE_EVENT_DATA_SET_COMPONENT_ENABLE_RESULT_FB \
    XR_TYPE_EVENT_DATA_SET_COMPONENT_ENABLE_RESULT_FBX2
#define XrEventDataSetComponentEnableResultFB XrEventDataSetComponentEnableResultFBX2

#define PFN_xrCreateSpatialAnchorFB PFN_xrCreateSpatialAnchorFBX2
#define PFN_xrGetSpatialEntityUuidFB PFN_xrGetSpatialEntityUuidFBX2
#define PFN_xrEnumerateSupportedComponentsFB PFN_xrEnumerateSupportedComponentsFBX2
#define PFN_xrSetComponentEnabledFB PFN_xrSetComponentEnabledFBX2
#define PFN_xrGetComponentStatusFB PFN_xrGetComponentStatusFBX2

#endif // XR_FB_spatial_entity_EXPERIMENTAL_VERSION == 2


#endif // XR_FB_spatial_entity_EXPERIMENTAL_VERSION

// =============================================================================
// End Backwards Compatibility (DEPRECATED)
// =============================================================================

#endif // XR_FBX_spatial_entity

#ifdef __cplusplus
}
#endif
