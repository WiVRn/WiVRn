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

#include "hand_model.h"
#include "openxr/openxr.h"
#include "render/gpu_buffer.h"
#include "render/scene_components.h"
#include "vk/shader.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <entt/entt.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <string_view>

namespace components
{
struct hand_joint
{
	XrHandEXT hand;
	XrHandJointEXT joint;
};

struct hand_mesh_fb_root
{
	XrHandEXT hand;
};
} // namespace components

static glm::vec3 to_vec3(const XrVector3f & value)
{
	return {value.x, value.y, value.z};
}

static glm::quat to_quat(const XrQuaternionf & value)
{
	return glm::quat(value.w, value.x, value.y, value.z);
}

static glm::mat4 to_matrix(const XrPosef & pose)
{
	return glm::translate(glm::mat4(1), to_vec3(pose.position)) * glm::mat4_cast(to_quat(pose.orientation));
}

template <typename T>
static void write_vertex_attribute(std::vector<std::byte> & buffer, size_t stride, size_t offset, size_t index, const T & value)
{
	std::memcpy(buffer.data() + index * stride + offset, &value, sizeof(value));
}

static std::string normalize_hand_mesh_fb_semantic(std::string semantic)
{
	std::transform(semantic.begin(), semantic.end(), semantic.begin(), [](unsigned char c) {
		return static_cast<char>(std::toupper(c));
	});

	if (semantic.starts_with("IN_"))
		semantic = semantic.substr(3);

	return semantic;
}

static renderer::vertex_layout create_hand_mesh_fb_layout(scene & scene)
{
	renderer::vertex_layout layout;
	auto shader = load_shader(scene.device, "lit_skinned.vert");
	for (const auto & input: shader->inputs)
	{
		std::string semantic = normalize_hand_mesh_fb_semantic(input.name);
		const uint32_t binding = semantic == "POSITION" || semantic == "JOINTS" || semantic == "WEIGHTS" ? 0 : 1;
		layout.add_vertex_attribute(semantic, input.format, binding, input.location, input.array_size);
	}
	return layout;
}

static const vk::VertexInputBindingDescription & find_hand_mesh_fb_binding(const renderer::vertex_layout & layout, uint32_t binding)
{
	for (const auto & description: layout.bindings)
		if (description.binding == binding)
			return description;

	throw std::runtime_error("Missing hand mesh FB vertex binding");
}

static const vk::VertexInputAttributeDescription & find_hand_mesh_fb_attribute(const renderer::vertex_layout & layout, std::string_view semantic)
{
	for (size_t i = 0; i < layout.attribute_names.size(); ++i)
		if (layout.attribute_names[i] == semantic)
			return layout.attributes.at(i);

	throw std::runtime_error("Missing hand mesh FB vertex attribute");
}

static std::shared_ptr<renderer::material> create_hand_mesh_fb_material(scene & scene, XrHandEXT hand)
{
	return scene.create_material([hand](renderer::material & material) {
		material.staging.base_color_factor = {0.5f, 0.5f, 0.5f, 1.0f};
		material.staging.metallic_factor = 0.0f;
		material.staging.roughness_factor = 0.5527864f;
		material.name = hand == XR_HAND_LEFT_EXT ? "left-hand-mesh-material" : "right-hand-mesh-material";
		material.double_sided = false;
		material.blend_enable = false;
	});
}

