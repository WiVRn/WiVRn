/*
 * WiVRn VR streaming
 * Copyright (C) 2022  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2022  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "scene.h"

#include "application.h"
#include "render/scene_components.h"
#include "utils/contains.h"
#include "utils/i18n.h"
#include "utils/overloaded.h"
#include "utils/ranges.h"
#include <entt/core/fwd.hpp>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan_raii.hpp>
#include <openxr/openxr.h>

// TODO remove constants
#include "constants.h"

std::vector<scene::meta *> scene::scene_registry;

scene::~scene() {}

scene::scene(key, const meta & current_meta, std::span<const vk::Format> supported_color_formats, std::span<const vk::Format> supported_depth_formats, scene * parent_scene) :
        instance(application::instance().xr_instance),
        system(application::instance().xr_system_id),
        session(application::instance().xr_session),
        viewconfig(application::instance().app_info.viewconfig),

        vk_instance(application::instance().vk_instance),
        device(application::instance().vk_device),
        physical_device(application::instance().vk_physical_device),
        queue(application::instance().vk_queue),
        queue_family_index(application::instance().vk_queue_family_index),
        commandpool(application::instance().vk_cmdpool),
        current_meta(current_meta)
{
	swapchain_format = vk::Format::eUndefined;
	spdlog::info("Supported swapchain formats:");

	for (auto format: session.get_swapchain_formats())
	{
		spdlog::info("    {}", vk::to_string(format));
	}

	for (auto format: session.get_swapchain_formats())
	{
		if (utils::contains(supported_color_formats, format))
		{
			swapchain_format = format;
			break;
		}
	}

	if (swapchain_format == vk::Format::eUndefined)
		throw std::runtime_error(_("No supported swapchain format"));

	auto views = system.view_configuration_views(viewconfig);
	depth_format = scene_renderer::find_usable_image_format(
	        physical_device,
	        supported_depth_formats,
	        {
	                views[0].recommendedImageRectWidth,
	                views[0].recommendedImageRectHeight,
	                1,
	        },
	        vk::ImageUsageFlagBits::eDepthStencilAttachment);

	composition_layer_depth_test_supported =
	        instance.has_extension(XR_KHR_COMPOSITION_LAYER_DEPTH_EXTENSION_NAME) and
	        instance.has_extension(XR_FB_COMPOSITION_LAYER_DEPTH_TEST_EXTENSION_NAME);

	composition_layer_color_scale_bias_supported = instance.has_extension(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);

	if (parent_scene)
	{
		renderer = parent_scene->renderer;
		gltf_cache = parent_scene->gltf_cache;
		image_cache = parent_scene->image_cache;
	}
	else
	{
		renderer = std::make_shared<scene_renderer>(device, physical_device, queue, queue_family_index);
		gltf_cache = std::make_shared<gltf_cache_type>(device, physical_device, queue, queue_family_index, renderer->get_default_material(), application::get_cache_path() / "textures");
		image_cache = std::make_shared<image_cache_type>(device, physical_device, queue, queue_family_index);
	}
}

void scene::set_focused(bool status)
{
	if (status != focused)
	{
		focused = status;
		if (focused)
			on_focused();
		else
			on_unfocused();
	}
}

glm::mat4 scene::projection_matrix(XrFovf fov, float zn)
{
	float r = tan(fov.angleRight);
	float l = tan(fov.angleLeft);
	float t = tan(fov.angleUp);
	float b = tan(fov.angleDown);

	// reversed Z projection, infinite far plane

	// clang-format off
	return glm::mat4{
		{ 2/(r-l),     0,            0,    0 },
		{ 0,           2/(b-t),      0,    0 },
		{ (l+r)/(r-l), (t+b)/(b-t),  0,   -1 },
		{ 0,           0,            zn,   0 }
	};
	// clang-format on
}

glm::mat4 scene::view_matrix(XrPosef pose)
{
	XrQuaternionf q = pose.orientation;
	XrVector3f pos = pose.position;

	glm::mat4 inv_view_matrix = glm::mat4_cast(glm::quat(q.w, q.x, q.y, q.z));

	inv_view_matrix = glm::translate(glm::mat4(1), glm::vec3(pos.x, pos.y, pos.z)) * inv_view_matrix;

	return glm::inverse(inv_view_matrix);
}

void scene::render_world(
        XrCompositionLayerFlags flags,
        XrSpace space,
        std::span<XrView> views,
        uint32_t width,
        uint32_t height,
        bool keep_depth_buffer,
        uint32_t layer_mask,
        XrColor4f clear_color,
        const std::optional<xr::foveation_profile> & foveation,
        bool render_debug_draws)
{
	assert(views.size() <= layer::max_views);
	beman::inplace_vector::inplace_vector<scene_renderer::frame_info, layer::max_views> frames;
	beman::inplace_vector::inplace_vector<XrCompositionLayerProjectionView, layer::max_views> composition_layer_color;
	beman::inplace_vector::inplace_vector<XrCompositionLayerDepthInfoKHR, layer::max_views> composition_layer_depth;

	auto [color_swapchain, color_image, foveation_image] = [&]() -> std::tuple<XrSwapchain, vk::Image, vk::Image> {
		xr::swapchain & color_swapchain = get_swapchain(swapchain_format, width, height, 1, views.size(), foveation);

		int color_image_index = color_swapchain.acquire();
		vk::Image color_image = color_swapchain.images()[color_image_index].image;
		vk::Image foveation_image = color_swapchain.images()[color_image_index].foveation;
		color_swapchain.wait();
		return {color_swapchain, color_image, foveation_image};
	}();

	auto [depth_swapchain, depth_image] = [&]() -> std::pair<XrSwapchain, vk::Image> {
		if (keep_depth_buffer)
		{
			xr::swapchain & depth_swapchain = get_swapchain(depth_format, width, height, 1, views.size());

			int depth_image_index = depth_swapchain.acquire();
			vk::Image depth_image = depth_swapchain.images()[depth_image_index].image;
			depth_swapchain.wait();
			return {depth_swapchain, depth_image};
		}
		else
			return {};
	}();

	for (auto && [index, view]: utils::enumerate(views))
	{
		frames.push_back({
		        .projection = projection_matrix(view.fov, constants::lobby::near_plane),
		        .view = view_matrix(view.pose),
		});

		composition_layer_color.push_back({
		        .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
		        .pose = view.pose,
		        .fov = view.fov,
		        .subImage = {
		                .swapchain = color_swapchain,
		                .imageRect = {
		                        .offset = {0, 0},
		                        .extent = {(int)width, (int)height},
		                },
		                .imageArrayIndex = (uint32_t)index,
		        },
		});

		if (keep_depth_buffer)
		{
			composition_layer_depth.push_back({
			        .type = XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR,
			        .subImage = {
			                .swapchain = depth_swapchain,
			                .imageRect = {
			                        .offset = {0, 0},
			                        .extent = {(int)width, (int)height},
			                },
			                .imageArrayIndex = (uint32_t)index,
			        },
			        .minDepth = 0,
			        .maxDepth = 1,
			        .nearZ = std::bit_cast<float>(0x7f800000), // infinity
			        .farZ = constants::lobby::near_plane,
			});
			static_assert(std::numeric_limits<float>::is_iec559);
		}
	}

	// TODO: compute transforms only once

	renderer->render(
	        world,
	        {clear_color.r, clear_color.g, clear_color.b, clear_color.a},
	        layer_mask,
	        vk::Extent2D{width, height},
	        swapchain_format,
	        depth_format,
	        color_image,
	        depth_image,
	        foveation_image,
	        frames,
	        render_debug_draws);

	add_projection_layer(
	        XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
	        space,
	        composition_layer_color,
	        composition_layer_depth);
}

xr::swapchain & scene::get_swapchain(vk::Format format, int32_t width, int32_t height, int sample_count, uint32_t array_size, const std::optional<xr::foveation_profile> & foveation)
{
	XrFoveationProfileFB foveation_profile = XR_NULL_HANDLE;
	XrFoveationLevelFB foveation_level = XR_FOVEATION_LEVEL_NONE_FB;
	float foveation_vertical_offset_degrees = 0;
	bool foveation_dynamic = false;

	if (foveation)
	{
		foveation_profile = *foveation;
		foveation_level = foveation->level();
		foveation_vertical_offset_degrees = foveation->vertical_offset_degrees();
		foveation_dynamic = foveation->dynamic();
	}

	// Look for an exact match
	for (swapchain_entry & entry: swapchains)
	{
		if (not entry.used and
		    entry.format == format and
		    entry.width == width and
		    entry.height == height and
		    entry.sample_count == sample_count and
		    entry.array_size == array_size and
		    entry.foveation_level == foveation_level and
		    entry.foveation_vertical_offset_degrees == foveation_vertical_offset_degrees and
		    entry.foveation_dynamic == foveation_dynamic)
		{
			entry.used = true;
			return entry.swapchain;
		}
	}

	// Look for a swapchain with a different foveation profile
	for (swapchain_entry & entry: swapchains)
	{
		if (not entry.used and
		    entry.format == format and
		    entry.width == width and
		    entry.height == height and
		    entry.sample_count == sample_count and
		    entry.array_size == array_size)
		{
			spdlog::info("Updating swapchain foveation profile to {}, {:.1f} deg", magic_enum::enum_name(foveation_level), foveation_vertical_offset_degrees);
			entry.foveation_level = foveation_level;
			entry.foveation_vertical_offset_degrees = foveation_vertical_offset_degrees;
			entry.foveation_dynamic = foveation_dynamic;

			entry.used = true;
			entry.swapchain.update_foveation(*foveation);
			return entry.swapchain;
		}
	}

	spdlog::info("Creating new swapchain: {}, {}x{}, {} sample(s), {} level(s)", magic_enum::enum_name(format), width, height, sample_count, array_size);
	swapchain_entry new_swapchain{
	        .format = format,
	        .width = width,
	        .height = height,
	        .sample_count = sample_count,
	        .array_size = array_size,
	        .foveation_level = foveation_level,
	        .foveation_vertical_offset_degrees = foveation_vertical_offset_degrees,
	        .foveation_dynamic = foveation_dynamic,
	        .used = true,
	        .swapchain = xr::swapchain(
	                instance,
	                session,
	                device,
	                format,
	                width,
	                height,
	                sample_count,
	                array_size,
	                foveation_profile)};
	spdlog::info("Created swapchain");

	return swapchains.emplace_back(std::move(new_swapchain)).swapchain;
}

void scene::clear_swapchains()
{
	swapchains.clear();
}

void scene::render_start(bool passthrough, XrTime predicted_display_time_)
{
	blend_mode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	predicted_display_time = predicted_display_time_;
	layers.clear();
	openxr_layers.clear();

	for (swapchain_entry & swapchain: swapchains)
		swapchain.used = false;

	if (renderer)
		renderer->start_frame();

	if (passthrough)
	{
		std::visit(
		        utils::overloaded{
		                [&](std::monostate &) {
			                assert(false);
		                },
		                [&](xr::passthrough_alpha_blend & p) {
			                blend_mode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
		                },
		                [&](auto & p) {
			                layers.push_back(layer{
			                        .composition_layer = p.layer(),
			                });
		                }},
		        session.get_passthrough());
	}
}

void scene::add_projection_layer(
        XrCompositionLayerFlags flags,
        XrSpace space,
        std::span<XrCompositionLayerProjectionView> color_views,
        std::span<XrCompositionLayerDepthInfoKHR> depth_views)
{
	layers.push_back(layer{
	        .composition_layer = XrCompositionLayerProjection{
	                .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
	                .next = nullptr,
	                .layerFlags = flags,
	                .space = space,
	                .viewCount = (uint32_t)color_views.size(),
	                .views = nullptr,
	        },
	        .color_views{color_views.begin(), color_views.end()},
	        .depth_views{depth_views.begin(), depth_views.end()},
	});
}

void scene::add_quad_layer(
        XrCompositionLayerFlags flags,
        XrSpace space,
        XrEyeVisibility eyeVisibility,
        XrSwapchainSubImage subImage,
        XrPosef pose,
        XrExtent2Df size)
{
	layers.push_back(layer{
	        .composition_layer = XrCompositionLayerQuad{
	                .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
	                .next = nullptr,
	                .layerFlags = flags,
	                .space = space,
	                .eyeVisibility = eyeVisibility,
	                .subImage = subImage,
	                .pose = pose,
	                .size = size,
	        },
	});
}

void scene::set_color_scale_bias(XrColor4f scale, XrColor4f bias)
{
	assert(not layers.empty());
	layers.back().color_scale_bias = XrCompositionLayerColorScaleBiasKHR{
	        .type = XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR,
	        .colorScale = scale,
	        .colorBias = bias,
	};
}

void scene::set_depth_test(bool write, XrCompareOpFB op)
{
	assert(not layers.empty());
	layers.back().depth_test = XrCompositionLayerDepthTestFB{
	        .type = XR_TYPE_COMPOSITION_LAYER_DEPTH_TEST_FB,
	        .depthMask = write,
	        .compareOp = op,
	};
}

void scene::set_layer_settings(XrCompositionLayerSettingsFlagsFB flags)
{
	assert(not layers.empty());
	layers.back().settings = XrCompositionLayerSettingsFB{
	        .type = XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB,
	        .layerFlags = flags,
	};
}

void scene::render_end()
{
	// Fixup all pointers
	for (auto & i: layers)
	{
		XrCompositionLayerBaseHeader * base = std::visit(
		        utils::overloaded{
		                [&](XrCompositionLayerProjection & layer) {
			                layer.views = i.color_views.data();

			                for (auto [color, depth]: std::views::zip(i.color_views, i.depth_views))
				                color.next = &depth;

			                return reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer);
		                },
		                [&](XrCompositionLayerQuad & layer) {
			                return reinterpret_cast<XrCompositionLayerBaseHeader *>(&layer);
		                },
		                [&](XrCompositionLayerBaseHeader * layer) {
			                return layer;
		                },
		        },
		        i.composition_layer);

		if (i.color_scale_bias)
		{
			i.color_scale_bias->next = base->next;
			base->next = &*i.color_scale_bias;
		}

		if (i.depth_test)
		{
			i.depth_test->next = base->next;
			base->next = &*i.depth_test;
		}

		if (i.settings)
		{
			i.settings->type = XR_TYPE_COMPOSITION_LAYER_SETTINGS_FB;
			i.settings->next = base->next;
			base->next = &*i.settings;
		}

		openxr_layers.push_back(base);
	}

	if (renderer)
		renderer->end_frame();

	// Release all swapchains after the renderer has submitted the command buffers
	for (auto & swapchain: swapchains)
	{
		if (swapchain.used)
			swapchain.swapchain.release();
	}

	session.end_frame(predicted_display_time, openxr_layers, blend_mode);
}

template <typename T>
void copy_components(entt::registry & scene, const entt::registry & prefab, const std::unordered_map<entt::entity, entt::entity> & entity_map)
{
	for (const auto & [entity, component]: prefab.view<T>().each())
	{
		scene.emplace<T>(entity_map.at(entity), component);
	}
}

std::pair<entt::entity, components::node &> scene::add_gltf(std::shared_ptr<entt::registry> gltf, uint32_t layer_mask)
{
	auto root = world.create();
	auto & node = world.emplace<components::node>(root);
	node.layer_mask = layer_mask;

	auto prefab_entities = gltf->view<entt::entity>();

	std::vector<entt::entity> scene_entities{prefab_entities.size()};
	world.create(scene_entities.begin(), scene_entities.end());

	std::unordered_map<entt::entity, entt::entity> entity_map; // key: prefab entity, value: scene entity

	entity_map.emplace(entt::null, root);
	for (auto [prefab_entity, scene_entity]: std::ranges::zip_view(prefab_entities, scene_entities))
		entity_map.emplace(prefab_entity, scene_entity);

	copy_components<components::node>(world, *gltf, entity_map);
	copy_components<components::animation>(world, *gltf, entity_map);

	// update links
	for (auto [prefab_entity, scene_entity]: entity_map)
	{
		if (prefab_entity == entt::null)
			continue;

		auto * node = world.try_get<components::node>(scene_entity);
		auto * anim = world.try_get<components::animation>(scene_entity);

		if (node)
		{
			node->parent = entity_map.at(node->parent);

			for (auto & joint: node->joints)
			{
				joint.first = entity_map.at(joint.first);
			}
		}

		if (anim)
		{
			for (auto & track: anim->tracks)
			{
				std::visit(utils::overloaded{[&](auto & t) { t.target = entity_map.at(t.target); }}, track);
			}
		}
	}

	return {root, node};
}

std::shared_ptr<entt::registry> scene::load_gltf(const std::filesystem::path & path, std::function<void(float)> progress_cb)
{
	return gltf_cache->load(path, path, progress_cb);
}

void scene::unload_gltf(const std::filesystem::path & path)
{
	gltf_cache->remove(path);
}

std::pair<entt::entity, components::node &> scene::add_gltf(const std::filesystem::path & path, uint32_t layer_mask)
{
	return add_gltf(load_gltf(path), layer_mask);
}

void scene::remove(entt::entity entity)
{
	std::vector to_be_removed{entity};

	while (not to_be_removed.empty())
	{
		entt::entity parent = to_be_removed.back();
		to_be_removed.pop_back();
		world.destroy(parent);

		for (auto [e, node]: world.view<components::node>().each())
		{
			if (node.parent == parent)
				to_be_removed.push_back(e);
		}

		for (auto [e, anim]: world.view<components::animation>().each())
		{
			if (std::ranges::any_of(anim.tracks,
			                        [&](const auto & track) { return std::visit(
				                                                  [&](const components::animation_track_base & track) {
					                                                  return track.target == parent;
				                                                  },
				                                                  track); }))
			{
				to_be_removed.push_back(e);
			}
		}
	}
}

void scene::on_unfocused() {}
void scene::on_focused() {}
void scene::on_xr_event(const xr::event &) {}

bool scene::on_input_key_down(uint8_t key_code)
{
	return false;
}
bool scene::on_input_key_up(uint8_t key_code)
{
	return false;
}
bool scene::on_input_mouse_move(float x, float y)
{
	return false;
}
bool scene::on_input_button_down(uint8_t button)
{
	return false;
}
bool scene::on_input_button_up(uint8_t button)
{
	return false;
}
bool scene::on_input_scroll(float h, float v)
{
	return false;
}
