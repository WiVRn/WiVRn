// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   fb_scene.h
Content     :   This header defines spatial entity components and functions used to obtain
                information from Scene Model.
Language    :   C99

*************************************************************************************/

#pragma once

#include <openxr/fb_spatial_entity.h>

#if defined(__cplusplus)
extern "C" {
#endif

// Extension 176

#ifndef XR_FB_scene
#define XR_FB_scene 1

#define XR_FB_scene_SPEC_VERSION 1
#define XR_FB_SCENE_EXTENSION_NAME "XR_FB_scene"

static const XrSpaceComponentTypeFB XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB =
    (XrSpaceComponentTypeFB)3;
static const XrSpaceComponentTypeFB XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB =
    (XrSpaceComponentTypeFB)4;
static const XrSpaceComponentTypeFB XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB =
    (XrSpaceComponentTypeFB)5;
static const XrSpaceComponentTypeFB XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB =
    (XrSpaceComponentTypeFB)6;

// Helper structs to define a 3D bounding box, similar to the 2D counterparts.
typedef struct XrExtent3DfFB {
    float width;
    float height;
    float depth;
} XrExtent3DfFB;

typedef struct XrOffset3DfFB {
    float x;
    float y;
    float z;
} XrOffset3DfFB;

typedef struct XrRect3DfFB {
    XrOffset3DfFB offset;
    XrExtent3DfFB extent;
} XrRect3DfFB;

// Semantic labels component for two-call idiom with xrGetSpaceSemanticLabelsFB.
static const XrStructureType XR_TYPE_SEMANTIC_LABELS_FB = (XrStructureType)1000175000;
typedef struct XrSemanticLabelsFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;

    // Input, capacity of the label buffer in byte.
    uint32_t bufferCapacityInput;

    // Output, size of the label buffer in byte.
    uint32_t bufferCountOutput;

    // Multiple labels represented by raw string, separated by comma (,).
    char* buffer;
} XrSemanticLabelsFB;

// Room layout component for two-call idiom with xrGetSpaceRoomLayoutFB.
static const XrStructureType XR_TYPE_ROOM_LAYOUT_FB = (XrStructureType)1000175001;
typedef struct XrRoomLayoutFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;

    // Floor of the room layout.
    XrUuidEXT floorUuid;

    // Ceiling of the room layout.
    XrUuidEXT ceilingUuid;

    // Input, capacity of wall list buffer.
    uint32_t wallUuidCapacityInput;

    // Output, number of walls included in the list.
    uint32_t wallUuidCountOutput;

    // Ordered list of walls of the room layout.
    XrUuidEXT* wallUuids;
} XrRoomLayoutFB;

// 2D boundary for two-call idiom with xrGetSpaceBoundary2DFB.
static const XrStructureType XR_TYPE_BOUNDARY_2D_FB = (XrStructureType)1000175002;
typedef struct XrBoundary2DFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;

    // Input, capacity of the vertex buffer.
    uint32_t vertexCapacityInput;

    // Output, size of the vertex buffer.
    uint32_t vertexCountOutput;

    // Vertices of the polygonal boundary in the coordinate frame of the associated space.
    // Currently only support outer bounds.
    XrVector2f* vertices;
} XrBoundary2DFB;

// Get 2D bounding box associated with space that has bounded 2D component enabled.
typedef XrResult(XRAPI_PTR* PFN_xrGetSpaceBoundingBox2DFB)(
    XrSession session,
    XrSpace space,
    XrRect2Df* boundingBox2DOutput);

// Get 3D bounding box associated with space that has bounded 3D component enabled.
typedef XrResult(XRAPI_PTR* PFN_xrGetSpaceBoundingBox3DFB)(
    XrSession session,
    XrSpace space,
    XrRect3DfFB* boundingBox3DOutput);

// Get semantic labels associated with space that has semantic labels component enabled.
typedef XrResult(XRAPI_PTR* PFN_xrGetSpaceSemanticLabelsFB)(
    XrSession session,
    XrSpace space,
    XrSemanticLabelsFB* semanticLabelsOutput);

// Get 2D boundary associated with space that has bounded 2D component enabled.
typedef XrResult(XRAPI_PTR* PFN_xrGetSpaceBoundary2DFB)(
    XrSession session,
    XrSpace space,
    XrBoundary2DFB* boundary2DOutput);

