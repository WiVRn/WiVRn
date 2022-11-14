// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   fb_spatial_entity_query.h
Content     :   spatial entity interface.
Language    :   C99

*************************************************************************************/

#pragma once

#include <openxr/fb_spatial_entity.h>
#include <openxr/fb_spatial_entity_storage.h>

/*
  157 XR_FB_spatial_entity_query
*/

#ifdef __cplusplus
extern "C" {
#endif

#ifndef XR_FB_spatial_entity_query
#define XR_FB_spatial_entity_query 1

#ifndef XR_FB_spatial_entity
#error "This extension depends XR_FB_spatial_entity which has not been defined"
#endif

#ifndef XR_FB_spatial_entity_storage
#error "This extension depends XR_FB_spatial_entity_storage which has not been defined"
#endif

#define XR_FBX2_spatial_entity_query_SPEC_VERSION 2
#define XR_FBX2_SPATIAL_ENTITY_QUERY_EXTENSION_NAME "XR_FBX2_spatial_entity_query"

#ifndef XR_FB_spatial_entity_query_EXPERIMENTAL_VERSION
#define XR_FB_spatial_entity_query_SPEC_VERSION 1
#define XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME "XR_FB_spatial_entity_query"
#elif XR_FB_spatial_entity_query_EXPERIMENTAL_VERSION == 2
#define XR_FB_spatial_entity_query_SPEC_VERSION XR_FBX2_spatial_entity_query_SPEC_VERSION
#define XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME XR_FBX2_SPATIAL_ENTITY_QUERY_EXTENSION_NAME
#else
#error "unknown experimental version for XR_FB_spatial_entity_query"
#endif

// This extension allows an application to query the spaces that have been previously shared
// or persisted onto the device
//
// The following types of Query actions can be performed:
//
// - XR_SPACE_QUERY_ACTION_LOAD_FB
//      Performs a simple query that returns a XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB
//      when results found during the query become available.  There will also be a final
//      XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB event that is returned when all XrSpaces
//      have been returned.  This query type can be used when looking for a list of all
//      spaces that match the filter criteria specified.  Using this query does an implicit load
//      on the spaces found by this query.
//

// Type of query being performed.
typedef enum XrSpaceQueryActionFB {
    // returns XrSpaces
    XR_SPACE_QUERY_ACTION_LOAD_FB = 0,
        XR_SPACE_QUERY_ACTION_MAX_ENUM_FB = 0x7FFFFFFF
} XrSpaceQueryActionFB;

// Query base struct
typedef struct XR_MAY_ALIAS XrSpaceQueryInfoBaseHeaderFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
} XrSpaceQueryInfoBaseHeaderFB;

// Query filter base struct
typedef struct XR_MAY_ALIAS XrSpaceFilterInfoBaseHeaderFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
} XrSpaceFilterInfoBaseHeaderFB;

// Query result to be returned in the results array of XrEventSpatialEntityQueryResultsFBX2 or as
// output from xrRetrieveSpaceQueryResultsFB(). No type or next pointer included to save
// space in the results array.
typedef struct XrSpaceQueryResultFB {
    XrSpace space;
    XrUuidEXT uuid;
} XrSpaceQueryResultFB;

// May be used to query for spaces and perform a specific action on the spaces returned.
// The available actions can be found in XrSpaceQueryActionFB.
// The filter info provided to the filter member of the struct will be used as an inclusive
// filter.  All spaces that match this criteria will be included in the results returned.
// The excludeFilter member of the struct is not supported at this time.
static const XrStructureType XR_TYPE_SPACE_QUERY_INFO_FB = (XrStructureType)1000156001;
typedef struct XrSpaceQueryInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpaceQueryActionFB queryAction;
    uint32_t maxResultCount;
    XrDuration timeout;
    const XrSpaceFilterInfoBaseHeaderFB* filter;
    const XrSpaceFilterInfoBaseHeaderFB* excludeFilter;
} XrSpaceQueryInfoFB;

// Storage location info is used by the query filters and added to the next chain in order to
// specify which location the query filter wishes to perform it query from
static const XrStructureType XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB =
    (XrStructureType)1000156003;
typedef struct XrSpaceStorageLocationFilterInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpaceStorageLocationFB location;
} XrSpaceStorageLocationFilterInfoFB;

// May be used to query the system to find all spaces that match the uuids provided
// in the filter info
static const XrStructureType XR_TYPE_SPACE_UUID_FILTER_INFO_FB = (XrStructureType)1000156054;
typedef struct XrSpaceUuidFilterInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    uint32_t uuidCount;
    const XrUuidEXT* uuids;
} XrSpaceUuidFilterInfoFB;

