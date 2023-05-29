#ifndef META_RECOMMENDED_LAYER_RESOLUTION_H_
#define META_RECOMMENDED_LAYER_RESOLUTION_H_ 1

/**********************
This file is @generated from the OpenXR XML API registry.
Language    :   C99
Copyright   :   (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.
***********************/

#include <openxr/openxr.h>

#ifdef __cplusplus
extern "C" {
#endif


#ifndef XR_META_recommended_layer_resolution

#define XR_META_recommended_layer_resolution 1
#define XR_META_recommended_layer_resolution_SPEC_VERSION 1
#define XR_META_RECOMMENDED_LAYER_RESOLUTION_EXTENSION_NAME "XR_META_recommended_layer_resolution"
#define XR_TYPE_RECOMMENDED_LAYER_RESOLUTION_META ((XrStructureType) 1000254000U)
#define XR_TYPE_RECOMMENDED_LAYER_RESOLUTION_GET_INFO_META ((XrStructureType) 1000254001U)
typedef struct XrRecommendedLayerResolutionMETA {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    XrExtent2Di           recommendedImageDimensions;
    XrBool32              isValid;
} XrRecommendedLayerResolutionMETA;

typedef struct XrRecommendedLayerResolutionGetInfoMETA {
    XrStructureType                  type;
    const void* XR_MAY_ALIAS         next;
    XrCompositionLayerBaseHeader*    layer;
    XrTime                           predictedDisplayTime;
} XrRecommendedLayerResolutionGetInfoMETA;

typedef XrResult (XRAPI_PTR *PFN_xrGetRecommendedLayerResolutionMETA)(XrSession session, const XrRecommendedLayerResolutionGetInfoMETA* layerResolutionRecommendationGetInfo, XrRecommendedLayerResolutionMETA* layerResolutionRecommendation);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES
XRAPI_ATTR XrResult XRAPI_CALL xrGetRecommendedLayerResolutionMETA(
    XrSession                                   session,
    const XrRecommendedLayerResolutionGetInfoMETA* layerResolutionRecommendationGetInfo,
    XrRecommendedLayerResolutionMETA*           layerResolutionRecommendation);
#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */
#endif /* XR_META_recommended_layer_resolution */

#ifdef __cplusplus
}
#endif

#endif
