/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "input_profile.h"

#include "application.h"
#include "hardware.h"
#include "render/scene_components.h"
#include "utils/mapped_file.h"
#include "xr/space.h"
#include <entt/entt.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <magic_enum.hpp>
#include <simdjson.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>

namespace components
{
namespace details
{
struct node_state_transform
{
	glm::vec3 position;
	glm::quat orientation;
};

struct node_state_visibility
{
};

using node_state = std::variant<std::pair<node_state_transform, node_state_transform>, node_state_visibility>;
} // namespace details

// Component added to nodes affected by actions
struct visual_response
{
	XrAction action;
	XrActionType type;
	int axis; // Only if type is XR_ACTION_TYPE_VECTOR2F_INPUT
	float bias;
	float scale;

	details::node_state state;
};
} // namespace components

namespace
{

// Component added to nodes bound to an action space
struct bound_space
{
	xr::spaces space;
};

// clang-format off
const std::unordered_map<std::string, std::string> input_mappings = {
	{"xr-standard-squeeze"   , "/input/squeeze/value"    },
	{"xr-standard-trigger"   , "/input/trigger/value"    },
	{"xr-standard-touchpad"  , "/input/trackpad"         },
	{"xr-standard-thumbstick", "/input/thumbstick"       },
	{"a-button"              , "/input/a/click"          },
	{"b-button"              , "/input/b/click"          },
	{"x-button"              , "/input/x/click"          },
	{"y-button"              , "/input/y/click"          },
	{"thumbrest"             , "/input/thumbrest/touch"  },
};
// clang-format on

enum class component_type
{
	trigger,
	squeeze,
	button,
	thumbstick,
	touchpad
};

component_type parse_component_type(std::string_view type)
{
	if (type == "trigger")
		return component_type::trigger;

	if (type == "squeeze")
		return component_type::squeeze;

	if (type == "button")
		return component_type::button;

	if (type == "thumbstick")
		return component_type::thumbstick;

	if (type == "touchpad")
		return component_type::touchpad;

	throw std::runtime_error("Invalid type: " + std::string(type));
}

enum class component_property
{
	x_axis,
	y_axis,
	button,
	state, // ???
};

component_property parse_component_property(std::string_view property)
{
	if (property == "xAxis")
		return component_property::x_axis;

	if (property == "yAxis")
		return component_property::y_axis;

	if (property == "button")
		return component_property::button;

	if (property == "state")
		return component_property::state;

	throw std::runtime_error("Invalid property: " + std::string(property));
}

enum class value_node_property
{
	transform,
	visibility
};

value_node_property parse_value_node_property(std::string_view property)
{
	if (property == "transform")
		return value_node_property::transform;

	if (property == "visibility")
		return value_node_property::visibility;

	throw std::runtime_error("Invalid property: " + std::string(property));
}

struct json_visual_response
{
	// From the WebXR profile
	std::string layout;
	std::string component_id;
	component_type type;
	component_property property;
	std::string target_node;
	components::details::node_state state;

	// OpenXR component path
	std::string component_subpath;
};
} // namespace

