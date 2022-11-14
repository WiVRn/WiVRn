// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

/************************************************************************************
Filename    :   fb_haptic_amplitude_envelope.h
Content     :   Haptic Amplitude Envelope extension.
Language    :   C99
*************************************************************************************/

#pragma once

#include <openxr/openxr_extension_helpers.h>

/*
  174 XR_FB_haptic_amplitude_envelope
*/

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef XR_FB_haptic_amplitude_envelope
#define XR_FB_haptic_amplitude_envelope 1

#define XR_FBX1_haptic_amplitude_envelope_SPEC_VERSION 1
#define XR_FBX1_HAPTIC_AMPLITUDE_ENVELOPE_EXTENSION_NAME "XR_FBX1_haptic_amplitude_envelope"

#ifndef XR_FB_haptic_amplitude_envelope_EXPERIMENTAL_VERSION
#define XR_FB_haptic_amplitude_envelope_SPEC_VERSION 1
#define XR_FB_HAPTIC_AMPLITUDE_ENVELOPE_EXTENSION_NAME "XR_FB_haptic_amplitude_envelope"
#elif XR_FB_haptic_amplitude_envelope_EXPERIMENTAL_VERSION == 1
#define XR_FB_haptic_amplitude_envelope_SPEC_VERSION XR_FBX1_haptic_amplitude_envelope_SPEC_VERSION
#define XR_FB_HAPTIC_AMPLITUDE_ENVELOPE_EXTENSION_NAME \
    XR_FBX1_HAPTIC_AMPLITUDE_ENVELOPE_EXTENSION_NAME
#else
#error \
    "unknown experimental version number for XR_FB_haptic_amplitude_envelope_EXPERIMENTAL_VERSION"
#endif // XR_FB_haptic_amplitude_envelope_EXPERIMENTAL_VERSION

#define XR_MAX_HAPTIC_AMPLITUDE_ENVELOPE_SAMPLES_FB 4000u

XR_STRUCT_ENUM(XR_TYPE_HAPTIC_AMPLITUDE_ENVELOPE_VIBRATION_FB, 1000173001);
/// If triggered on localized haptics (trigger/thumb), this will throw an
/// XR_ERROR_FEATURE_UNSUPPORTED
typedef struct XrHapticAmplitudeEnvelopeVibrationFB {
    XrStructureType type;
    const void* XR_MAY_ALIAS next;
    /// time for which the effect will be played
    XrDuration duration;
    /// number of elements in the array
    uint32_t amplitudeCount;
    /// array representing the amplitude envelope
    const float* amplitudes;
} XrHapticAmplitudeEnvelopeVibrationFB;

#endif // XR_FB_haptic_amplitude_envelope

#ifdef __cplusplus
}
#endif
