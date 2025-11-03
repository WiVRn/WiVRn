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

#pragma once

#include "render/image_loader.h"
#include "render/scene_loader.h"
#include "render/scene_renderer.h"
#include "utils/cache.h"
#include "xr/actionset.h"
#include "xr/foveation_profile.h"
#include "xr/instance.h"
#include "xr/session.h"
#include "xr/swapchain.h"
#include "xr/system.h"

#include <cstdint>
#include <entt/entity/registry.hpp>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <openxr/openxr.h>

// See http://www.nirfriedman.com/2018/04/29/unforgettable-factory/
// for automatic registration of scenes

class scene
{
public:
	struct action_binding
	{
		std::string action_name;
		std::string input_source;
	};

	struct suggested_binding
	{
		std::vector<std::string> profile_names;
		std::vector<action_binding> paths;
	};

	struct meta
	{
		// Filled by the scene class
		std::string name;
		std::vector<std::pair<std::string, XrActionType>> actions;
		std::vector<suggested_binding> bindings;

		// Filled by the application class
		xr::actionset actionset;
		std::unordered_map<std::string, std::pair<XrAction, XrActionType>> actions_by_name;
		std::unordered_map<std::string, xr::space> spaces_by_name;
	};

private:
	// Force derived classes to inherit from scene_impl<T> instead of scene
	class key
	{};
	template <typename T>
	friend class scene_impl;
	friend class application;

protected:
	static std::vector<meta *> scene_registry;

	xr::instance & instance;
	xr::system & system;
	xr::session & session;
	XrViewConfigurationType viewconfig;
	bool focused = false;

public:
	vk::raii::Instance & vk_instance;
	vk::raii::Device & device;
	vk::raii::PhysicalDevice & physical_device;
	thread_safe<vk::raii::Queue> & queue;
	uint32_t queue_family_index;

protected:
	vk::raii::CommandPool & commandpool;

	const meta & current_meta;

	std::shared_ptr<scene_renderer> renderer;

	using gltf_cache_type = utils::cache<std::string, entt::registry, scene_loader>;
	std::shared_ptr<gltf_cache_type> gltf_cache;

	using image_cache_type = utils::cache<std::string, loaded_image, image_loader>;
	std::shared_ptr<image_cache_type> image_cache;

	vk::Format swapchain_format;
	vk::Format depth_format;
	bool composition_layer_depth_test_supported;
	bool composition_layer_color_scale_bias_supported;

	std::pair<XrAction, XrActionType> get_action(const std::string & name)
	{
		return current_meta.actions_by_name.at(name);
	}

	XrSpace get_action_space(const std::string & name)
	{
		return current_meta.spaces_by_name.at(name);
	}

	// Helper functions
	static glm::mat4 projection_matrix(XrFovf fov, float zn = 0.02);
	static glm::mat4 view_matrix(XrPosef pose);

	// Layer rendering
	XrEnvironmentBlendMode blend_mode;
	XrTime predicted_display_time;

	struct layer
	{
		std::variant<XrCompositionLayerProjection, XrCompositionLayerQuad, XrCompositionLayerBaseHeader *> composition_layer;

		// TODO in place vectors
		std::vector<XrCompositionLayerProjectionView> color_views; // Used by XrCompositionLayerProjection
		std::vector<XrCompositionLayerDepthInfoKHR> depth_views;   // Used by XrCompositionLayerProjection

		std::optional<XrCompositionLayerColorScaleBiasKHR> color_scale_bias;
		std::optional<XrCompositionLayerDepthTestFB> depth_test;
		std::optional<XrCompositionLayerSettingsFB> settings;
	};

	std::vector<layer> layers;
	std::vector<XrCompositionLayerBaseHeader *> openxr_layers;

	struct swapchain_entry
	{
		vk::Format format;
		int32_t width;
		int32_t height;
		int sample_count;
		uint32_t array_size;

		XrFoveationLevelFB foveation_level;
		float foveation_vertical_offset_degrees;
		bool foveation_dynamic;