static std::shared_ptr<renderer::mesh> create_hand_mesh_fb_mesh(scene & scene, const xr::hand_tracker::mesh_data & mesh)
{
	std::vector<uint16_t> indices;
	indices.reserve(mesh.indices.size());
	for (int16_t index: mesh.indices)
		indices.push_back(static_cast<uint16_t>(index));

	auto layout = create_hand_mesh_fb_layout(scene);
	const auto & binding_0_layout = find_hand_mesh_fb_binding(layout, 0);
	const auto & binding_1_layout = find_hand_mesh_fb_binding(layout, 1);
	const size_t position_offset = find_hand_mesh_fb_attribute(layout, "POSITION").offset;
	const size_t joints_offset = find_hand_mesh_fb_attribute(layout, "JOINTS").offset;
	const size_t weights_offset = find_hand_mesh_fb_attribute(layout, "WEIGHTS").offset;
	const size_t normal_offset = find_hand_mesh_fb_attribute(layout, "NORMAL").offset;
	const size_t tangent_offset = find_hand_mesh_fb_attribute(layout, "TANGENT").offset;
	const size_t texcoord_0_offset = find_hand_mesh_fb_attribute(layout, "TEXCOORD_0").offset;
	const size_t texcoord_1_offset = find_hand_mesh_fb_attribute(layout, "TEXCOORD_1").offset;
	const size_t color_offset = find_hand_mesh_fb_attribute(layout, "COLOR").offset;
	std::vector<std::byte> binding_0(mesh.vertex_positions.size() * binding_0_layout.stride);
	std::vector<std::byte> binding_1(mesh.vertex_positions.size() * binding_1_layout.stride);

	glm::vec3 obb_min;
	glm::vec3 obb_max;
	for (size_t i = 0; i < mesh.vertex_positions.size(); ++i)
	{
		glm::vec3 position = to_vec3(mesh.vertex_positions.at(i));
		glm::vec3 normal = to_vec3(mesh.vertex_normals.at(i));
		glm::vec4 tangent{};
		glm::vec2 texcoord_0{mesh.vertex_uvs.at(i).x, mesh.vertex_uvs.at(i).y};
		glm::vec2 texcoord_1{};
		glm::vec4 color{1, 1, 1, 1};
		glm::vec4 joints{
				static_cast<float>(mesh.vertex_blend_indices.at(i).x),
				static_cast<float>(mesh.vertex_blend_indices.at(i).y),
				static_cast<float>(mesh.vertex_blend_indices.at(i).z),
				static_cast<float>(mesh.vertex_blend_indices.at(i).w)};
		glm::vec4 weights{
				mesh.vertex_blend_weights.at(i).x,
				mesh.vertex_blend_weights.at(i).y,
				mesh.vertex_blend_weights.at(i).z,
				mesh.vertex_blend_weights.at(i).w};

		if (i == 0)
			obb_min = obb_max = position;
		else
		{
			obb_min.x = std::min(obb_min.x, position.x);
			obb_min.y = std::min(obb_min.y, position.y);
			obb_min.z = std::min(obb_min.z, position.z);
			obb_max.x = std::max(obb_max.x, position.x);
			obb_max.y = std::max(obb_max.y, position.y);
			obb_max.z = std::max(obb_max.z, position.z);
		}

		write_vertex_attribute(binding_0, binding_0_layout.stride, position_offset, i, position);
		write_vertex_attribute(binding_0, binding_0_layout.stride, joints_offset, i, joints);
		write_vertex_attribute(binding_0, binding_0_layout.stride, weights_offset, i, weights);
		write_vertex_attribute(binding_1, binding_1_layout.stride, normal_offset, i, normal);
		write_vertex_attribute(binding_1, binding_1_layout.stride, tangent_offset, i, tangent);
		write_vertex_attribute(binding_1, binding_1_layout.stride, texcoord_0_offset, i, texcoord_0);
		write_vertex_attribute(binding_1, binding_1_layout.stride, texcoord_1_offset, i, texcoord_1);
		write_vertex_attribute(binding_1, binding_1_layout.stride, color_offset, i, color);
	}

	fastgltf::Asset asset;
	gpu_buffer staging_buffer(scene.physical_device, asset);
	auto hand_mesh_fb_mesh = std::make_shared<renderer::mesh>();
	auto & primitive = hand_mesh_fb_mesh->primitives.emplace_back();
	primitive.indexed = true;
	primitive.index_count = indices.size();
	primitive.vertex_count = mesh.vertex_positions.size();
	primitive.index_type = vk::IndexType::eUint16;
	primitive.index_offset = staging_buffer.add_indices(indices);
	primitive.vertex_offset.push_back(staging_buffer.add_vertices(binding_0));
	primitive.vertex_offset.push_back(staging_buffer.add_vertices(binding_1));
	primitive.layout = std::move(layout);
	primitive.obb_min = obb_min;
	primitive.obb_max = obb_max;
	primitive.vertex_shader = "lit_skinned.vert";
	primitive.cull_mode = vk::CullModeFlagBits::eBack;
	primitive.front_face = vk::FrontFace::eCounterClockwise;
	primitive.topology = vk::PrimitiveTopology::eTriangleList;
	hand_mesh_fb_mesh->buffer = std::make_shared<buffer_allocation>(staging_buffer.copy_to_gpu(scene.device));
	return hand_mesh_fb_mesh;
}

