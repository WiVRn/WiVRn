/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2023  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2023  Patrick Nicolas <patricknicolas@laposte.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "application.h"

#include "hardware.h"
#include "openxr/openxr.h"
#include "scene.h"
#include "spdlog/common.h"
#include "spdlog/spdlog.h"
#include "utils/class_from_member.h"
#include "utils/contains.h"
#include "utils/files.h"
#include "utils/i18n.h"
#include "vk/check.h"
#include "wifi_lock.h"
#include "wivrn_config.h"
#include "xr/actionset.h"
#include "xr/check.h"
#include "xr/htc_exts.h"
#include "xr/htc_face_tracker.h"
#include "xr/meta_body_tracking_fidelity.h"
#include "xr/to_string.h"
#include <algorithm>
#include <boost/locale.hpp>
#include <boost/url/parse.hpp>
#include <chrono>
#include <ctype.h>
#include <exception>
#include <magic_enum.hpp>
#include <string>
#include <thread>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr_platform.h>

#ifndef NDEBUG
#include "utils/backtrace.h"
#endif

#ifdef __ANDROID__
#include "utils/named_thread.h"
#include <android/native_activity.h>
#include <sys/system_properties.h>

#include "android/jnipp.h"
#else
#include "utils/xdg_base_directory.h"
#include <signal.h>
#endif

using namespace std::chrono_literals;

struct interaction_profile
{
	std::string profile_name;
	std::vector<const char *> required_extensions;
	XrVersion min_version = XR_MAKE_VERSION(1, 0, 0);
	std::vector<std::string> input_sources;
	bool available;
};

