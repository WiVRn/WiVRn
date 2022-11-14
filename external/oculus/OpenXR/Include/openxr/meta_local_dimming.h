// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************
Filename      : meta_local_dimming.h
Content       : Local dimming extension.
Merge Request : https://gitlab.khronos.org/openxr/openxr/-/merge_requests/2267
Language      : C99
*************************************************************************************/

#pragma once

#include <openxr/openxr_extension_helpers.h>

/*
  Extension 217 XR_META_local_dimming
*/

#if defined(__cplusplus)
extern "C" {
#endif


#ifndef XR_META_local_dimming
#define XR_META_local_dimming 1
#define XR_META_local_dimming_SPEC_VERSION 1
#define XR_META_LOCAL_DIMMING_EXTENSION_NAME "XR_META_local_dimming"

typedef enum XrLocalDimmingModeMETA {
    XR_LOCAL_DIMMING_MODE_OFF_META = 0,
    XR_LOCAL_DIMMING_MODE_ON_META = 1,
    XR_LOCAL_DIMMING_MODE_MAX_ENUM_META = 0x7FFFFFFF
} XrLocalDimmingModeMETA;

XR_STRUCT_ENUM(XR_TYPE_FRAME_END_INFO_LOCAL_DIMMING_META, 1000216000);
struct XrLocalDimmingFrameEndInfoMETA {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrLocalDimmingModeMETA localDimmingMode;
};

#endif // XR_META_local_dimming

#ifdef __cplusplus
}
#endif
