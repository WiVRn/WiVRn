// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************
Filename    :   fb_composition_layer_depth_test.h
Content     :   Extension to control if a layer will participate in depth testing. If
                extension is not used no layer will participate in depth testing.
Language    :   C99 Copyright
*************************************************************************************/

#pragma once

#include <openxr/openxr_extension_helpers.h>

/*
  Extension 213 XR_FB_composition_layer_depth_test
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_FB_composition_layer_depth_test

// This extension enables depth-tested layer composition. The compositor will maintain a depth
// buffer in addition to a color buffer. The depth buffer is cleared to a depth corresponding to the
// infinitely far distance at the beginning of composition.

// When composing each layer, if depth testing is requested, the incoming layer depths are
// transformed into the compositor window space depth and compared to the depth stored in the frame
// buffer. If the depth test fails, the fragment is discarded. If the depth test passes the depth
// buffer is updated if depth writes are enabled, and color processing continues.

// Depth testing requires depth values for the layer. For projection layers, this can be supplied
// via the XR_KHR_composition_layer_depth extension. For geometric primitive layers, the runtime
// computes the depth of the sample directly from the layer parameters.
// XrCompositionLayerDepthTestFB may only be chained to layers that support depth.

#define XR_FB_composition_layer_depth_test 1
#define XR_FB_composition_layer_depth_test_SPEC_VERSION 1
#define XR_FB_COMPOSITION_LAYER_DEPTH_TEST_EXTENSION_NAME "XR_FB_composition_layer_depth_test"

typedef enum XrCompareOpFB {
    XR_COMPARE_OP_NEVER_FB = 0,
    XR_COMPARE_OP_LESS_FB = 1,
    XR_COMPARE_OP_EQUAL_FB = 2,
    XR_COMPARE_OP_LESS_OR_EQUAL_FB = 3,
    XR_COMPARE_OP_GREATER_FB = 4,
    XR_COMPARE_OP_NOT_EQUAL_FB = 5,
    XR_COMPARE_OP_GREATER_OR_EQUAL_FB = 6,
    XR_COMPARE_OP_ALWAYS_FB = 7,
} XrCompareOpFB;

// To specify that a layer should be depth tested, a XrCompositionLayerDepthTestFB structure must be
// passed via the polymorphic XrCompositionLayerBaseHeader structure’s next parameter chain.

// Valid Usage (Implicit)
// * The XR_FB_composition_layer_depth_test extension must be enabled prior to using
// XrCompositionLayerDepthTestFB
// * type must be XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_FB
// * next must be NULL or a valid pointer to the next structure in a structure chain
// * compareOp must be a valid XrCompareOpFB value

XR_STRUCT_ENUM(XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_FB, 1000212000);
typedef struct XrCompositionLayerDepthTestFB {
    XrStructureType type;
    void* XR_MAY_ALIAS next;
    XrBool32 depthMask;
    XrCompareOpFB compareOp;
} XrCompositionLayerDepthTestFB;

#endif // XR_FB_composition_layer_depth_test


#ifdef __cplusplus
}
#endif