static std::vector<interaction_profile> interaction_profiles{
        interaction_profile{
                .profile_name = "/interaction_profiles/khr/simple_controller",
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/select/click",

                        "/user/hand/right/input/menu/click",
                        "/user/hand/right/input/select/click",

                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/oculus/touch_controller",
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/thumbrest/touch",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",
                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/facebook/touch_controller_pro",
                .required_extensions = {XR_FB_TOUCH_CONTROLLER_PRO_EXTENSION_NAME},
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/left/output/haptic_trigger_fb",
                        "/user/hand/left/output/haptic_thumb_fb",
                        "/user/hand/right/output/haptic",
                        "/user/hand/right/output/haptic_trigger_fb",
                        "/user/hand/right/output/haptic_thumb_fb",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/thumbrest/touch",
                        "/user/hand/left/input/thumbrest/force",
                        "/user/hand/left/input/stylus_fb/force",
                        "/user/hand/left/input/trigger/curl_fb",
                        "/user/hand/left/input/trigger/slide_fb",
                        "/user/hand/left/input/trigger/proximity_fb",
                        "/user/hand/left/input/thumb_fb/proximity_fb",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",
                        "/user/hand/right/input/thumbrest/force",
                        "/user/hand/right/input/stylus_fb/force",
                        "/user/hand/right/input/trigger/curl_fb",
                        "/user/hand/right/input/trigger/slide_fb",
                        "/user/hand/right/input/trigger/proximity_fb",
                        "/user/hand/right/input/thumb_fb/proximity_fb",
                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/meta/touch_pro_controller",
                .min_version = XR_MAKE_VERSION(1, 1, 0),
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/left/output/haptic_trigger",
                        "/user/hand/left/output/haptic_thumb",
                        "/user/hand/right/output/haptic",
                        "/user/hand/right/output/haptic_trigger",
                        "/user/hand/right/output/haptic_thumb",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/trigger/proximity",
                        "/user/hand/left/input/trigger_curl/value",
                        "/user/hand/left/input/trigger_slide/value",
                        "/user/hand/left/input/thumb_resting_surfaces/proximity",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/thumbrest/touch",
                        "/user/hand/left/input/thumbrest/force",
                        "/user/hand/left/input/stylus/force",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/trigger/proximity",
                        "/user/hand/right/input/trigger_curl/value",
                        "/user/hand/right/input/trigger_slide/value",
                        "/user/hand/right/input/thumb_resting_surfaces/proximity",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",
                        "/user/hand/right/input/thumbrest/force",
                        "/user/hand/right/input/stylus/force",
                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/meta/touch_controller_plus",
                .required_extensions = {XR_META_TOUCH_CONTROLLER_PLUS_EXTENSION_NAME},
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/thumbrest/touch",
                        "/user/hand/left/input/thumb_meta/proximity_meta",
                        "/user/hand/left/input/trigger/curl_meta",
                        "/user/hand/left/input/trigger/slide_meta",
                        "/user/hand/left/input/trigger/force",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",
                        "/user/hand/right/input/thumb_meta/proximity_meta",
                        "/user/hand/right/input/trigger/curl_meta",
                        "/user/hand/right/input/trigger/slide_meta",
                        "/user/hand/right/input/trigger/force",

                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/meta/touch_plus_controller",
                .min_version = XR_MAKE_VERSION(1, 1, 0),
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/trigger/force",
                        "/user/hand/left/input/trigger/proximity",
                        "/user/hand/left/input/trigger_curl/value",
                        "/user/hand/left/input/trigger_slide/value",
                        "/user/hand/left/input/thumb_resting_surfaces/proximity",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/thumbrest/touch",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/trigger/force",
                        "/user/hand/right/input/trigger/proximity",
                        "/user/hand/right/input/trigger_curl/value",
                        "/user/hand/right/input/trigger_slide/value",
                        "/user/hand/right/input/thumb_resting_surfaces/proximity",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",

                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/bytedance/pico_neo3_controller",
                .required_extensions = {XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME},
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/system/click",
                        "/user/hand/left/input/squeeze/click",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/menu/click",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/click",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",

                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/bytedance/pico4_controller",
                .required_extensions = {XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME},
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/system/click",
                        "/user/hand/left/input/trigger/click",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/squeeze/click",
                        "/user/hand/left/input/squeeze/value",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/trigger/click",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/squeeze/click",
                        "/user/hand/right/input/squeeze/value",
                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/bytedance/pico4s_controller",
                .required_extensions = {XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME},
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/x/touch",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/y/touch",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/system/click",
                        "/user/hand/left/input/trigger/click",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/squeeze/click",
                        "/user/hand/left/input/squeeze/value",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/a/touch",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/b/touch",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/trigger/click",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/squeeze/click",
                        "/user/hand/right/input/squeeze/value",
                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/htc/vive_focus3_controller",
                .required_extensions = {XR_HTC_VIVE_FOCUS3_CONTROLLER_INTERACTION_EXTENSION_NAME},
                .input_sources = {
                        "/user/hand/left/output/haptic",
                        "/user/hand/right/output/haptic",

                        "/user/hand/left/input/grip/pose",
                        "/user/hand/left/input/aim/pose",

                        "/user/hand/right/input/grip/pose",
                        "/user/hand/right/input/aim/pose",

                        "/user/hand/left/input/x/click",
                        "/user/hand/left/input/y/click",
                        "/user/hand/left/input/menu/click",
                        "/user/hand/left/input/squeeze/click",
                        "/user/hand/left/input/squeeze/touch",
                        "/user/hand/left/input/squeeze/value",
                        "/user/hand/left/input/trigger/click",
                        "/user/hand/left/input/trigger/touch",
                        "/user/hand/left/input/trigger/value",
                        "/user/hand/left/input/thumbstick",
                        "/user/hand/left/input/thumbstick/click",
                        "/user/hand/left/input/thumbstick/touch",
                        "/user/hand/left/input/thumbrest/touch",

                        "/user/hand/right/input/a/click",
                        "/user/hand/right/input/b/click",
                        "/user/hand/right/input/system/click",
                        "/user/hand/right/input/squeeze/click",
                        "/user/hand/right/input/squeeze/touch",
                        "/user/hand/right/input/squeeze/value",
                        "/user/hand/right/input/trigger/click",
                        "/user/hand/right/input/trigger/touch",
                        "/user/hand/right/input/trigger/value",
                        "/user/hand/right/input/thumbstick",
                        "/user/hand/right/input/thumbstick/click",
                        "/user/hand/right/input/thumbstick/touch",
                        "/user/hand/right/input/thumbrest/touch",
                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/ext/hand_interaction_ext",
                .required_extensions = {XR_EXT_HAND_INTERACTION_EXTENSION_NAME},
                .input_sources = {
                        "/user/hand/left/input/aim/pose",
                        "/user/hand/left/input/grip/pose",

                        "/user/hand/left/input/pinch_ext/pose",
                        "/user/hand/left/input/pinch_ext/value",
                        "/user/hand/left/input/pinch_ext/ready_ext",
                        "/user/hand/left/input/poke_ext/pose",
                        "/user/hand/left/input/aim_activate_ext/value",
                        "/user/hand/left/input/aim_activate_ext/ready_ext",
                        "/user/hand/left/input/grasp_ext/value",
                        "/user/hand/left/input/grasp_ext/ready_ext",

                        "/user/hand/right/input/aim/pose",
                        "/user/hand/right/input/grip/pose",

                        "/user/hand/right/input/pinch_ext/pose",
                        "/user/hand/right/input/pinch_ext/value",
                        "/user/hand/right/input/pinch_ext/ready_ext",
                        "/user/hand/right/input/poke_ext/pose",
                        "/user/hand/right/input/aim_activate_ext/value",
                        "/user/hand/right/input/aim_activate_ext/ready_ext",
                        "/user/hand/right/input/grasp_ext/value",
                        "/user/hand/right/input/grasp_ext/ready_ext",
                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/htc/vive_xr_tracker",
                .required_extensions = {
                        XR_HTC_VIVE_XR_TRACKER_INTERACTION_EXTENSION_NAME,
                        XR_HTC_PATH_ENUMERATION_EXTENSION_NAME,
                },
        },
        interaction_profile{
                .profile_name = "/interaction_profiles/ext/eye_gaze_interaction",
                .required_extensions = {XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME},
                .input_sources = {
                        "/user/eyes_ext/input/gaze_ext/pose",
                },
        },
};

static const std::pair<std::string_view, XrActionType> action_suffixes[] =
        {
                // clang-format off
		// From OpenXR spec 1.1.43, §6.3.2 Input subpaths
                // + extensions

		// Standard components
		{"/click",       XR_ACTION_TYPE_BOOLEAN_INPUT},
		{"/touch",       XR_ACTION_TYPE_BOOLEAN_INPUT},
		{"/proximity",   XR_ACTION_TYPE_BOOLEAN_INPUT},
		{"/proximity_fb",XR_ACTION_TYPE_BOOLEAN_INPUT},
		{"/force",       XR_ACTION_TYPE_FLOAT_INPUT},
		{"/value",       XR_ACTION_TYPE_FLOAT_INPUT},
		{"/x",           XR_ACTION_TYPE_FLOAT_INPUT},
		{"/y",           XR_ACTION_TYPE_FLOAT_INPUT},
		{"/twist",       XR_ACTION_TYPE_FLOAT_INPUT},
		{"/curl_fb",     XR_ACTION_TYPE_FLOAT_INPUT},
		{"/curl_meta",   XR_ACTION_TYPE_FLOAT_INPUT},
		{"/pose",        XR_ACTION_TYPE_POSE_INPUT},

		// Standard 2D identifier, can be used without the /x and /y cmoponents
		{"/trackpad",   XR_ACTION_TYPE_VECTOR2F_INPUT},
		{"/thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT},
		{"/joystick",   XR_ACTION_TYPE_VECTOR2F_INPUT},
		{"/trackball",  XR_ACTION_TYPE_VECTOR2F_INPUT},

		// XR_EXT_hand_interaction
		{"/ready_ext", XR_ACTION_TYPE_BOOLEAN_INPUT},

		// Output paths
		{"/haptic",           XR_ACTION_TYPE_VIBRATION_OUTPUT},
		{"/haptic_trigger",   XR_ACTION_TYPE_VIBRATION_OUTPUT},
		{"/haptic_trigger_fb",XR_ACTION_TYPE_VIBRATION_OUTPUT},
		{"/haptic_thumb",     XR_ACTION_TYPE_VIBRATION_OUTPUT},
		{"/haptic_thumb_fb",  XR_ACTION_TYPE_VIBRATION_OUTPUT},
                // clang-format on
};

static XrActionType guess_action_type(const std::string & name)
{
	for (const auto & [suffix, type]: action_suffixes)
	{
		if (name.ends_with(suffix))
			return type;
	}

	return XR_ACTION_TYPE_FLOAT_INPUT;
}

VkBool32 application::vulkan_debug_report_callback(
#if VK_HEADER_VERSION >= 304
        vk::DebugReportFlagsEXT flags_,
        vk::DebugReportObjectTypeEXT objectType,
#else
        VkDebugReportFlagsEXT flags_,
        VkDebugReportObjectTypeEXT objectType,
#endif
        uint64_t object,
        size_t location,
        int32_t messageCode,
        const char * pLayerPrefix,
        const char * pMessage,
        void * pUserData)
{
	std::lock_guard lock(instance().debug_report_mutex);
	if (instance().debug_report_ignored_objects.contains(object))
		return VK_FALSE;

	spdlog::level::level_enum level = spdlog::level::info;

	if (vk::DebugReportFlagsEXT flags(flags_); flags & vk::DebugReportFlagBitsEXT::eInformation)
		level = spdlog::level::info;
	else if (flags & (vk::DebugReportFlagBitsEXT::eWarning | vk::DebugReportFlagBitsEXT::ePerformanceWarning))
		level = spdlog::level::warn;
	else if (flags & vk::DebugReportFlagBitsEXT::eError)
		level = spdlog::level::err;
	else if (flags & vk::DebugReportFlagBitsEXT::eDebug)
		level = spdlog::level::debug;

	// for(const std::string& s: utils::split(pMessage, "|"))
	// spdlog::log(level, s);
	spdlog::log(level, pMessage);

	auto it = instance().debug_report_object_name.find(object);
	if (it != instance().debug_report_object_name.end())
	{
		spdlog::log(level, "{:#016x}: {}", object, it->second);
	}

#ifndef NDEBUG
	bool my_error = true;
	if (level >= spdlog::level::warn)
	{
		bool validation_layer_found = false;
		for (auto & i: utils::backtrace(20))
		{
			if (i.library == "libVkLayer_khronos_validation.so")
				validation_layer_found = true;

			if (validation_layer_found && i.library != "libVkLayer_khronos_validation.so")
				spdlog::log(level, "{:#016x}: {} + {:#x}", i.pc, i.library, i.pc - i.library_base);

			if (i.library == "libopenxr_loader.so")
				my_error = false;
		}
	}

	if (level >= spdlog::level::err and my_error)
		abort();
#endif

	return VK_FALSE;
}

std::string parse_driver_sersion(const vk::PhysicalDeviceProperties & p)
{
	// See https://github.com/SaschaWillems/vulkan.gpuinfo.org/blob/1e6ca6e3c0763daabd6a101b860ab4354a07f5d3/functions.php#L294
	switch (p.vendorID)
	{
		case 0x10de: // nvidia
			return fmt::format(
			        "{}.{}.{}.{}",
			        (p.driverVersion >> 22) & 0x3ff,
			        (p.driverVersion >> 14) & 0xff,
			        (p.driverVersion >> 6) & 0xff,
			        p.driverVersion & 0x3f);

		default:
			return fmt::format(
			        "{}.{}.{}",
			        VK_VERSION_MAJOR(p.driverVersion),
			        VK_VERSION_MINOR(p.driverVersion),
			        VK_VERSION_PATCH(p.driverVersion));
	}
}

void application::initialize_vulkan()
{
	auto graphics_requirements = xr_system_id.graphics_requirements();
	XrVersion vulkan_version = std::max(app_info.min_vulkan_version, graphics_requirements.minApiVersionSupported);
	spdlog::info("OpenXR runtime wants Vulkan {}", xr::to_string(graphics_requirements.minApiVersionSupported));
	spdlog::info("Requesting Vulkan {}", xr::to_string(vulkan_version));

	std::vector<const char *> layers;

	spdlog::info("Available Vulkan layers:");
	[[maybe_unused]] bool validation_layer_found = false;

	for (vk::LayerProperties & i: vk_context.enumerateInstanceLayerProperties())
	{
		spdlog::info("    {}", i.layerName.data());
		if (!strcmp(i.layerName, "VK_LAYER_KHRONOS_validation"))
		{
			validation_layer_found = true;
		}
	}
#ifndef NDEBUG
	if (validation_layer_found)
	{
		spdlog::info("Using Vulkan validation layer");
		layers.push_back("VK_LAYER_KHRONOS_validation");
	}
	bool debug_report_found = false;
	bool debug_utils_found = false;
#endif

	std::vector<const char *> instance_extensions{};
	std::unordered_set<std::string_view> optional_device_extensions{};

	spdlog::info("Available Vulkan instance extensions:");
	std::vector<std::pair<std::string, int>> extensions;
	for (vk::ExtensionProperties & i: vk_context.enumerateInstanceExtensionProperties(nullptr))
	{
		extensions.emplace_back(i.extensionName.data(), i.specVersion);

#ifndef NDEBUG
		if (!strcmp(i.extensionName, VK_EXT_DEBUG_REPORT_EXTENSION_NAME))
		{
			debug_report_found = true;
			instance_extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		}

		if (!strcmp(i.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) and
		    guess_model() != model::oculus_quest) // Quest 1 lies, the extension won't load
		{
			debug_utils_found = true;
			instance_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
#endif
	}
	std::ranges::sort(extensions);
	for (const auto & [extension_name, spec_version]: extensions)
		spdlog::info("    {} (version {})", extension_name, spec_version);

	vk_device_extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
	vk_device_extensions.push_back(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
	vk_device_extensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
	optional_device_extensions.emplace(VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);
	optional_device_extensions.emplace(VK_IMG_FILTER_CUBIC_EXTENSION_NAME);
	optional_device_extensions.emplace(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
	optional_device_extensions.emplace(VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME);

#ifdef __ANDROID__
	vk_device_extensions.push_back(VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
	vk_device_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
	vk_device_extensions.push_back(VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME);
	vk_device_extensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
	vk_device_extensions.push_back(VK_KHR_MAINTENANCE1_EXTENSION_NAME);
	vk_device_extensions.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
	vk_device_extensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
	instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
	instance_extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
#endif

	vk::ApplicationInfo application_info{
	        .pApplicationName = app_info.name.c_str(),
	        .applicationVersion = (uint32_t)app_info.version,
	        .pEngineName = engine_name,
	        .engineVersion = engine_version,
	        .apiVersion = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(vulkan_version), XR_VERSION_MINOR(vulkan_version), 0),
	};

	vk::InstanceCreateInfo instance_create_info{
	        .pApplicationInfo = &application_info,
	};
	instance_create_info.setPEnabledLayerNames(layers);
	instance_create_info.setPEnabledExtensionNames(instance_extensions);

	XrVulkanInstanceCreateInfoKHR create_info{
	        .type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR,
	        .systemId = xr_system_id,
	        .createFlags = 0,
	        .pfnGetInstanceProcAddr = vkGetInstanceProcAddr,
	        .vulkanCreateInfo = &(VkInstanceCreateInfo &)instance_create_info,
	        .vulkanAllocator = nullptr,
	};

	auto xrCreateVulkanInstanceKHR =
	        xr_instance.get_proc<PFN_xrCreateVulkanInstanceKHR>("xrCreateVulkanInstanceKHR");

	VkResult vresult;
	VkInstance tmp;
	XrResult xresult = xrCreateVulkanInstanceKHR(xr_instance, &create_info, &tmp, &vresult);
	CHECK_VK(vresult, "xrCreateVulkanInstanceKHR");
	CHECK_XR(xresult, "xrCreateVulkanInstanceKHR");
	vk_instance = vk::raii::Instance(vk_context, tmp);

#ifndef NDEBUG
	if (debug_report_found)
	{
		vk::DebugReportCallbackCreateInfoEXT debug_report_info{
		        .flags = vk::DebugReportFlagBitsEXT::eInformation |
		                 vk::DebugReportFlagBitsEXT::eWarning |
		                 vk::DebugReportFlagBitsEXT::ePerformanceWarning |
		                 vk::DebugReportFlagBitsEXT::eError |
		                 vk::DebugReportFlagBitsEXT::eDebug,
		        .pfnCallback = vulkan_debug_report_callback,
		};
		debug_report_callback = vk::raii::DebugReportCallbackEXT(vk_instance, debug_report_info);
	}
#endif

	vk_physical_device = xr_system_id.physical_device(vk_instance);
	physical_device_properties = vk_physical_device.getProperties();

	extensions.clear();
	for (vk::ExtensionProperties & i: vk_physical_device.enumerateDeviceExtensionProperties())
		extensions.emplace_back(i.extensionName.data(), i.specVersion);
	std::ranges::sort(extensions);

	spdlog::info("Available Vulkan device extensions:");
	for (const auto & [extension_name, spec_version]: extensions)
	{
		spdlog::info("    {} (version {})", extension_name, spec_version);
		if (auto it = optional_device_extensions.find(extension_name); it != optional_device_extensions.end())
			vk_device_extensions.push_back(it->data());
	}

	spdlog::info("Initializing Vulkan with device {}", physical_device_properties.deviceName.data());
	spdlog::info("    Vendor ID: 0x{:04x}", physical_device_properties.vendorID);
	spdlog::info("    Device ID: 0x{:04x}", physical_device_properties.deviceID);
	spdlog::info("    Driver version: {}", parse_driver_sersion(physical_device_properties));

	std::vector<vk::QueueFamilyProperties> queue_properties = vk_physical_device.getQueueFamilyProperties();

	vk_queue_family_index = -1;
	[[maybe_unused]] bool vk_queue_found = false;
	for (size_t i = 0; i < queue_properties.size(); i++)
	{
		if (queue_properties[i].queueFlags & vk::QueueFlagBits::eGraphics)
		{
			vk_queue_found = true;
			vk_queue_family_index = i;
			break;
		}
	}
	assert(vk_queue_found);

	float queuePriority = 0.0f;

	vk::DeviceQueueCreateInfo queueCreateInfo{
	        .queueFamilyIndex = vk_queue_family_index,
	        .queueCount = 1,
	        .pQueuePriorities = &queuePriority,
	};

	vk::PhysicalDeviceFeatures device_features{
	        .shaderClipDistance = true,
	};

	vk::StructureChain device_create_info{
	        vk::DeviceCreateInfo{
	                .queueCreateInfoCount = 1,
	                .pQueueCreateInfos = &queueCreateInfo,
	                .enabledExtensionCount = (uint32_t)vk_device_extensions.size(),
	                .ppEnabledExtensionNames = vk_device_extensions.data(),
	                .pEnabledFeatures = &device_features,
	        },
	        vk::PhysicalDeviceSamplerYcbcrConversionFeaturesKHR{
	                .samplerYcbcrConversion = true,
	        },
	        vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR{},
	        vk::PhysicalDeviceMultiviewFeaturesKHR{
	                .multiview = true,
	        },
	        vk::PhysicalDeviceIndexTypeUint8FeaturesEXT{}};

	auto check_feature_flag = [&](auto feature_flag, const char * extension_name) -> bool {
		using FeatureStruct = class_from_member_t<decltype(feature_flag)>;

		if (utils::contains(vk_device_extensions, extension_name) and
		    vk_physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, FeatureStruct>().template get<FeatureStruct>().*feature_flag)
		{
			device_create_info.get<FeatureStruct>().*feature_flag = true;
			return true;
		}
		else
		{
			device_create_info.unlink<FeatureStruct>();
			return false;
		}
	};

	check_feature_flag(&vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR::timelineSemaphore, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
	check_feature_flag(&vk::PhysicalDeviceIndexTypeUint8FeaturesEXT::indexTypeUint8, VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME);

	vk_device = xr_system_id.create_device(vk_physical_device, device_create_info.get());
	*vk_queue.lock() = vk_device.getQueue(vk_queue_family_index, 0);

	vk::PipelineCacheCreateInfo pipeline_cache_info;
	std::vector<std::byte> pipeline_cache_bytes;

	try
	{
		// TODO Robust pipeline cache serialization
		// https://zeux.io/2019/07/17/serializing-pipeline-cache/
		pipeline_cache_bytes = utils::read_whole_file<std::byte>(cache_path / "pipeline_cache");

		pipeline_cache_info.setInitialData<std::byte>(pipeline_cache_bytes);
	}
	catch (...)
	{
	}

	pipeline_cache = vk::raii::PipelineCache(vk_device, pipeline_cache_info);

	allocator.emplace(VmaAllocatorCreateInfo{
	                          .physicalDevice = *vk_physical_device,
	                          .device = *vk_device,
	                          .instance = *vk_instance,
	                  },
#ifndef NDEBUG
	                  debug_utils_found
#else
	                  false
#endif
	);
}

void application::log_views()
{
	for (XrViewConfigurationType i: xr_system_id.view_configurations())
	{
		spdlog::info("View configuration {}", xr::to_string(i));
		XrViewConfigurationProperties p = xr_system_id.view_configuration_properties(i);

		spdlog::info("    fovMutable: {}", p.fovMutable ? "true" : "false");

		int n = 0;
		for (const XrViewConfigurationView & j: xr_system_id.view_configuration_views(i))
		{
			spdlog::info("    View {}:", ++n);
			spdlog::info("        Recommended: {}x{}, {} sample(s)", j.recommendedImageRectWidth, j.recommendedImageRectHeight, j.recommendedSwapchainSampleCount);
			spdlog::info("        Maximum:     {}x{}, {} sample(s)", j.maxImageRectWidth, j.maxImageRectHeight, j.maxSwapchainSampleCount);
		}

		for (const XrEnvironmentBlendMode j: xr_system_id.environment_blend_modes(i))
		{
			spdlog::info("    Blend mode: {}", xr::to_string(j));
		}
	}
}

static std::string make_xr_name(std::string name)
{
	// Generate a name suitable for a path component (see OpenXR spec §6.2)
	for (char & c: name)
	{
		if (!isalnum(c) && c != '-' && c != '_' && c != '.')
			c = '_';
		else
			c = tolower(c);
	}

	size_t pos = name.find_first_of("abcdefghijklmnopqrstuvwxyz");

	return name.substr(pos);
}

void application::initialize_actions()
{
	spdlog::debug("Initializing actions");

	// Build an action set with all possible input sources
	std::vector<XrActionSet> action_sets;
	xr_actionset = xr::actionset(xr_instance, "all_actions", "All actions");
	action_sets.push_back(xr_actionset);

	std::unordered_map<std::string, std::vector<XrActionSuggestedBinding>> suggested_bindings;

	XrVersion api_version = xr_instance.get_api_version();
	// Build the list of all possible input sources, without duplicates,
	// checking which profiles are supported by the runtime
	std::vector<std::string> sources;
	for (auto & profile: interaction_profiles)
	{
		profile.available = std::ranges::all_of(profile.required_extensions, [&](auto & ext) { return xr_instance.has_extension(ext); }) and
		                    profile.min_version <= api_version;

		if (profile.profile_name.ends_with("khr/simple_controller"))
		{
			switch (guess_model())
			{
				// Quest hand tracking creates a fake khr/simple_controller when hand tracking
				// is enabled, this messes with native hand tracking
				case model::meta_quest_3:
				case model::meta_quest_pro:
				case model::meta_quest_3s:
				case model::oculus_quest_2:
					profile.available = false;
				default:
					break;
			}
		}

		if (!profile.available)
			continue;

		// Patch profile to add grip_surface or palm_ext
		bool add_palms = true;
		if (profile.profile_name.ends_with("ext/hand_interaction_ext"))
		{
			switch (guess_model())
			{
				// Quest breaks spec and does not support grip_surface for ext/hand_interaction_ext
				case model::meta_quest_3:
				case model::meta_quest_pro:
				case model::meta_quest_3s:
				case model::oculus_quest_2:
				case model::oculus_quest:
					add_palms = false;
					break;
				default:
					break;
			}
		}
		if (add_palms)
		{
			if ((api_version >= XR_MAKE_VERSION(1, 1, 0) or xr_instance.has_extension(XR_KHR_MAINTENANCE1_EXTENSION_NAME)) //
			    and utils::contains(profile.input_sources, "/user/hand/left/input/grip/pose")                              //
			    and not utils::contains(profile.input_sources, "/user/hand/left/input/grip_surface/pose"))
			{
				spdlog::info("Adding grip_surface/pose for interaction profile {}", profile.profile_name);
				profile.input_sources.push_back("/user/hand/left/input/grip_surface/pose");
				profile.input_sources.push_back("/user/hand/right/input/grip_surface/pose");
			}
			else if (xr_instance.has_extension(XR_EXT_PALM_POSE_EXTENSION_NAME)                    //
			         and utils::contains(profile.input_sources, "/user/hand/left/input/grip/pose") //
			         and not utils::contains(profile.input_sources, "/user/hand/left/input/palm_ext/pose"))
			{
				spdlog::info("Adding palm_ext/pose for interaction profile {}", profile.profile_name);
				profile.input_sources.push_back("/user/hand/left/input/palm_ext/pose");
				profile.input_sources.push_back("/user/hand/right/input/palm_ext/pose");
			}
		}

		// Patch profile to add pinch_ext/pose and poke_ext/pose
		if (!profile.profile_name.ends_with("ext/hand_interaction_ext") && xr_instance.has_extension(XR_EXT_HAND_INTERACTION_EXTENSION_NAME))
		{
			spdlog::info("Adding pinch_ext/pose for interaction profile {}", profile.profile_name);
			profile.input_sources.push_back("/user/hand/left/input/pinch_ext/pose");
			profile.input_sources.push_back("/user/hand/right/input/pinch_ext/pose");
			spdlog::info("Adding poke_ext/pose for interaction profile {}", profile.profile_name);
			profile.input_sources.push_back("/user/hand/left/input/poke_ext/pose");
			profile.input_sources.push_back("/user/hand/right/input/poke_ext/pose");
		}

		// Dynamically add VIVE XR Trackers to the profile if available
		if (utils::contains(profile.required_extensions, XR_HTC_VIVE_XR_TRACKER_INTERACTION_EXTENSION_NAME) and
		    xr_instance.has_extension(XR_HTC_PATH_ENUMERATION_EXTENSION_NAME))
		{
			XrPath tracker_profile = xr_instance.string_to_path("/interaction_profiles/htc/vive_xr_tracker");
			for (const auto & user_path: xr_instance.enumerate_paths_for_interaction_profile(tracker_profile))
			{
				generic_trackers.emplace_back(user_path, nullptr);
				for (const auto & input_path: xr_instance.enumerate_paths_for_interaction_profile(tracker_profile, user_path))
					profile.input_sources.push_back(xr_instance.path_to_string(user_path) + xr_instance.path_to_string(input_path));
			}
		}

		suggested_bindings.emplace(profile.profile_name, std::vector<XrActionSuggestedBinding>{});

		for (const std::string & source: profile.input_sources)
		{
			if (!utils::contains(sources, source))
				sources.push_back(source);
		}
	}

	// For each possible input source, create a XrAction and add it to the suggested binding
	std::unordered_map<std::string, XrAction> actions_by_name;

	for (std::string & name: sources)
	{
		std::string name_without_slashes = make_xr_name(name);

		XrActionType type = guess_action_type(name);

		auto a = xr_actionset.create_action(type, name_without_slashes);
		actions.emplace_back(a, type, name);
		actions_by_name.emplace(name, a);

		if (name == "/user/hand/left/input/grip/pose")
			spaces[size_t(xr::spaces::grip_left)] = xr_session.create_action_space(a);
		else if (name == "/user/hand/left/input/aim/pose")
			spaces[size_t(xr::spaces::aim_left)] = xr_session.create_action_space(a);
		else if (name == "/user/hand/left/input/palm_ext/pose" or name == "/user/hand/left/input/grip_surface/pose")
			spaces[size_t(xr::spaces::palm_left)] = xr_session.create_action_space(a);
		else if (name == "/user/hand/left/input/pinch_ext/pose")
			spaces[size_t(xr::spaces::pinch_left)] = xr_session.create_action_space(a);
		else if (name == "/user/hand/left/input/poke_ext/pose")
			spaces[size_t(xr::spaces::poke_left)] = xr_session.create_action_space(a);
		else if (name == "/user/hand/right/input/grip/pose")
			spaces[size_t(xr::spaces::grip_right)] = xr_session.create_action_space(a);
		else if (name == "/user/hand/right/input/aim/pose")
			spaces[size_t(xr::spaces::aim_right)] = xr_session.create_action_space(a);
		else if (name == "/user/hand/right/input/palm_ext/pose" or name == "/user/hand/right/input/grip_surface/pose")
			spaces[size_t(xr::spaces::palm_right)] = xr_session.create_action_space(a);
		else if (name == "/user/hand/right/input/pinch_ext/pose")
			spaces[size_t(xr::spaces::pinch_right)] = xr_session.create_action_space(a);
		else if (name == "/user/hand/right/input/poke_ext/pose")
			spaces[size_t(xr::spaces::poke_right)] = xr_session.create_action_space(a);
		else if (name == "/user/eyes_ext/input/gaze_ext/pose")
			spaces[size_t(xr::spaces::eye_gaze)] = xr_session.create_action_space(a);
		else if (name.contains("/input/entity_htc/pose"))
		{
			for (auto & [path, action]: generic_trackers)
			{
				if (name.starts_with(xr_instance.path_to_string(path)))
					action = xr_session.create_action_space(a);
			}
		}
	}

	// Build an action set for each scene
	for (scene::meta * i: scene::scene_registry)
	{
		std::string actionset_name = make_xr_name(i->name);

		i->actionset = xr::actionset(xr_instance, actionset_name, i->name);
		action_sets.push_back(i->actionset);

		for (auto & [action_name, action_type]: i->actions)
		{
			XrAction a = i->actionset.create_action(action_type, action_name);
			i->actions_by_name[action_name] = std::make_pair(a, action_type);

			if (action_type == XR_ACTION_TYPE_POSE_INPUT)
				i->spaces_by_name[action_name] = xr_session.create_action_space(a);
		}

		for (const scene::suggested_binding & j: i->bindings)
		{
			for (const auto & profile: j.profile_names)
			{
				// Skip unsupported profiles
				if (!suggested_bindings.contains(profile))
					continue;

				std::vector<XrActionSuggestedBinding> & xr_bindings = suggested_bindings[profile];

				for (const scene::action_binding & k: j.paths)
				{
					XrAction a = i->actions_by_name[k.action_name].first;
					assert(a != XR_NULL_HANDLE);

					xr_bindings.push_back(XrActionSuggestedBinding{
					        .action = a,
					        .binding = xr_instance.string_to_path(k.input_source)});
				}
			}
		}
	}

	// Suggest bindings for all supported controllers
	for (const auto & profile: interaction_profiles)
	{
		// Skip unavailable interaction profiles
		if (!profile.available)
			continue;

		std::vector<XrActionSuggestedBinding> & xr_bindings = suggested_bindings[profile.profile_name];

		for (const auto & name: profile.input_sources)
		{
			xr_bindings.push_back({actions_by_name[name], xr_instance.string_to_path(name)});
		}

		try
		{
			xr_instance.suggest_bindings(profile.profile_name, xr_bindings);
		}
		catch (...)
		{
			// Ignore errors
		}
	}

	xr_session.attach_actionsets(action_sets);
}

void application::initialize()
{
	// LogLayersAndExtensions
	assert(!xr_instance);
	std::vector<const char *> xr_extensions{
	        // Required extensions
	        XR_KHR_CONVERT_TIMESPEC_TIME_EXTENSION_NAME,
	};

	// Optional extensions
	std::vector<const char *> opt_extensions{
	        XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME,
	        XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME,
	        XR_KHR_LOCATE_SPACES_EXTENSION_NAME,
	        XR_KHR_MAINTENANCE1_EXTENSION_NAME,
	        XR_KHR_VISIBILITY_MASK_EXTENSION_NAME,

	        XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME,
	        XR_EXT_HAND_INTERACTION_EXTENSION_NAME,
	        XR_EXT_HAND_TRACKING_EXTENSION_NAME,
	        XR_EXT_PALM_POSE_EXTENSION_NAME,
	        XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME,
	        XR_EXT_USER_PRESENCE_EXTENSION_NAME,

	        XR_ANDROID_FACE_TRACKING_EXTENSION_NAME,

	        XR_BD_BODY_TRACKING_EXTENSION_NAME,

	        XR_FB_BODY_TRACKING_EXTENSION_NAME,
	        XR_FB_COMPOSITION_LAYER_DEPTH_TEST_EXTENSION_NAME,
	        XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME,
	        XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
	        XR_FB_FACE_TRACKING2_EXTENSION_NAME,
	        // Disable foveation, doesn't seem useful
	        // XR_FB_FOVEATION_CONFIGURATION_EXTENSION_NAME,
	        // XR_FB_FOVEATION_EXTENSION_NAME,
	        // XR_FB_FOVEATION_VULKAN_EXTENSION_NAME,
	        XR_FB_PASSTHROUGH_EXTENSION_NAME,
	        XR_FB_SWAPCHAIN_UPDATE_STATE_EXTENSION_NAME,

	        XR_HTC_PASSTHROUGH_EXTENSION_NAME,
	        XR_HTC_PATH_ENUMERATION_EXTENSION_NAME,
	        XR_HTC_FACIAL_TRACKING_EXTENSION_NAME,
	        XR_HTC_VIVE_XR_TRACKER_INTERACTION_EXTENSION_NAME,

	        XR_META_BODY_TRACKING_FIDELITY_EXTENSION_NAME,
	        XR_META_BODY_TRACKING_FULL_BODY_EXTENSION_NAME,
	};

	for (const auto & i: interaction_profiles)
		opt_extensions.insert(opt_extensions.end(), i.required_extensions.begin(), i.required_extensions.end());

	for (const auto & ext: xr::instance::extensions())
	{
		auto it = std::find_if(opt_extensions.begin(),
		                       opt_extensions.end(),
		                       [&ext](const char * i) { return strcmp(i, ext.extensionName) == 0; });
		if (it != opt_extensions.end())
			xr_extensions.push_back(*it);
	}

#ifdef __ANDROID__
	xr_instance =
	        xr::instance(app_info.name, app_info.native_app->activity->vm, app_info.native_app->activity->clazz, xr_extensions);
#else
	xr_instance = xr::instance(app_info.name, xr_extensions);
#endif

	spdlog::info("Created OpenXR instance, runtime {}, version {}, API version {}",
	             xr_instance.get_runtime_name(),
	             xr_instance.get_runtime_version(),
	             xr::to_string(xr_instance.get_api_version()));

	xr_system_id = xr::system(xr_instance, app_info.formfactor);
	spdlog::info("Created OpenXR system for form factor {}", xr::to_string(app_info.formfactor));

	// Log system properties
	XrSystemProperties properties = xr_system_id.properties();
	spdlog::info("OpenXR system properties:");
	spdlog::info("    Vendor ID: {:#x}", properties.vendorId);
	spdlog::info("    System name: {}", properties.systemName);
	spdlog::info("    Graphics properties:");
	spdlog::info("        Maximum swapchain image size: {}x{}", properties.graphicsProperties.maxSwapchainImageWidth, properties.graphicsProperties.maxSwapchainImageHeight);
	spdlog::info("        Maximum layer count: {}", properties.graphicsProperties.maxLayerCount);
	spdlog::info("    Tracking properties:");
	spdlog::info("        Orientation tracking: {}", (bool)properties.trackingProperties.orientationTracking);
	spdlog::info("        Position tracking: {}", (bool)properties.trackingProperties.positionTracking);

	spdlog::info("    Hand tracking support: {}", xr_system_id.hand_tracking_supported());

	if (xr_instance.has_extension(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME))
	{
		XrSystemEyeGazeInteractionPropertiesEXT eye_gaze_properties = xr_system_id.eye_gaze_interaction_properties();
		spdlog::info("    Eye gaze support: {}", (bool)eye_gaze_properties.supportsEyeGazeInteraction);
		eye_gaze_supported = eye_gaze_properties.supportsEyeGazeInteraction;
	}

	if (xr_instance.has_extension(XR_FB_COMPOSITION_LAYER_SETTINGS_EXTENSION_NAME))
	{
		spdlog::info("    OpenXR post-processing extension support: true");
		openxr_post_processing_supported = true;
	}

	switch (xr_system_id.passthrough_supported())
	{
		case xr::passthrough_type::none:
			spdlog::info("    Passthrough: not supported");
			break;
		case xr::passthrough_type::bw:
			spdlog::info("    Passthrough: black and white");
			break;
		case xr::passthrough_type::color:
			spdlog::info("    Passthrough: color");
			break;
	}

	spdlog::info("    Face tracker: {}", magic_enum::enum_name(xr_system_id.face_tracker_supported()));
	spdlog::info("    Body tracker: {}", magic_enum::enum_name(xr_system_id.body_tracker_supported()));

	// Log view configurations and blend modes
	log_views();

	initialize_vulkan();

	xr_session = xr::session(xr_instance, xr_system_id, vk_instance, vk_physical_device, vk_device, vk_queue, vk_queue_family_index);

	spaces[size_t(xr::spaces::view)] = xr_session.create_reference_space(XR_REFERENCE_SPACE_TYPE_VIEW);
	spaces[size_t(xr::spaces::world)] = xr_session.create_reference_space(XR_REFERENCE_SPACE_TYPE_STAGE);

	config.emplace(xr_system_id);

	// HTC face tracker fails if created later
	// we can destroy it right away, it actually stores static handles
	if (xr_system_id.face_tracker_supported() == xr::face_tracker_type::htc)
	{
		auto props = xr_system_id.htc_face_tracking_properties();
		xr::htc_face_tracker(xr_instance, xr_session, props.supportEyeFacialTracking, props.supportLipFacialTracking);
	}

	{
		auto spaces = xr_session.get_reference_spaces();
		spdlog::info("{} reference spaces", spaces.size());
		for (XrReferenceSpaceType i: spaces)
			spdlog::info("    {}", xr::to_string(i));
	}

	vk::CommandPoolCreateInfo cmdpool_create_info;
	cmdpool_create_info.queueFamilyIndex = vk_queue_family_index;
	cmdpool_create_info.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	vk_cmdpool = vk::raii::CommandPool{vk_device, cmdpool_create_info};

	initialize_actions();
	load_locale();
}

void application::load_locale()
{
	gen.add_messages_domain("wivrn");
	std::locale loc = gen("");

	messages_info.encoding = "UTF-8";
	if (config->locale.empty())
	{
#ifdef __ANDROID__
		jni::klass java_util_Locale("java/util/Locale");
		auto default_locale = java_util_Locale.call<jni::object<"java/util/Locale">>("getDefault");

		// if (auto language = default_locale.call<jni::string>("toString"))
		if (auto language = default_locale.call<jni::string>("getLanguage"))
			messages_info.language = language;

		if (auto country = default_locale.call<jni::string>("getCountry"))
			messages_info.country = country;
#else
		auto & facet = std::use_facet<boost::locale::info>(loc);
		messages_info.language = facet.language();
		messages_info.country = facet.country();
#endif
	}
	else
	{
		auto pos = config->locale.find("_");
		messages_info.language = config->locale.substr(0, pos);
		if (pos != std::string::npos)
			messages_info.country = config->locale.substr(pos + 1);
	}

	spdlog::info("Current locale: language {}, country {}, encoding {}", messages_info.language, messages_info.country, messages_info.encoding);

	messages_info.paths.push_back("locale");

	messages_info.domains.push_back(boost::locale::gnu_gettext::messages_info::domain("wivrn"));
	messages_info.callback = open_locale_file;
	loc = std::locale(loc, boost::locale::gnu_gettext::create_messages_facet<char>(messages_info));

	std::locale::global(loc);
}

std::pair<XrAction, XrActionType> application::get_action(std::string_view requested_name)
{
	for (const auto & [action, type, name]: instance().actions)
	{
		if (name == requested_name)
			return {action, type};
	}

	return {};
}

#ifdef __ANDROID__
extern "C" __attribute__((visibility("default"))) void Java_org_meumeu_wivrn_MainActivity_onNewIntent(JNIEnv * env, jobject instance, jobject intent_obj)
{
	jni::jni_thread::setup_thread(env);
	jni::object<"android/content/Intent"> intent{intent_obj};

	if (auto data_string = intent.call<jni::string>("getDataString"))
	{
		spdlog::info("Received intent {}", (std::string)data_string);
		application::instance().set_server_uri(data_string);
	}
}
#endif

static bool is_tcp_scheme(boost::core::string_view scheme)
{
	if (scheme.empty() or scheme == "wivrn")
		return false;
	if (scheme == "wivrn+tcp")
		return true;
	throw std::runtime_error("invalid URI scheme " + std::string(scheme));
}

void application::set_server_uri(std::string uri)
{
	auto parse_result = boost::urls::parse_uri(uri);
	if (parse_result.has_error())
		throw boost::system::system_error(parse_result.error(), "failed to parse uri");
	if (not parse_result.has_value())
		throw std::runtime_error("failed to parse uri, empty result");

	{
		std::unique_lock _{server_intent_mutex};
		server_intent =
		        wivrn_discover::service{
		                .name = "",
		                .hostname = parse_result->host(),
		                .port = parse_result->has_port() ? parse_result->port_number() : wivrn::default_port,
		                .tcp_only = is_tcp_scheme(parse_result->scheme()),
		                .pin = parse_result->password(),
		        };
	}
}

application::application(application_info info) :
        app_info(std::move(info))

{
#ifdef __ANDROID__
	// https://docs.oracle.com/javase/7/docs/technotes/guides/jni/spec/types.html

	setup_jni();
	{
		jni::object<""> act(app_info.native_app->activity->clazz);
		auto app = act.call<jni::object<"android/app/Application">>("getApplication");
		auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");

		// Get the intent, to handle wivrn://uri
		auto intent = act.call<jni::object<"android/content/Intent">>("getIntent");
		if (auto data_string = intent.call<jni::string>("getDataString"))
		{
			spdlog::info("Started with intent {}", (std::string)data_string);
			try
			{
				set_server_uri(data_string);
			}
			catch (std::exception & e)
			{
				spdlog::warn("failed to set server uri: {}", e.what());
			}
		}

		auto files_dir = ctx.call<jni::object<"java/io/File">>("getFilesDir");
		if (auto files_dir_path = files_dir.call<jni::string>("getAbsolutePath"))
		{
			config_path = files_dir_path;
			cache_path = files_dir_path;
		}
	}

	app_info.native_app->userData = this;
	app_info.native_app->onAppCmd = [](android_app * app, int32_t cmd) {
		switch (cmd)
		{
			// There is no APP_CMD_CREATE. The ANativeActivity creates the
			// application thread from onCreate(). The application thread
			// then calls android_main().
			case APP_CMD_START:
				break;
			case APP_CMD_RESUME:
				static_cast<application *>(app->userData)->resumed = true;
				break;
			case APP_CMD_PAUSE:
				static_cast<application *>(app->userData)->resumed = false;
				break;
			case APP_CMD_STOP:
				break;
			case APP_CMD_DESTROY:
				static_cast<application *>(app->userData)->native_window = nullptr;
				break;
			case APP_CMD_INIT_WINDOW:
				static_cast<application *>(app->userData)->native_window = app->window;
				break;
			case APP_CMD_TERM_WINDOW:
				static_cast<application *>(app->userData)->native_window = nullptr;
				break;
		}
	};

	// capture pointer to receive relative mouse events
	app_info.native_app->activity->callbacks->onWindowFocusChanged = [](ANativeActivity * activity, int has_focus) {
		if (has_focus)
			android_hid::request_pointer_capture(activity);
		else
			android_hid::release_pointer_capture(activity);
	};

	app_info.native_app->onInputEvent = [](android_app * app, AInputEvent * event) {
		auto app_instance = static_cast<application *>(app->userData);

		std::unique_lock _{app_instance->scene_stack_lock};
		if (!app_instance->scene_stack.empty())
		{
			auto scene = app_instance->scene_stack.back();
			return app_instance->input_handler.handle_input(scene.get(), event) ? 1 : 0;
		}
		return 0;
	};

	wifi = wifi_lock::make_wifi_lock(app_info.native_app->activity->clazz);

	// Initialize the loader for this platform
	PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
	if (XR_SUCCEEDED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction *)(&initializeLoader))))
	{
		XrLoaderInitInfoAndroidKHR loaderInitInfoAndroid{
		        .type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
		        .next = nullptr,
		        .applicationVM = app_info.native_app->activity->vm,
		        .applicationContext = app_info.native_app->activity->clazz,
		};
		initializeLoader((const XrLoaderInitInfoBaseHeaderKHR *)&loaderInitInfoAndroid);
	}

#else
	wifi = std::make_shared<wifi_lock>();
	config_path = xdg_config_home() / "wivrn";
	cache_path = xdg_cache_home() / "wivrn";
#endif

	std::filesystem::create_directories(config_path);
	std::filesystem::create_directories(cache_path);
	spdlog::debug("Config path: {}", config_path.native());
	spdlog::debug("Cache path: {}", cache_path.native());

	try
	{
		initialize();
	}
	catch (std::exception & e)
	{
		spdlog::error("Error during initialization: {}", e.what());
		cleanup();
		throw;
	}
}

#ifdef __ANDROID__
void application::setup_jni()
{
	jni::jni_thread::setup_thread(app_info.native_app->activity->vm);
}
#endif

void application::cleanup()
{
	// Remove all scenes before destroying the allocator
	scene_stack.clear();

	// Empty the meta objects while the OpenXR instance still exists
	for (auto i: scene::scene_registry)
	{
		xr::actionset tmp = std::move(i->actionset);
		i->actions_by_name.clear(); // Not strictly necessary
		i->spaces_by_name.clear();
	}

	wifi.reset();

#ifdef __ANDROID__
	jni::jni_thread::detach();
#endif
}

application::~application()
{
	auto pipeline_cache_bytes = pipeline_cache.getData();
	utils::write_whole_file(cache_path / "pipeline_cache", pipeline_cache_bytes);

	cleanup();
}

void application::loop()
{
	poll_events();

	auto scene = current_scene();
	if (not is_session_running())
	{
		if (not timestamp_unsynchronized)
			timestamp_unsynchronized = std::chrono::steady_clock::now();

		if (scene and std::chrono::steady_clock::now() - *timestamp_unsynchronized > 3s)
			scene->set_focused(false);

		// Throttle loop since xrWaitFrame won't be called.
		std::this_thread::sleep_for(250ms);
	}
	else
	{
		timestamp_unsynchronized.reset();

		if (scene)
		{
			poll_actions();
			if (auto tmp = last_scene.lock(); scene != tmp)
			{
				if (tmp)
					tmp->set_focused(false);

				last_scene = scene;
			}
			scene->set_focused(true);

			XrFrameState framestate = xr_session.wait_frame();

			auto t1 = std::chrono::steady_clock::now();

			scene->render(framestate);

			last_scene_cpu_time = std::chrono::steady_clock::now() - t1;
		}
		else
		{
			spdlog::info("Last scene was popped");
			exit_requested = true;
		}
	}
}

#ifdef __ANDROID__
void application::run()
{
	auto application_thread = utils::named_thread("application_thread", [&]() {
		setup_jni();

		while (!is_exit_requested())
		{
			try
			{
				loop();
			}
			catch (std::exception & e)
			{
				spdlog::error("Caught exception in application_thread: \"{}\"", e.what());
				exit_requested = true;
			}
			catch (...)
			{
				spdlog::error("Caught unknown exception in application_thread");
				exit_requested = true;
			}
		}
		spdlog::info("Exiting application_thread");
	});

	// Read all pending events.
	while (!exit_requested)
	{
		int events;
		struct android_poll_source * source;

		// TODO signal with a file descriptor instead of a 100ms timeout
		while (ALooper_pollOnce(100, nullptr, &events, (void **)&source) >= 0)
		{
			// Process this event.
			if (source != nullptr)
				source->process(app_info.native_app, source);
		}

		if (app_info.native_app->destroyRequested)
		{
			spdlog::info("app_info.native_app->destroyRequested is true");
			exit_requested = true;
		}
	}

	spdlog::info("Exiting normally");

	application_thread.join();
}
#else
void application::run()
{
	struct sigaction act{};
	act.sa_handler = [](int) {
		instance().exit_requested = true;
	};
	sigaction(SIGINT, &act, nullptr);

	while (!is_exit_requested())
	{
		loop();
	}
}
#endif

std::shared_ptr<scene> application::current_scene()
{
	std::unique_lock _{instance().scene_stack_lock};
	if (!instance().scene_stack.empty())
		return instance().scene_stack.back();
	else
		return {};
}

void application::pop_scene()
{
	std::unique_lock _{instance().scene_stack_lock};
	if (!instance().scene_stack.empty())
		instance().scene_stack.pop_back();
}

void application::push_scene(std::shared_ptr<scene> s)
{
	std::unique_lock _{instance().scene_stack_lock};
	instance().scene_stack.push_back(std::move(s));
}

void application::poll_actions()
{
	std::array<XrActionSet, 2> action_sets{
	        instance().xr_actionset,
	        instance().current_scene()->current_meta.actionset};

	instance().xr_session.sync_actions(action_sets);
}

std::optional<std::pair<XrTime, bool>> application::read_action_bool(XrAction action)
{
	if (!is_focused())
		return {};

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
	CHECK_XR(xrGetActionStateBoolean(instance().xr_session, &get_info, &state));

	if (!state.isActive)
		return {};

	return std::make_pair(state.lastChangeTime, state.currentState);
}

std::optional<std::pair<XrTime, float>> application::read_action_float(XrAction action)
{
	if (!is_focused())
		return {};

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
	CHECK_XR(xrGetActionStateFloat(instance().xr_session, &get_info, &state));

	if (!state.isActive)
		return {};

	return std::make_pair(state.lastChangeTime, state.currentState);
}

std::optional<std::pair<XrTime, XrVector2f>> application::read_action_vec2(XrAction action)
{
	if (!is_focused())
		return {};

	XrActionStateGetInfo get_info{
	        .type = XR_TYPE_ACTION_STATE_GET_INFO,
	        .action = action,
	};

	XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
	CHECK_XR(xrGetActionStateVector2f(instance().xr_session, &get_info, &state));

	if (!state.isActive)
		return {};

	return std::make_pair(state.lastChangeTime, state.currentState);
}

void application::haptic_start(XrAction action, XrPath subpath, int64_t duration, float frequency, float amplitude)
{
	if (!is_focused())
		return;

	XrHapticActionInfo action_info{
	        .type = XR_TYPE_HAPTIC_ACTION_INFO,
	        .action = action,
	        .subactionPath = subpath,
	};

	XrHapticVibration vibration{
	        .type = XR_TYPE_HAPTIC_VIBRATION,
	        .duration = duration,
	        .frequency = frequency,
	        .amplitude = amplitude};

	xrApplyHapticFeedback(application::instance().xr_session, &action_info, (XrHapticBaseHeader *)&vibration);
}

void application::haptic_stop(XrAction action, XrPath subpath)
{
	if (!is_focused())
		return;

	XrHapticActionInfo action_info{
	        .type = XR_TYPE_HAPTIC_ACTION_INFO,
	        .action = action,
	        .subactionPath = subpath,
	};

	xrStopHapticFeedback(application::instance().xr_session, &action_info);
}

void application::session_state_changed(XrSessionState new_state, XrTime timestamp)
{
	// See HandleSessionStateChangedEvent
	spdlog::info("Session state changed at timestamp {}: {} => {}", timestamp, xr::to_string(session_state), xr::to_string(new_state));
	session_state = new_state;

	switch (new_state)
	{
		case XR_SESSION_STATE_READY:
			xr_session.begin_session(app_info.viewconfig);
			session_running = true;
			break;

		case XR_SESSION_STATE_SYNCHRONIZED:
			session_visible = false;
			session_focused = false;
			break;

		case XR_SESSION_STATE_VISIBLE:
			session_visible = true;
			session_focused = false;
			break;

		case XR_SESSION_STATE_FOCUSED:
			session_visible = true;
			session_focused = true;
			break;

		case XR_SESSION_STATE_STOPPING:
			session_visible = false;
			session_focused = false;
			xr_session.end_session();
			session_running = false;
			break;

		case XR_SESSION_STATE_EXITING:
			exit_requested = true;
			break;

		case XR_SESSION_STATE_LOSS_PENDING:
			exit_requested = true;
			break;
		default:
			break;
	}
}

void application::poll_events()
{
	xr::event e;
	while (xr_instance.poll_event(e))
	{
		switch (e.header.type)
		{
			case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
				spdlog::info("Received XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING");
				exit_requested = true;
			}
			break;
			case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
				break;
			case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
				if (e.state_changed.session == xr_session)
					session_state_changed(e.state_changed.state, e.state_changed.time);
				else
					spdlog::error("Received XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED for unknown "
					              "session");
			}
			break;
			case XR_TYPE_EVENT_DATA_DISPLAY_REFRESH_RATE_CHANGED_FB: {
				spdlog::info("Refresh rate changed from {} to {}",
				             e.refresh_rate_changed.fromDisplayRefreshRate,
				             e.refresh_rate_changed.toDisplayRefreshRate);
			}
			break;
			case XR_TYPE_EVENT_DATA_PASSTHROUGH_STATE_CHANGED_FB: {
				spdlog::info("Passthrough state changed:");
				if (e.passthrough_state_changed.flags & XR_PASSTHROUGH_STATE_CHANGED_REINIT_REQUIRED_BIT_FB)
					spdlog::info("    XR_PASSTHROUGH_STATE_CHANGED_REINIT_REQUIRED_BIT_FB");
				if (e.passthrough_state_changed.flags & XR_PASSTHROUGH_STATE_CHANGED_NON_RECOVERABLE_ERROR_BIT_FB)
					spdlog::info("    XR_PASSTHROUGH_STATE_CHANGED_NON_RECOVERABLE_ERROR_BIT_FB");
				if (e.passthrough_state_changed.flags & XR_PASSTHROUGH_STATE_CHANGED_RECOVERABLE_ERROR_BIT_FB)
					spdlog::info("    XR_PASSTHROUGH_STATE_CHANGED_RECOVERABLE_ERROR_BIT_FB");
				if (e.passthrough_state_changed.flags & XR_PASSTHROUGH_STATE_CHANGED_RESTORED_ERROR_BIT_FB)
					spdlog::info("    XR_PASSTHROUGH_STATE_CHANGED_RESTORED_ERROR_BIT_FB");
			}
			break;
			default:
				spdlog::info("Received event type {}", xr::to_string(e.header.type));
				break;
		}
		if (std::shared_ptr<scene> s = current_scene())
			s->on_xr_event(e);
	}
}