input_profile::input_profile(scene & scene, const std::filesystem::path & json_profile, uint32_t layer_mask_controller, uint32_t layer_mask_ray)
{
	utils::mapped_file json{json_profile};

	simdjson::dom::parser parser;
	simdjson::dom::element root = parser.parse(reinterpret_cast<const char *>(json.data()), json.size());

	id = std::string(root["profileId"]);

	std::vector<std::tuple<std::string, entt::entity, std::shared_ptr<entt::registry>>> models;
	std::vector<json_visual_response> json_responses;

	// Fill models with the different models to be loaded and their name ("left" or "right")
	// Don't load them into the scene right now in case there is an exception
	for (simdjson::dom::key_value_pair layout: simdjson::dom::object(root["layouts"]))
	{
		// TODO handle case where i.key is none and there is no left or right
		// std::string user_path = "/user/hand/" + std::string(layout.key);
		std::filesystem::path asset_path = json_profile.parent_path() / std::string(layout.value["assetPath"]);

		std::shared_ptr<entt::registry> data = std::get<2>(models.emplace_back(layout.key, entt::null, scene.load_gltf(asset_path)));

		for (simdjson::dom::key_value_pair component: simdjson::dom::object(layout.value["components"]))
		{
			auto it = input_mappings.find(std::string(component.key));
			if (it == input_mappings.end())
			{
				spdlog::debug("Unknown component: {}", component.key);
				continue;
			}

			std::string component_subpath = it->second;
			component_type type = parse_component_type(component.value["type"]);

			for (simdjson::dom::key_value_pair response: simdjson::dom::object(component.value["visualResponses"]))
			{
				json_visual_response & vr = json_responses.emplace_back();

				vr.layout = layout.key;
				vr.component_id = component.key;
				vr.type = type;
				vr.property = parse_component_property(response.value["componentProperty"]);
				vr.target_node = std::string(response.value["valueNodeName"]);
				vr.component_subpath = component_subpath;

				// Check if the node exists
				find_node_by_name(*data, vr.target_node);

				auto node_property = parse_value_node_property(response.value["valueNodeProperty"]);
				switch (node_property)
				{
					case value_node_property::transform: {
						auto min_node = data->get<components::node>(find_node_by_name(*data, std::string(response.value["minNodeName"])));
						auto max_node = data->get<components::node>(find_node_by_name(*data, std::string(response.value["maxNodeName"])));

						components::details::node_state_transform min{
						        min_node.position,
						        min_node.orientation};

						components::details::node_state_transform max{
						        max_node.position,
						        max_node.orientation};

						vr.state = std::make_pair(min, max);
					}
					break;

					case value_node_property::visibility:
						vr.state = components::details::node_state_visibility{};
						break;
				}
			}
		}
	}

	// Add all models in the scene
	for (auto & [layout, entity, model]: models)
	{
		xr::spaces space;
		if (layout == "left")
			space = xr::spaces::grip_left;
		else if (layout == "right")
			space = xr::spaces::grip_right;
		else if (layout == "left_aim")
			space = xr::spaces::aim_left;
		else if (layout == "right_aim")
			space = xr::spaces::aim_right;
		else
			continue;

		auto [e, node] = scene.add_gltf(model, layer_mask_controller);
		node.name = layout;
		entity = e;
		scene.world.emplace<bound_space>(e, space);

		spdlog::debug("Created entity {}", layout);
	}

	for (simdjson::dom::key_value_pair layout: simdjson::dom::object(root["layouts"]))
	{
		xr::spaces space;
		if (layout.key == "left")
			space = xr::spaces::aim_left;
		else if (layout.key == "right")
			space = xr::spaces::aim_right;
		else
			continue;

		auto && [entity, node] = scene.add_gltf(controller_ray_model_name(), layer_mask_ray);
		node.name = (std::string)layout.key + "_ray";
		spdlog::debug("Created entity {}", node.name);

		scene.world.emplace<bound_space>(entity, space);

		if (layout.key == "left")
			left_ray = entity;
		else if (layout.key == "right")
			right_ray = entity;
	}

	for (auto & json_response: json_responses)
	{
		std::string action_path = "/user/hand/" + json_response.layout + json_response.component_subpath;

		if (json_response.type == component_type::thumbstick && json_response.property == component_property::button)
			action_path += "/click";

		auto action = application::get_action(action_path);
		if (action.first == XR_NULL_PATH)
		{
			spdlog::debug("No input for {}/{} ({})", json_response.layout, json_response.component_id, action_path);
			continue;
		}

		components::visual_response response;
		response.action = action.first;
		response.type = action.second;

		switch (json_response.property)
		{
			case component_property::button:
			case component_property::state:
				response.axis = -1;
				response.bias = 0;
				response.scale = 1;
				break;
			case component_property::x_axis:
				response.axis = 0;
				response.bias = 0.5;
				response.scale = 0.5;
				break;
			case component_property::y_axis:
				// Y axis is reversed in WebXR (min up, max down, https://github.com/immersive-web/webxr-input-profiles/blob/main/packages/assets/tutorial/README.md#buttons) and OpenXR (-1 down, +1 up, §6.3.2, Standard components)
				response.axis = 1;
				response.bias = 0.5;
				response.scale = -0.5;
				break;
		}

		entt::entity controller_entity = entt::null;
		for (const auto & [layout, entity, model]: models)
		{
			if (layout == json_response.layout)
			{
				controller_entity = entity;
				break;
			}
		}

		response.state = json_response.state;

		auto target = find_node_by_name(scene.world, json_response.target_node, controller_entity);
		scene.world.emplace<components::visual_response>(target, response);
	}

	offset.fill({{0, 0, 0}, {1, 0, 0, 0}});
}

