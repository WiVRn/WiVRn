// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "openxr_extension_helpers.h"

/*

Last Modified Date :
    2022-05-17
IP Status :
    No known IP claims.
Contributors :
    Bryce Hutchings, Microsoft
    Yin Li, Microsoft
    Jonathan Wright, Meta Platforms
    Brandon Furtwangler, Meta Platforms
    Wenlin Mao, Meta Platforms
    Andreas Selvik, Meta Platforms
    John Schofield, Meta Platforms
Contacts :
    Cass Everitt, Meta Platforms
    Wenlin Mao, Meta Platforms
*/

#if defined(__cplusplus)
extern "C" {
#endif

// Extension 113
#ifndef XR_EXT_event_channel

#if defined(XR_EXT_event_channel_EXPERIMENTAL_VERSION)
#if XR_EXT_event_channel_EXPERIMENTAL_VERSION != 1
#error "unknown experimental version number for XR_EXT_event_channel_EXPERIMENTAL_VERSION"
#endif

#define XR_EXT_event_channel 1
#define XR_EXT_event_channel_SPEC_VERSION 1
#define XR_EXT_event_channel_EXTENSION_NAME "XR_EXT_event_channel_EXPERIMENTAL_1"

/*
Overview

The OpenXR 1.0 event model is very simple.
There is a single implicit event channel per instance, and the application
polls it for events via flink:xrPollEvent.
This design is sufficient for limited use, but introduces extra burden on
modular application code making use of events for disparate purposes, as it
is the application's responsibility to route any events to the module or
modules that need to handle the event.

This extension expands on the OpenXR event model to add support for multiple
separate event channels.
These event channels are created and destroyed by the application, and they
can: be polled independently.
Operations that may generate events may: also optionally allow the
application to specify the event channel where the events will be delivered.
If left unspecified, such events will be delivered to the implicit
per-instance event channel by default.
*/

/*
XrEventChannelEXT is an event channel with its own queue. It can be polled
independently from the implicit per-instance event channel used by default.
*/
XR_DEFINE_HANDLE(XrEventChannelEXT)

/*
Create info structure contains no parameters beyond the conventional ones.
*/
XR_STRUCT_ENUM(XR_TYPE_EVENT_CHANNEL_CREATE_INFO_EXT, 1000170001);
typedef struct XrEventChannelCreateInfoEXT {
    XrStructureType type; // XR_TYPE_EVENT_CHANNEL_CREATE_INFO_EXT
    const void* XR_MAY_ALIAS next;
} XrEventChannelCreateInfoEXT;

/*
The event channel target structure provides a means to append the
target event channel for an operation that can generate events.
This extension does not define any such operations, but this structure
is expected to be useful for other extensions.
*/
XR_STRUCT_ENUM(XR_TYPE_EVENT_CHANNEL_TARGET_EXT, 1000170002);
typedef struct XrEventChannelTargetEXT {
    XrStructureType type; // XR_TYPE_EVENT_CHANNEL_TARGET_EXT
    const void* XR_MAY_ALIAS next;
    XrEventChannelEXT channel;
} XrEventChannelTargetEXT;

/* flag for select event channel info struct */
typedef enum XrSelectEventChannelFlagsEXT {
    // Reserved for future extensions.
} XrSelectEventChannelFlagsEXT;

XR_STRUCT_ENUM(XR_TYPE_SELECT_EVENT_CHANNEL_INFO_EXT, 1000170003);
/*
info structure for xrSelectEventChannelEXT contains all input parameters for xrSelectEventChannelEXT
to use
 */
typedef struct XrSelectEventChannelInfoEXT {
    XrStructureType type; // XR_TYPE_SELECT_EVENT_CHANNEL_INFO_EXT
    const void* XR_MAY_ALIAS next;
    XrSelectEventChannelFlagsEXT flags;
    XrDuration timeout;
    uint32_t eventChannelCount;
    const XrEventChannelEXT* eventChannels;
} XrSelectEventChannelInfoEXT;

#ifndef XR_NO_PROTOTYPES
#ifdef XR_EXTENSION_PROTOTYPES

/*
functions to create event channel and return a handle
 */
XRAPI_ATTR XrResult XRAPI_CALL xrCreateEventChannelEXT(
    XrInstance instance,
    const XrEventChannelCreateInfoEXT* info,
    XrEventChannelEXT* channel);

/*
Pending or future events assigned to this channel are inaccessible
after its destruction.
*/
XRAPI_ATTR XrResult XRAPI_CALL xrDestroyEventChannelEXT(XrEventChannelEXT channel);

/*
Same semantics as xrPollEvent, but with a specific channel.
*/
XRAPI_ATTR XrResult XRAPI_CALL
xrPollEventChannelEXT(XrEventChannelEXT channel, XrEventDataBuffer* eventData);

/*
Multiple event channels can be checked for available events, and
the application may choose to block for a specified duration waiting
for events to be enqueued.
On success, XR_SUCCESS is returned, and channelWithEvent is set to the
index of the corresponding element of eventChannels.
On timeout, XR_TIMEOUT_EXPIRED is returned, and channelWithEvent is
unmodified.

ISSUE: Should there be any ordering requirements for which channel
is selected when multiple channels have events enqueued?

Current Solution:
No order is specified.
*/
XRAPI_ATTR XrResult XRAPI_CALL xrSelectEventChannelEXT(
    XrInstance instance,
    XrSelectEventChannelInfoEXT* info,
    uint32_t* channelWithEvent);

/*
get default eventchannel handle that is created duing xrCreateInstance to be accessed and used by
application
*/
XRAPI_ATTR XrResult XRAPI_CALL
xrGetDefaultEventChannelEXT(XrInstance instance, XrEventChannelEXT* channel);

#endif /* XR_EXTENSION_PROTOTYPES */
#endif /* !XR_NO_PROTOTYPES */

typedef XrResult(XRAPI_PTR* PFN_xrCreateEventChannelEXT)(
    XrInstance instance,
    const XrEventChannelCreateInfoEXT* info,
    XrEventChannelEXT* channel);

typedef XrResult(XRAPI_PTR* PFN_xrDestroyEventChannelEXT)(XrEventChannelEXT channel);

typedef XrResult(
    XRAPI_PTR* PFN_xrPollEventChannelEXT)(XrEventChannelEXT channel, XrEventDataBuffer* eventData);

typedef XrResult(XRAPI_PTR* PFN_xrSelectEventChannelEXT)(
    XrInstance instance,
    XrSelectEventChannelInfoEXT* info,
    uint32_t* channelWithEvent);

typedef XrResult(
    XRAPI_PTR* PFN_xrGetDefaultEventChannelEXT)(XrInstance instance, XrEventChannelEXT* channel);

#endif // defined(XR_EXT_event_channel_EXPERIMENTAL_VERSION)

#endif // XR_EXT_event_channel

#ifdef __cplusplus
}
#endif
