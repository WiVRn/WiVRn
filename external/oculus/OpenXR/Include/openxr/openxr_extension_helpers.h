// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************

Filename    :   openxr_extension_helpers.h
Content     :   Helpers for private and experimental extension definition headers.
Language    :   C99

*************************************************************************************/

#pragma once

#include <openxr/openxr.h>

#define XR_ENUM(type, enm, constant) static const type enm = (type)constant
#define XR_STRUCT_ENUM(enm, constant) XR_ENUM(XrStructureType, enm, constant)
#define XR_REFSPACE_ENUM(enm, constant) XR_ENUM(XrReferenceSpaceType, enm, constant)
#define XR_RESULT_ENUM(enm, constant) XR_ENUM(XrResult, enm, constant)
#define XR_COMPONENT_ENUM(enm, constant) XR_ENUM(XrComponentTypeFB, enm, constant)