// May be used to query the system to find all spaces that have a particular component enabled
static const XrStructureType XR_TYPE_SPACE_COMPONENT_FILTER_INFO_FB = (XrStructureType)1000156052;
typedef struct XrSpaceComponentFilterInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpaceComponentTypeFB componentType;
} XrSpaceComponentFilterInfoFB;

static const XrStructureType XR_TYPE_SPACE_QUERY_RESULTS_FB = (XrStructureType)1000156002;
typedef struct XrSpaceQueryResultsFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    uint32_t resultCapacityInput;
    uint32_t resultCountOutput;
    XrSpaceQueryResultFB* results;
} XrSpaceQueryResultsFB;

static const XrStructureType XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB =
    (XrStructureType)1000156103;
typedef struct XrEventDataSpaceQueryResultsAvailableFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
} XrEventDataSpaceQueryResultsAvailableFB;

// When a query has completely finished this event will be returned
static const XrStructureType XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB =
    (XrStructureType)1000156104;
typedef struct XrEventDataSpaceQueryCompleteFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB requestId;
    XrResult result;
} XrEventDataSpaceQueryCompleteFB;

typedef XrResult(XRAPI_PTR* PFN_xrQuerySpacesFB)(
    XrSession session,
    const XrSpaceQueryInfoBaseHeaderFB* info,
    XrAsyncRequestIdFB* requestId);

typedef XrResult(XRAPI_PTR* PFN_xrRetrieveSpaceQueryResultsFB)(
    XrSession session,
    XrAsyncRequestIdFB requestId,
    XrSpaceQueryResultsFB* results);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL xrQuerySpacesFB(
    XrSession session,
    const XrSpaceQueryInfoBaseHeaderFB* info,
    XrAsyncRequestIdFB* requestId);

// Call this function to get available results for a request. Results are available following a
// XrEventDataSpaceQueryResultsAvailableFB from xrPollEvent() and will be purged from the
// runtime once results have been copied into the application's buffer. The application should call
// this function once to populate resultCount before allocating enough memory for a second call to
// actually retrieve any available results.
XRAPI_ATTR XrResult XRAPI_CALL xrRetrieveSpaceQueryResultsFB(
    XrSession session,
    XrAsyncRequestIdFB requestId,
    XrSpaceQueryResultsFB* results);

#endif // XR_EXTENSION_PROTOTYPES
#endif // !XR_NO_PROTOTYPES
#endif // XR_FB_spatial_entity_query

// =============================================================================
// Begin Backwards Compatibility (DEPRECATED)
// =============================================================================
// Separate include guard for experimental backwards compatibility,
// to make sure this still gets included even when the extension
// is included in openxr.h
#ifndef XR_FBX_spatial_entity_query
#define XR_FBX_spatial_entity_query 1

// Conditionally define experimental versions, since they are not getting defined
// if the extension is included from openxr.h
#ifndef XR_FBX2_spatial_entity_query_SPEC_VERSION
#define XR_FBX2_spatial_entity_query_SPEC_VERSION 2
#define XR_FBX2_SPATIAL_ENTITY_QUERY_EXTENSION_NAME "XR_FBX2_spatial_entity_query"
#endif

#ifdef XR_FB_spatial_entity_query_EXPERIMENTAL_VERSION

#if XR_FB_spatial_entity_query_EXPERIMENTAL_VERSION >= 2

static const XrStructureType XR_TYPE_SPATIAL_ENTITY_QUERY_INFO_ACTION_QUERY_FBX2 =
    (XrStructureType)1000156000;
typedef struct XrSpatialEntityQueryInfoActionQueryFBX2 {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    int32_t maxQuerySpaces;
    XrDuration timeout;
    XrSpaceQueryActionFB queryAction;
    const XrSpaceFilterInfoBaseHeaderFB* filter;
    const XrSpaceFilterInfoBaseHeaderFB* excludeFilter;
} XrSpatialEntityQueryInfoActionQueryFBX2;

static const XrStructureType XR_TYPE_SPATIAL_ENTITY_QUERY_FILTER_IDS_FBX2 =
    (XrStructureType)1000156053;
typedef struct XrSpatialEntityQueryFilterIdsFBX2 {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrSpatialEntityUuidFBX2* uuids;
    uint32_t numIds;
} XrSpatialEntityQueryFilterIdsFBX2;

typedef struct XrSpatialEntityQueryResultFBX2 {
    XrSpace space;
    XrSpatialEntityUuidFBX2 uuid;
} XrSpatialEntityQueryResultFBX2;

#define XR_SPATIAL_ENTITY_QUERY_MAX_RESULTS_PER_EVENT_FBX2 128
static const XrStructureType XR_TYPE_EVENT_SPATIAL_ENTITY_QUERY_RESULTS_FBX2 =
    (XrStructureType)1000156102;