static void apply_visual_response(components::node & node, std::pair<components::details::node_state_transform, components::details::node_state_transform> transforms, float value)
{
	node.position = glm::mix(transforms.first.position, transforms.second.position, value);
	node.orientation = glm::slerp(transforms.first.orientation, transforms.second.orientation, value);
}

static void apply_visual_response(components::node & node, components::details::node_state_visibility, float value)
{
	node.visible = value > 0.5;
}

static void set_clipping_planes(entt::registry & scene, entt::entity entity, std::span<glm::vec4> clipping_planes)
{
	// If the ray starts on the wrong side of the GUI, hide it entirely
	// This assumes the node is a child of the root node
	components::node & node = scene.get<components::node>(entity);
	for (glm::vec4 & plane: clipping_planes)
	{
		if (glm::dot(plane, glm::vec4(node.position, 1)) < 0)
		{
			scene.get<components::node>(entity).visible = false;
			return;
		}
	}

	// Apply the clipping planes on direct children of the target entity
	for (auto && [child_entity, child_node]: scene.view<components::node>().each())
	{
		if (child_node.parent == entity)
		{
			size_t nb_clipping_planes = std::min(child_node.clipping_planes.size(), clipping_planes.size());

			auto copy_results = std::ranges::copy_n(
			        clipping_planes.begin(),
			        nb_clipping_planes,
			        child_node.clipping_planes.begin());

			// Disable the remaining clipping planes
			std::ranges::fill_n(
			        copy_results.out,
			        child_node.clipping_planes.size() - nb_clipping_planes,
			        glm::vec4(0, 0, 0, 1));
		}
	}
}

void input_profile::apply(
        entt::registry & scene,
        XrSpace world_space,
        XrTime predicted_display_time,
        bool hide_left_controller,
        bool hide_left_ray,
        bool hide_right_controller,
        bool hide_right_ray,
        std::span<glm::vec4> pointer_limits)
{
	for (auto && [entity, node, space]: scene.view<components::node, bound_space>().each())
	{
		if ((space.space == xr::spaces::grip_left and hide_left_controller) or
		    (space.space == xr::spaces::aim_left and hide_left_ray) or
		    (space.space == xr::spaces::grip_right and hide_right_controller) or
		    (space.space == xr::spaces::aim_right and hide_right_ray))
		{
			node.visible = false;
		}
		else if (auto location = application::locate_controller(application::space(space.space), world_space, predicted_display_time); location)
		{
			node.visible = true;

			assert((int)space.space >= 0);
			assert((int)space.space < offset.size());
			auto & [p, q] = offset[space.space];

			node.position = location->first + glm::mat3_cast(location->second * q) * p;
			node.orientation = location->second * q;
		}
		else
		{
			node.visible = false;
		}
	}

	for (auto && [entity, node, response]: scene.view<components::node, components::visual_response>().each())
	{
		assert(response.action);

		std::optional<float> value;

		switch (response.type)
		{
			case XR_ACTION_TYPE_BOOLEAN_INPUT: {
				auto opt_value = application::read_action_bool(response.action);
				if (opt_value)
					value = opt_value->second;
				break;
			}

			case XR_ACTION_TYPE_FLOAT_INPUT: {
				auto opt_value = application::read_action_float(response.action);
				if (opt_value)
					value = opt_value->second;
				break;
			}

			case XR_ACTION_TYPE_VECTOR2F_INPUT: {
				auto opt_value = application::read_action_vec2(response.action);
				if (opt_value)
				{
					switch (response.axis)
					{
						case 0:
							value = opt_value->second.x;
							break;
						case 1:
							value = opt_value->second.y;
							break;
					}
				}
				break;
			}

			case XR_ACTION_TYPE_POSE_INPUT:
			case XR_ACTION_TYPE_VIBRATION_OUTPUT:
			case XR_ACTION_TYPE_MAX_ENUM:
				assert(false);
				break;
		}

		if (value)
		{
			float scaled_value = *value * response.scale + response.bias;

			if ((scaled_value < 0) || (scaled_value > 1))
			{
				spdlog::warn("Out of range value {} (scaled to {}) for node {}", *value, scaled_value, get_node_name(scene, entity));
			}

			std::visit([&](auto & transform) {
				apply_visual_response(node, transform, scaled_value);
			},
			           response.state);
		}
	}

	set_clipping_planes(scene, left_ray, pointer_limits);
	set_clipping_planes(scene, right_ray, pointer_limits);
}