static const std::array<std::pair<XrHandJointEXT, std::string_view>, XR_HAND_JOINT_COUNT_EXT - 1 /* No palm in gltf */> joints{{
        // clang-format off
	{XR_HAND_JOINT_WRIST_EXT,               "wrist"},
	{XR_HAND_JOINT_THUMB_METACARPAL_EXT,    "thumb-metacarpal"},
	{XR_HAND_JOINT_THUMB_PROXIMAL_EXT,      "thumb-phalanx-proximal"},
	{XR_HAND_JOINT_THUMB_DISTAL_EXT,        "thumb-phalanx-distal"},
	{XR_HAND_JOINT_THUMB_TIP_EXT,           "thumb-tip"},
	{XR_HAND_JOINT_INDEX_METACARPAL_EXT,    "index-finger-metacarpal"},
	{XR_HAND_JOINT_INDEX_PROXIMAL_EXT,      "index-finger-phalanx-proximal"},
	{XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT,  "index-finger-phalanx-intermediate"},
	{XR_HAND_JOINT_INDEX_DISTAL_EXT,        "index-finger-phalanx-distal"},
	{XR_HAND_JOINT_INDEX_TIP_EXT,           "index-finger-tip"},
	{XR_HAND_JOINT_MIDDLE_METACARPAL_EXT,   "middle-finger-metacarpal"},
	{XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,     "middle-finger-phalanx-proximal"},
	{XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, "middle-finger-phalanx-intermediate"},
	{XR_HAND_JOINT_MIDDLE_DISTAL_EXT,       "middle-finger-phalanx-distal"},
	{XR_HAND_JOINT_MIDDLE_TIP_EXT,          "middle-finger-tip"},
	{XR_HAND_JOINT_RING_METACARPAL_EXT,     "ring-finger-metacarpal"},
	{XR_HAND_JOINT_RING_PROXIMAL_EXT,       "ring-finger-phalanx-proximal"},
	{XR_HAND_JOINT_RING_INTERMEDIATE_EXT,   "ring-finger-phalanx-intermediate"},
	{XR_HAND_JOINT_RING_DISTAL_EXT,         "ring-finger-phalanx-distal"},
	{XR_HAND_JOINT_RING_TIP_EXT,            "ring-finger-tip"},
	{XR_HAND_JOINT_LITTLE_METACARPAL_EXT,   "pinky-finger-metacarpal"},
	{XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,     "pinky-finger-phalanx-proximal"},
	{XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, "pinky-finger-phalanx-intermediate"},
	{XR_HAND_JOINT_LITTLE_DISTAL_EXT,       "pinky-finger-phalanx-distal"},
	{XR_HAND_JOINT_LITTLE_TIP_EXT,          "pinky-finger-tip"},
        // clang-format on
}};

void hand_model::add_hand(scene & scene,
                          XrHandEXT hand,
                          const std::filesystem::path & gltf_path,
                          uint32_t layer_mask)
{
	auto && [entity, node] = scene.add_gltf(gltf_path, layer_mask);

	node.name = gltf_path.stem();

	for (auto [joint, name]: joints)
	{
		entt::entity joint_entity = find_node_by_name(scene.world, name, entity);

		scene.world.emplace<components::hand_joint>(joint_entity, hand, joint);
	}

	// Fake joint to hide the hand when it should not be visible
	scene.world.emplace<components::hand_joint>(entity, hand, (XrHandJointEXT)-1);
}

