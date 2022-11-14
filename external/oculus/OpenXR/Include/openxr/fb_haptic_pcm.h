// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************
Filename    :   fb_haptic_pcm.h
Content     :   Haptic PCM extension.
Language    :   C99
*************************************************************************************/

#pragma once

#include <openxr/openxr.h>
#include <openxr/openxr_extension_helpers.h>

/*
  210 XR_FB_haptic_pcm
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_FB_haptic_pcm
#define XR_FB_haptic_pcm 1

#define XR_FBX1_haptic_pcm_SPEC_VERSION 1
#define XR_FBX1_HAPTIC_PCM_EXTENSION_NAME "XR_FBX1_haptic_pcm"

#ifndef XR_FB_haptic_pcm_EXPERIMENTAL_VERSION
#define XR_FB_haptic_pcm_SPEC_VERSION 1
#define XR_FB_HAPTIC_PCM_EXTENSION_NAME "XR_FB_haptic_pcm"
#elif XR_FB_haptic_pcm_EXPERIMENTAL_VERSION == 1
#define XR_FB_haptic_pcm_SPEC_VERSION XR_FBX1_haptic_pcm_SPEC_VERSION
#define XR_FB_HAPTIC_PCM_EXTENSION_NAME XR_FBX1_HAPTIC_PCM_EXTENSION_NAME
#else
#error "unknown experimental version for XR_FB_haptic_pcm"
#endif // XR_FB_haptic_pcm_EXPERIMENTAL_VERSION

/// We have fixed the internal buffer size. The developer may check the bufferCountOutout field of
/// the XrHapticPcmVibrationFB struct to know how many samples were processed.
#define XR_MAX_HAPTIC_PCM_BUFFER_SIZE_FB 4000

XR_STRUCT_ENUM(XR_TYPE_DEVICE_PCM_SAMPLE_RATE_GET_INFO_FB, 1000209002);
/// Provided to xrGetDevicePCMSampleRateFB to retrieve the PCM sample rate
/// of the last connected controller
typedef struct XrDevicePcmSampleRateGetInfoFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    /// value will be populated with a PCM haptic sample rate in Hz
    float sampleRate;
} XrDevicePcmSampleRateGetInfoFB;

XR_STRUCT_ENUM(XR_TYPE_HAPTIC_PCM_VIBRATION_FB, 1000209001);
/// This struct provides a high fidelity control over the haptics effect
/// If triggered on localized haptics (trigger/thumb), this will throw an
/// XR_ERROR_FEATURE_UNSUPPORTED
typedef struct XrHapticPcmVibrationFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    /// number of samples
    uint32_t bufferSize;
    /// the samples that the developer provides, expectation is that these samples are points on a
    /// sine curve
    const float* buffer;
    /// number of samples to be playes per second, this is used to determine the duration of the
    /// effect
    float sampleRate;
    /// if set to false, any existing samples will be cleared and a new haptic effect will begin, if
    /// true, samples will be appended to the currently playing effect
    XrBool32 append;
    /// pointer to an unsigned integer, which contains the number of samples that were consumed from
    /// the input
    uint32_t* samplesConsumed;
} XrHapticPcmVibrationFB;

typedef XrResult(XRAPI_PTR* PFN_xrGetDeviceSampleRateFB)(
    XrSession session,
    const XrHapticActionInfo* hapticActionInfo,
    XrDevicePcmSampleRateGetInfoFB* deviceSampleRate);

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

/// Get the PCM haptics sample rate of the last connected controller
/// If the controller is not supported by the PCM haptics APIs,
/// deviceSampleRate->sampleRate will be set to 0.0f.
XRAPI_ATTR XrResult XRAPI_CALL xrGetDeviceSampleRateFB(
    XrSession session,
    const XrHapticActionInfo* hapticActionInfo,
    XrDevicePcmSampleRateGetInfoFB* deviceSampleRate);

#endif // XR_EXTENSION_PROTOTYPES
#endif // !XR_NO_PROTOTYPES

#ifdef XR_FB_haptic_pcm_EXPERIMENTAL_VERSION

#if XR_FB_haptic_pcm_EXPERIMENTAL_VERSION >= 1

XR_STRUCT_ENUM(XR_TYPE_HAPTIC_PCM_VIBRATION_FBX1, 1000209001);
/// This struct provides a high fidelity control over the haptics effect
/// If triggered on localized haptics (trigger/thumb), this will throw an
/// XR_ERROR_FEATURE_UNSUPPORTED
typedef struct XrHapticPcmVibrationFBX1 {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    /// the samples that the developer provides, expectation is that these samples are points on a
    /// sine curve
    const float* buffer;
    /// number of samples
    uint32_t bufferSize;
    /// number of samples to be playes per second, this is used to determine the duration of the
    /// effect
    float sampleRate;
    /// if set to true, any existing samples will be cleared and a new haptic effect will begin, if
    /// false, samples will be appended to the currently playing effect
    XrBool32 clear;
    /// pointer to an unsigned integer, which contains the number of samples that were consumed from
    /// the input
    uint32_t* bufferCountOutput;
} XrHapticPcmVibrationFBX1;

typedef XrResult(XRAPI_PTR* PFN_xrGetDevicePCMSampleRateFBX1)(
    XrSession session,
    XrDevicePcmSampleRateGetInfoFB* deviceSampleRate);

#endif // XR_FB_haptic_pcm_EXPERIMENTAL_VERSION >= 1

#if XR_FB_haptic_pcm_EXPERIMENTAL_VERSION == 1

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

XRAPI_ATTR XrResult XRAPI_CALL
xrGetDevicePCMSampleRateFB(XrSession session, XrDevicePcmSampleRateGetInfoFB* deviceSampleRate);

#endif // XR_EXTENSION_PROTOTYPES
#endif // !XR_NO_PROTOTYPES

#define PFN_xrGetDevicePCMSampleRateFB PFN_xrGetDevicePCMSampleRateFBX1

#endif // XR_FB_haptic_pcm_EXPERIMENTAL_VERSION == 1

#endif // XR_FB_haptic_pcm_EXPERIMENTAL_VERSION

#endif // XR_FB_haptic_pcm

#ifdef __cplusplus
}
#endif
