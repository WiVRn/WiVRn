// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************
Filename    :   fb_touch_controller_proximity.h
Content     :   Touch Controller Proximity interaction profile extension.
Language    :   C99
*************************************************************************************/

#pragma once

#include <openxr/openxr_extension_helpers.h>

/*
  207  XR_FB_touch_controller_proximity
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_FB_touch_controller_proximity

#define XR_FB_touch_controller_proximity 1
#define XR_FB_touch_controller_proximity_SPEC_VERSION 1
#define XR_FB_TOUCH_CONTROLLER_PROXIMITY_EXTENSION_NAME "XR_FB_touch_controller_proximity"

// Oculus Touch Controller Profile with additional paths
//  Path: /interaction_profiles/oculus/touch_controller
//  Valid for user paths:
//      /user/hand/left
//      /user/hand/right
//
//  This interaction profile represents the input sources and haptics on the Oculus Touch
//  controller. This is a superset of the existing `Oculus Touch Controller Profile`
//  Path: /interaction_profiles/oculus/touch_controller
//
//  Supported component paths compatible with the above:
//
//  On /user/hand/left only:
//    …/input/x/click
//    …/input/x/touch
//    …/input/y/click
//    …/input/y/touch
//    …/input/menu/click
//
//  On /user/hand/right only:
//    …/input/a/click
//    …/input/a/touch
//    …/input/b/click
//    …/input/b/touch
//    …/input/system/click (may not be available for application use)
//
//  On both:
//    …/input/squeeze/value
//    …/input/trigger/value
//    …/input/trigger/touch
//    …/input/thumbstick
//    …/input/thumbstick/x
//    …/input/thumbstick/y
//    …/input/thumbstick/click
//    …/input/thumbstick/touch
//    …/input/thumbrest/touch
//    …/input/grip/pose
//    …/input/aim/pose
//    …/output/haptic
//
//  Additional paths enabled by this extension
//
//  On both:
//    …/input/trigger/proximity_fb
//    …/input/thumb_fb/proximity_fb

#endif // ifndef XR_FB_touch_controller_proximity


#ifdef __cplusplus
}
#endif