typedef struct XrEventSpatialEntityQueryResultsFBX2 {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrAsyncRequestIdFB request;
    uint32_t numResults;
    XrSpatialEntityQueryResultFBX2 results[XR_SPATIAL_ENTITY_QUERY_MAX_RESULTS_PER_EVENT_FBX2];
} XrEventSpatialEntityQueryResultsFBX2;

static const XrStructureType XR_TYPE_EVENT_SPATIAL_ENTITY_QUERY_COMPLETE_FBX2 =
    (XrStructureType)1000156101;
typedef struct XrEventSpatialEntityQueryCompleteFBX2 {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrResult result;
    int32_t numSpacesFound;
    XrAsyncRequestIdFB request;
} XrEventSpatialEntityQueryCompleteFBX2;

typedef XrResult(XRAPI_PTR* PFN_xrQuerySpatialEntityFBX2)(
    XrSession session,
    const XrSpaceQueryInfoBaseHeaderFB* info,
    XrAsyncRequestIdFB* requestId);


#endif // XR_FB_spatial_entity_query_EXPERIMENTAL_VERSION >= 2

#if XR_FB_spatial_entity_query_EXPERIMENTAL_VERSION == 2

#define XrSpatialEntityQueryPredicateFB XrSpaceQueryActionFB
#define XR_SPATIAL_ENTITY_QUERY_PREDICATE_LOAD_FB XR_SPACE_QUERY_ACTION_LOAD_FB
#define XR_SPATIAL_ENTITY_QUERY_PREDICATE_MAX_ENUM_FB XR_SPACE_QUERY_ACTION_MAX_ENUM_FB

#define XR_TYPE_SPATIAL_ENTITY_QUERY_INFO_ACTION_QUERY_FB \
    XR_TYPE_SPATIAL_ENTITY_QUERY_INFO_ACTION_QUERY_FBX2
#define XrSpatialEntityQueryInfoActionQueryFB XrSpatialEntityQueryInfoActionQueryFBX2

#define XrSpatialEntityQueryInfoBaseHeaderFB XrSpaceQueryInfoBaseHeaderFB
#define XrSpatialEntityQueryFilterBaseHeaderFB XrSpaceFilterInfoBaseHeaderFB

#define XR_TYPE_SPATIAL_ENTITY_STORAGE_LOCATION_INFO_FB \
    XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB
#define XrSpatialEntityStorageLocationInfoFB XrSpaceStorageLocationFilterInfoFB

#define XR_TYPE_SPATIAL_ENTITY_QUERY_FILTER_IDS_FB XR_TYPE_SPATIAL_ENTITY_QUERY_FILTER_IDS_FBX2
#define XrSpatialEntityQueryFilterIdsFB XrSpatialEntityQueryFilterIdsFBX2

#define XR_TYPE_SPATIAL_ENTITY_QUERY_FILTER_COMPONENT_TYPE_FB XR_TYPE_SPACE_COMPONENT_FILTER_INFO_FB
#define XrSpatialEntityQueryFilterComponentTypeFB XrSpaceComponentFilterInfoFB

#define XR_FB_SPATIAL_ENTITY_QUERY_MAX_RESULTS_PER_EVENT \
    XR_SPATIAL_ENTITY_QUERY_MAX_RESULTS_PER_EVENT_FBX2
#define XR_FBX2_SPATIAL_ENTITY_QUERY_MAX_RESULTS_PER_EVENT \
    XR_SPATIAL_ENTITY_QUERY_MAX_RESULTS_PER_EVENT_FBX2

#define XR_TYPE_EVENT_SPATIAL_ENTITY_QUERY_RESULTS_FB \
    XR_TYPE_EVENT_SPATIAL_ENTITY_QUERY_RESULTS_FBX2
#define XrEventSpatialEntityQueryResultsFB XrEventSpatialEntityQueryResultsFBX2

#define XR_TYPE_EVENT_SPATIAL_ENTITY_QUERY_COMPLETE_FB \
    XR_TYPE_EVENT_SPATIAL_ENTITY_QUERY_COMPLETE_FBX2
#define XrEventSpatialEntityQueryCompleteFB XrEventSpatialEntityQueryCompleteFBX2

#define PFN_xrQuerySpatialEntityFB PFN_xrQuerySpatialEntityFBX1
#define PFN_xrTerminateSpatialEntityQueryFB PFN_xrTerminateSpatialEntityQueryFBX2

#endif // XR_FB_spatial_entity_query_EXPERIMENTAL_VERSION == 2


#endif // XR_FB_spatial_entity_query_EXPERIMENTAL_VERSION

// =============================================================================
// End Backwards Compatibility (DEPRECATED)
// =============================================================================

#endif // XR_FBX_spatial_entity_query

#ifdef __cplusplus
}
#endif