void hand_model::add_hand(scene & scene,
                          XrHandEXT hand,
                          const xr::hand_tracker::mesh_data & mesh,
                          uint32_t layer_mask)
{
	auto material = create_hand_mesh_fb_material(scene, hand);

	entt::entity entity = scene.world.create();
	auto & node = scene.world.emplace<components::node>(entity);
	node.name = hand == XR_HAND_LEFT_EXT ? "left-hand-mesh" : "right-hand-mesh";
	node.mesh = create_hand_mesh_fb_mesh(scene, mesh);
	for (auto & primitive: node.mesh->primitives)
		primitive.material_ = material;
	node.visible = false;
	node.layer_mask = layer_mask;
	node.joints.reserve(mesh.joint_bind_poses.size());
	scene.world.emplace<components::hand_mesh_fb_root>(entity, hand);

	for (size_t i = 0; i < mesh.joint_bind_poses.size(); ++i)
	{
		entt::entity joint_entity = scene.world.create();
		auto & joint_node = scene.world.emplace<components::node>(joint_entity);
		joint_node.parent = entity;
		joint_node.name = std::string(magic_enum::enum_name(static_cast<XrHandJointEXT>(i)));
		node.joints.emplace_back(joint_entity, glm::inverse(to_matrix(mesh.joint_bind_poses[i])));
	}
}

void hand_model::apply(entt::registry & scene,
                       const std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> & left_hand,
                       const std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> & right_hand)
{
	for (auto && [entity, hj, node]: scene.view<components::hand_joint, components::node>().each())
	{
		auto f = [&](const std::optional<std::array<xr::hand_tracker::joint, XR_HAND_JOINT_COUNT_EXT>> & joints) {
			if (joints.has_value())
			{
				node.visible = true;

				if (hj.joint < 0 or hj.joint > XR_HAND_JOINT_COUNT_EXT)
					return;

				const auto & pose = (*joints)[hj.joint].first.pose;

				node.position = {
				        pose.position.x,
				        pose.position.y,
				        pose.position.z};

				node.orientation = {
				        pose.orientation.w,
				        pose.orientation.x,
				        pose.orientation.y,
				        pose.orientation.z};
			}
			else
			{
				node.visible = false;
			}
		};

		switch (hj.hand)
		{
			case XR_HAND_LEFT_EXT:
				f(left_hand);
				break;
			case XR_HAND_RIGHT_EXT:
				f(right_hand);
				break;
			default:
				break;
		}
	}

	for (auto && [entity, hand_root, node]: scene.view<components::hand_mesh_fb_root, components::node>().each())
	{
		const auto & joints = hand_root.hand == XR_HAND_LEFT_EXT ? left_hand : right_hand;
		if (!joints)
		{
			node.visible = false;
			continue;
		}

		node.visible = true;

		const auto & wrist_pose = (*joints)[XR_HAND_JOINT_WRIST_EXT].first.pose;
		glm::vec3 wrist_position = to_vec3(wrist_pose.position);
		glm::quat wrist_orientation = to_quat(wrist_pose.orientation);
		glm::quat wrist_orientation_inv = glm::conjugate(wrist_orientation);

		node.position = wrist_position;
		node.orientation = wrist_orientation;

		for (size_t i = 0; i < node.joints.size(); ++i)
		{
			auto & joint_node = scene.get<components::node>(node.joints[i].first);
			if (i == XR_HAND_JOINT_WRIST_EXT)
			{
				joint_node.position = {};
				joint_node.orientation = {1, 0, 0, 0};
				continue;
			}

			const auto & pose = (*joints)[i].first.pose;
			glm::vec3 position = to_vec3(pose.position);
			glm::quat orientation = to_quat(pose.orientation);
			joint_node.position = wrist_orientation_inv * (position - wrist_position);
			joint_node.orientation = glm::normalize(wrist_orientation_inv * orientation);
		}
	}
}
