// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************
Filename    :   fb_touch_controller_pro.h
Content     :   Touch Controller Pro interaction profile.
Language    :   C99
*************************************************************************************/

#pragma once

#include <openxr/openxr_extension_helpers.h>

/*
  168  XR_FB_touch_controller_pro
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_FB_touch_controller_pro
#define XR_FB_touch_controller_pro 1

#define XR_FBX1_touch_controller_pro_SPEC_VERSION 1
#define XR_FBX1_TOUCH_CONTROLLER_PRO_EXTENSION_NAME "XR_FBX1_touch_controller_pro"
#define XR_FBX2_touch_controller_pro_SPEC_VERSION 2
#define XR_FBX2_TOUCH_CONTROLLER_PRO_EXTENSION_NAME "XR_FBX2_touch_controller_pro"
#define XR_FBX3_touch_controller_pro_SPEC_VERSION 3
#define XR_FBX3_TOUCH_CONTROLLER_PRO_EXTENSION_NAME "XR_FBX3_touch_controller_pro"

#ifndef XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION
#define XR_FB_touch_controller_pro_SPEC_VERSION 1
#define XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME "XR_FB_touch_controller_pro"
#elif XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION == 1
#define XR_FB_touch_controller_pro_SPEC_VERSION XR_FBX1_touch_controller_pro_SPEC_VERSION
#define XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME XR_FBX1_TOUCH_CONTROLLER_PRO_EXTENSION_NAME
#elif XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION == 2
#define XR_FB_touch_controller_pro_SPEC_VERSION XR_FBX2_touch_controller_pro_SPEC_VERSION
#define XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME XR_FBX2_TOUCH_CONTROLLER_PRO_EXTENSION_NAME
#elif XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION == 3
#define XR_FB_touch_controller_pro_SPEC_VERSION XR_FBX3_touch_controller_pro_SPEC_VERSION
#define XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME XR_FBX3_TOUCH_CONTROLLER_PRO_EXTENSION_NAME
#else
#error "unknown experimental version number for XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION"
#endif // XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION

// Facebook Touch Controller PRO Profile
//  Path: /interaction_profiles/facebook/touch_controller_pro
//  Valid for user paths:
//      /user/hand/left
//      /user/hand/right
//
//  This interaction profile represents the input sources and haptics on the Facebook Touch
//  controller pro. This is a superset of the existing `Oculus Touch Controller Profile`
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
//  Additional supported paths enabled by this profile
//
//  On both:
//    …/input/thumbrest/force
//    …/input/stylus_fb/force
//    …/input/trigger/curl_fb
//    …/input/trigger/slide_fb
//    …/input/trigger/proximity_fb
//    …/input/thumb_fb/proximity_fb
//    …/output/trigger_haptic_fb
//    …/output/thumb_haptic_fb

#ifdef XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION
#if XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION == 3
// X3 adds Pinch API support

// Left for an(other) iteration to keep experimental builds working
#define XR_FB_touch_controller_pro_DEPRECATED_EXPERIMENTAL_VERSION 1
#define XR_FB_TOUCH_CONTROLLER_PRO_DEPRECATED_EXPERIMENTAL_EXTENSION_NAME \
    "XR_FBX1_touch_controller_pro"

// system properties
XR_STRUCT_ENUM(XR_TYPE_TOUCH_CONTROLLER_PRO_DEPRECATED_PROPERTIES_FB, 1000167000);
typedef struct XrTouchControllerPro_DEPRECATED_PropertiesFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrBool32 supportsLeftController;
    XrBool32 supportsRightController;
} XrTouchControllerPro_DEPRECATED_PropertiesFB;

// Facebook Touch Controller PRO Profile
//  Path: /interaction_profiles/facebook/touch_controller_pro
//  Valid for user paths:
//      /user/hand/left
//      /user/hand/right
//
//  This interaction profile represents the input sources and haptics on the Facebook Touch
//  controller pro. This is a superset of the existing `Oculus Touch Controller Profile`
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
//  Additional supported paths enabled by this profile
//
//  On both:
//    …/input/thumbrest/force
//    …/input/stylus_fb/force
//    …/input/trigger/curl_fb
//    …/input/trigger/slide_fb
//    …/input/trigger/proximity_fb
//    …/input/thumb_fb/proximity_fb
//    …/input/pinch_fb/value // NEW in X3
//    …/input/pinch_fb/force // NEW in X3
//    …/output/trigger_haptic_fb
//    …/output/thumb_haptic_fb

#elif XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION == 2

// Left for an iteration to keep experimental builds working
#define XR_FB_touch_controller_pro_DEPRECATED_EXPERIMENTAL_VERSION 1
#define XR_FB_TOUCH_CONTROLLER_PRO_DEPRECATED_EXPERIMENTAL_EXTENSION_NAME \
    "XR_FBX1_touch_controller_pro"

// system properties
XR_STRUCT_ENUM(XR_TYPE_TOUCH_CONTROLLER_PRO_DEPRECATED_PROPERTIES_FB, 1000167000);
typedef struct XrTouchControllerPro_DEPRECATED_PropertiesFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrBool32 supportsLeftController;
    XrBool32 supportsRightController;
} XrTouchControllerPro_DEPRECATED_PropertiesFB;

// Facebook Touch Controller PRO Profile
//  Path: /interaction_profiles/facebook/touch_controller_pro
//  Valid for user paths:
//      /user/hand/left
//      /user/hand/right
//
//  This interaction profile represents the input sources and haptics on the Facebook Touch
//  controller pro. This is a superset of the existing `Oculus Touch Controller Profile`
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
//  Additional supported paths enabled by this profile
//
//  On both:
//    …/input/thumbrest/force
//    …/input/stylus_fb/force
//    …/input/trigger/curl_fb
//    …/input/trigger/slide_fb
//    …/input/trigger/proximity_fb
//    …/input/thumb_fb/proximity_fb
//    …/output/trigger_haptic_fb
//    …/output/thumb_haptic_fb

#elif XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION == 1

// Facebook Touch Controller PRO Profile
//  Path: /interaction_profiles/facebook/touch_controller_pro
//  Valid for user paths:
//      /user/hand/left
//      /user/hand/right
//
//  This interaction profile represents the input sources and haptics on the Facebook Touch
//  controller pro. This is a superset of the existing `Oculus Touch Controller Profile`
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
//    …/input/thumbstick/x
//    …/input/thumbstick/y
//    …/input/thumbstick/click
//    …/input/thumbstick/touch
//    …/input/thumbrest/touch
//    …/input/grip/pose
//    …/input/aim/pose
//    …/output/haptic
//
//  Additional supported paths enabled by this profile
//
//  On both:
//    …/input/trackpad/x
//    …/input/trackpad/y
//    …/input/trackpad/force
//    …/input/stylus/force
//    …/input/trigger/curl
//    …/input/trigger/slide
//    …/input/trigger/proximity_fb
//    …/input/thumb_fb/proximity_fb
//    …/output/trigger_haptic
//    …/output/thumb_haptic

// system properties
XR_STRUCT_ENUM(XR_TYPE_TOUCH_CONTROLLER_PRO_PROPERTIES_FB, 1000167000);
typedef struct XrTouchControllerProPropertiesFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    XrBool32 supportsLeftController;
    XrBool32 supportsRightController;
} XrTouchControllerProPropertiesFB;

#endif // XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION == 1

#endif // defined(XR_FB_touch_controller_pro_EXPERIMENTAL_VERSION)

#endif // XR_FB_touch_controller_pro

#ifdef __cplusplus
}
#endif