// Get room layout associated with space that has room layout component enabled.
typedef XrResult(XRAPI_PTR* PFN_xrGetSpaceRoomLayoutFB)(
    XrSession session,
    XrSpace space,
    XrRoomLayoutFB* roomLayoutOutput);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

// Get 2D bounding box associated with space that has bounded 2D component enabled.
XRAPI_ATTR XrResult XRAPI_CALL
xrGetSpaceBoundingBox2DFB(XrSession session, XrSpace space, XrRect2Df* boundingBox2DOutput);

// Get 3D bounding box associated with space that has bounded 3D component enabled.
XRAPI_ATTR XrResult XRAPI_CALL
xrGetSpaceBoundingBox3DFB(XrSession session, XrSpace space, XrRect3DfFB* boundingBox3DOutput);

// Get semantic labels associated with space that has semantic labels component enabled.
// Note: This functions uses two-call idiom:
// 1) When byteCapacityInput == 0, only byteCountOutput will be updated and no semantic label string
// will be copied;
// 2) When byteCapacityInput >= byteCountOutput, semantic labels will be copied to labels as a
// string;
// 3) Otherwise returns XR_ERROR_SIZE_INSUFFICIENT.
XRAPI_ATTR XrResult XRAPI_CALL xrGetSpaceSemanticLabelsFB(
    XrSession session,
    XrSpace space,
    XrSemanticLabelsFB* semanticLabelsOutput);

// Get 2D boundary associated with space that has bounded 2D component enabled.
// Note: This functions uses two-call idiom:
// 1) When vertexCapacityInput == 0, only vertexCountOutput will be updated and no vertices will
// be copied;
// 2) When vertexCapacityInput >= vertexCountOutput, vertices will be copied to boundary2DOutput;
// 3) Otherwise returns XR_ERROR_SIZE_INSUFFICIENT.
XRAPI_ATTR XrResult XRAPI_CALL
xrGetSpaceBoundary2DFB(XrSession session, XrSpace space, XrBoundary2DFB* boundary2DOutput);

// Get room layout associated with space that has room layout component enabled.
// Note: This functions uses two-call idiom:
// 1) When wallUuidCapacityInput == 0, only wallUuidCountOutput will be updated and no UUIDs will
// be copied;
// 2) When wallUuidCapacityInput >= wallUuidCountOutput, UUIDs will be copied to
// entityContainerOutput;
// 3) Otherwise returns XR_ERROR_SIZE_INSUFFICIENT.
XRAPI_ATTR XrResult XRAPI_CALL
xrGetSpaceRoomLayoutFB(XrSession session, XrSpace space, XrRoomLayoutFB* roomLayoutOutput);

#endif // XR_EXTENSION_PROTOTYPES
#endif // !XR_NO_PROTOTYPES
#endif // XR_FB_scene

// Planned additions for spec version 2
#if XR_FB_scene_SPEC_VERSION == 1
#undef XR_FB_scene_SPEC_VERSION
#define XR_FB_scene_SPEC_VERSION 2

typedef XrFlags64 XrSemanticLabelsSupportFlagsFB;

// When this flag is set, the system may return multiple semantic labels separated by comma (,) in
// XrSemanticLabelsFB. Otherwise, the system returns a single semantic label.
static const XrSemanticLabelsSupportFlagsFB XR_SCENE_SUPPORT_MULTIPLE_SEMANTIC_LABELS_FB =
    0x00000001;

// Struct used to tell the system what semantic labels and format in the return are supported by the
// caller. This struct may be provided as the next of XrSemanticLabelsFB. When the next of
// XrSemanticLabelsFB is nullptr, the system assumes the behavior used in spec version 1, i.e.:
// (1) None of the flags are set. Specifically, the system returns a single semantic label.
// (2) recognizedLabels include a set of labels supported in spec version 1. Specifically, they are
// "DESK,COUCH,FLOOR,CEILING,WALL_FACE,WINDOW_FRAME,DOOR_FRAME,OTHER".
static const XrStructureType XR_TYPE_SEMANTIC_LABELS_SUPPORT_INFO_FB = (XrStructureType)1000175010;
typedef struct XrSemanticLabelsSupportInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;

    XrSemanticLabelsSupportFlagsFB flags;

    // Set of semantic labels recognized by the caller. Each semantic label is separated by comma
    // (,). It must be a zero-terminated string.
    const char* recognizedLabels;
} XrSemanticLabelsSupportInfoFB;

#endif // XR_FB_scene_SPEC_VERSION == 1


#ifdef __cplusplus
}
#endif
