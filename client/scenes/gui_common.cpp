/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2026  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022-2026  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "gui_common.h"

#include "application.h"
#include "configuration.h"
#include "constants.h"
#include "render/imgui_impl.h"
#include "utils/i18n.h"

#include "wivrn_packets.h"
#include "xr/instance.h"
#include "xr/session.h"
#include "xr/system.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace wivrn::gui
{

std::string rate_label(float rate, uint32_t divider)
{
	if (rate == 0)
		return _C("automatic refresh rate", "Automatic");
	if (divider == 1)
		return fmt::format("{}", rate);

	return fmt::format(_cF("refresh rate selection, with reprojection", "{} ({} with space warp)"), rate / divider, rate);
}

bool refresh_rate(
        xr::instance & instance,
        xr::session & session,
        imgui_context & imgui_ctx,
        configuration & config)
{
	if (not instance.has_extension(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
		return false;

	const auto & refresh_rates = session.get_refresh_rates();
	if (refresh_rates.empty())
		return false;

	bool changed = false;
	if (ImGui::BeginCombo(
	            _S("Refresh rate"),
	            rate_label(config.preferred_refresh_rate, config.fps_divider).c_str()))
	{
		if (ImGui::Selectable(
		            _cS("automatic refresh rate", "Automatic"),
		            config.preferred_refresh_rate == 0,
		            ImGuiSelectableFlags_SelectOnRelease))
		{
			config.preferred_refresh_rate = 0;
			config.fps_divider = 1;
			config.save();
			changed = true;
		}
		if (ImGui::IsItemHovered())
			imgui_ctx.tooltip(_("Select refresh rate based on measured application performance.\n"
			                    "May cause flicker when a change happens."));

		for (uint32_t divider: {1, 2})
		{
			for (float rate: refresh_rates)
			{
				if (ImGui::Selectable(
				            rate_label(rate, divider).c_str(),
				            rate == config.preferred_refresh_rate and divider == config.fps_divider,
				            ImGuiSelectableFlags_SelectOnRelease))
				{
					spdlog::info("configured refresh rate: {}", rate);
					session.set_refresh_rate(rate);
					config.preferred_refresh_rate = rate;
					config.fps_divider = divider;
					config.save();
					changed = true;
				}
			}
		}
		ImGui::EndCombo();
	}
	imgui_ctx.vibrate_on_hover();

	if (config.preferred_refresh_rate == 0 and refresh_rates.size() > 2)
	{
		float min_rate = config.minimum_refresh_rate.value_or(refresh_rates.front());
		if (ImGui::BeginCombo(_S("Minimum refresh rate"), fmt::format("{}", min_rate).c_str()))
		{
			for (float rate: refresh_rates | std::views::take(refresh_rates.size() - 1))
			{
				if (ImGui::Selectable(fmt::format("{}", rate).c_str(), rate == config.minimum_refresh_rate, ImGuiSelectableFlags_SelectOnRelease))
				{
					config.minimum_refresh_rate = rate;
					config.save();
				}
			}
			ImGui::EndCombo();
		}
		imgui_ctx.vibrate_on_hover();
	}
	return changed;
}

bool body_tracking_parts(
        xr::system & system,
        imgui_context & imgui_ctx,
        configuration & config)
{
	auto body_tracker = system.body_tracker_supported();
	if (body_tracker == xr::body_tracker_type::none or body_tracker == xr::body_tracker_type::htc)
		return false;

	const std::array body_parts{
	        std::make_pair(from_headset::body_part_mask::chest, _C("virtual body tracker selection", "Chest")),
	        std::make_pair(from_headset::body_part_mask::left_elbow, _C("virtual body tracker selection", "Left elbow")),
	        std::make_pair(from_headset::body_part_mask::right_elbow, _C("virtual body tracker selection", "Right elbow")),
	        std::make_pair(from_headset::body_part_mask::hip, _C("virtual body tracker selection", "Hip")),
	        std::make_pair(from_headset::body_part_mask::left_knee, _C("virtual body tracker selection", "Left knee")),
	        std::make_pair(from_headset::body_part_mask::right_knee, _C("virtual body tracker selection", "Right knee")),
	        std::make_pair(from_headset::body_part_mask::left_foot, _C("virtual body tracker selection", "Left foot")),
	        std::make_pair(from_headset::body_part_mask::right_foot, _C("virtual body tracker selection", "Right foot")),
	};

	bool changed = false;
	bool button_pressed = ImGui::ArrowButton("##OpenBodyPartMenu", ImGuiDir_Down);
	ImGui::SameLine(0.f, ImGui::GetStyle().ItemInnerSpacing.x);
	ImGui::Text("%s", _S("Virtual body trackers"));

	if (button_pressed)
		ImGui::OpenPopup("body part menu");

	const auto & popup_layer = imgui_ctx.layers()[1];
	const glm::vec2 popup_layer_center = popup_layer.vp_origin + popup_layer.vp_size / 2;
	ImGui::SetNextWindowPos({popup_layer_center.x, popup_layer_center.y}, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, constants::style::window_padding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, constants::style::window_rounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, constants::style::window_border_size);

	if (ImGui::BeginPopupModal("body part menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize))
	{
		const float height = ImGui::GetFrameHeight();
		const ImVec2 size(height, height);

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 0.40f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 1.00f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.00f));
		ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - size.x);
		if (ImGui::Button("X", size))
			ImGui::CloseCurrentPopup();
		imgui_ctx.vibrate_on_hover();
		ImGui::PopStyleColor(3); // ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive

		for (const auto & [bit, name]: body_parts)
		{
			if (body_tracker == xr::body_tracker_type::fb and bit > from_headset::body_part_mask::hip)
				continue;

			bool enabled = config.body_part_mask & bit;
			if (ImGui::Checkbox(name.c_str(), &enabled))
			{
				if (enabled)
				{
					config.body_part_mask |= bit;
				}
				else
				{
					config.body_part_mask &= ~bit;
				}
				config.save();
				changed = true;
				imgui_ctx.vibrate_on_hover();
			}
		}

		ImGui::EndPopup();
	}
	ImGui::PopStyleVar(3); // ImGuiStyleVar_WindowPadding, ImGuiStyleVar_WindowRounding, ImGuiStyleVar_WindowBorderSize

	return changed;
}

static std::string openxr_post_processing_flag_name(XrCompositionLayerSettingsFlagsFB flag)
{
	switch (flag)
	{
		case XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SUPER_SAMPLING_BIT_FB:
		case XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SHARPENING_BIT_FB:
			return _C("openxr_post_processing", "Normal");
		case XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB:
		case XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB:
			return _C("openxr_post_processing", "Quality");
		default:
			return _C("openxr_post_processing", "Disabled");
	}
}

bool post_processing(
        imgui_context & imgui_ctx,
        configuration & config)
{
	bool changed = false;
	if (not application::get_openxr_post_processing_supported())
		return changed;

	ImGui::Text("%s", _S("OpenXR post-processing"));
	ImGui::Indent();

	{
		XrCompositionLayerSettingsFlagsFB current = config.openxr_post_processing.super_sampling;
		if (ImGui::BeginCombo(_S("Supersampling"), openxr_post_processing_flag_name(current).c_str()))
		{
			const XrCompositionLayerSettingsFlagsFB selectable_options[]{
			        0,
			        XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SUPER_SAMPLING_BIT_FB,
			        XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SUPER_SAMPLING_BIT_FB};
			for (XrCompositionLayerSettingsFlagsFB option: selectable_options)
			{
				if (ImGui::Selectable(openxr_post_processing_flag_name(option).c_str(), current == option, ImGuiSelectableFlags_SelectOnRelease))
				{
					spdlog::info("Setting OpenXR super sampling to {}", openxr_post_processing_flag_name(option));
					config.openxr_post_processing.super_sampling = option;
					config.save();
					changed = true;
				}
				imgui_ctx.vibrate_on_hover();
			}
			ImGui::EndCombo();
		}
		imgui_ctx.vibrate_on_hover();
		if (ImGui::IsItemHovered())
			imgui_ctx.tooltip(_("Reduce flicker for high contrast edges.\nUseful when the input resolution is high compared to the headset display"));
	}

	{
		XrCompositionLayerSettingsFlagsFB current = config.openxr_post_processing.sharpening;
		if (ImGui::BeginCombo(_S("Sharpening"), openxr_post_processing_flag_name(current).c_str()))
		{
			const XrCompositionLayerSettingsFlagsFB selectable_options[]{
			        0,
			        XR_COMPOSITION_LAYER_SETTINGS_NORMAL_SHARPENING_BIT_FB,
			        XR_COMPOSITION_LAYER_SETTINGS_QUALITY_SHARPENING_BIT_FB};
			for (XrCompositionLayerSettingsFlagsFB option: selectable_options)
			{
				if (ImGui::Selectable(openxr_post_processing_flag_name(option).c_str(), current == option, ImGuiSelectableFlags_SelectOnRelease))
				{
					spdlog::info("Setting OpenXR sharpening to {}", openxr_post_processing_flag_name(option));
					config.openxr_post_processing.sharpening = option;
					config.save();
					changed = true;
				}
				imgui_ctx.vibrate_on_hover();
			}
			ImGui::EndCombo();
		}
		imgui_ctx.vibrate_on_hover();
		if (ImGui::IsItemHovered())
			imgui_ctx.tooltip(_("Improve clarity of high contrast edges and counteract blur.\nUseful when the input resolution is low compared to the headset display"));
	}

	ImGui::Unindent();
	return changed;
}
} // namespace wivrn::gui