		bool used;
		xr::swapchain swapchain;
	};
	std::vector<swapchain_entry> swapchains;

	// The returned reference is valid until the next call to get_swapchain
	xr::swapchain & get_swapchain(vk::Format format, int32_t width, int32_t height, int sample_count, uint32_t array_size, const std::optional<xr::foveation_profile> & foveation = std::nullopt);
	void clear_swapchains();

	void render_start(bool passthrough, XrTime predicted_display_time);

	void add_projection_layer(
	        XrCompositionLayerFlags flags,
	        XrSpace space,
	        std::vector<XrCompositionLayerProjectionView> && color_views,
	        std::vector<XrCompositionLayerDepthInfoKHR> && depth_views = {});

	void add_quad_layer(
	        XrCompositionLayerFlags flags,
	        XrSpace space,
	        XrEyeVisibility eyeVisibility,
	        XrSwapchainSubImage subImage,
	        XrPosef pose,
	        XrExtent2Df size);

	void render_world(
	        XrCompositionLayerFlags flags,
	        XrSpace space,
	        std::span<XrView> views,
	        uint32_t width,
	        uint32_t height,
	        bool keep_depth_buffer,
	        uint32_t layer_mask,
	        XrColor4f clear_color,
	        const std::optional<xr::foveation_profile> & foveation = std::nullopt,
	        bool render_debug_draws = false);

	void set_color_scale_bias(XrColor4f scale, XrColor4f bias);
	void set_depth_test(bool write, XrCompareOpFB op);
	void set_layer_settings(XrCompositionLayerSettingsFlagsFB flags);

	void render_end();

	virtual void on_unfocused();
	virtual void on_focused();

public:
	scene(key, const meta &, std::span<const vk::Format> supported_color_formats, std::span<const vk::Format> supported_depth_formats, scene * parent_scene);

	virtual ~scene();

	void set_focused(bool status);
	virtual void render(const XrFrameState &) = 0;
	virtual void on_xr_event(const xr::event &);

	virtual bool on_input_key_down(uint8_t key_code);
	virtual bool on_input_key_up(uint8_t key_code);
	virtual bool on_input_mouse_move(float x, float y);
	virtual bool on_input_button_down(uint8_t button);
	virtual bool on_input_button_up(uint8_t button);
	virtual bool on_input_scroll(float h, float v);

	entt::registry world;

	std::shared_ptr<entt::registry> load_gltf(const std::filesystem::path & path, std::function<void(float)> progress_cb = {});
	void unload_gltf(const std::filesystem::path & path);
	std::pair<entt::entity, components::node &> add_gltf(std::shared_ptr<entt::registry> gltf, uint32_t layer_mask = -1);
	std::pair<entt::entity, components::node &> add_gltf(const std::filesystem::path & path, uint32_t layer_mask = -1);
	void clear_texture_cache()
	{
		gltf_cache->loader().clear_texture_cache();
	}

	void clear_gltf_cache()
	{
		gltf_cache->clear();
	}

	void remove(entt::entity entity);
};

template <typename T>
class scene_impl : public scene
{
	friend T;
	friend scene;

	// Magically register all classes that derive from scene_impl
	static bool register_scene()
	{
		scene_registry.emplace_back(&T::get_meta_scene());
		return true;
	}

	static inline bool registered = scene_impl<T>::register_scene();

	scene_impl(std::span<const vk::Format> supported_color_formats, std::span<const vk::Format> supported_depth_formats, scene & parent_scene) :
	        scene(key{}, T::get_meta_scene(), supported_color_formats, supported_depth_formats, &parent_scene)
	{
		(void)registered;
	}

	scene_impl(std::span<const vk::Format> supported_color_formats, std::span<const vk::Format> supported_depth_formats) :
	        scene(key{}, T::get_meta_scene(), supported_color_formats, supported_depth_formats, nullptr)
	{
		(void)registered;
	}
};
