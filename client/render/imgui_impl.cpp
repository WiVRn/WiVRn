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

#include "imgui_impl.h"
#include "implot.h"

#include "application.h"
#include "asset.h"
#include "image_loader.h"
#include "openxr/openxr.h"
#include "utils/ranges.h"
#include "vulkan/vulkan_enums.hpp"
#include "vulkan/vulkan_handles.hpp"
#include "vulkan/vulkan_to_string.hpp"
#include <algorithm>
#include <backends/imgui_impl_vulkan.h>
#include <boost/locale.hpp>
#include <cmath>
#include <cstddef>
#include <glm/gtc/matrix_access.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <locale>
#include <optional>
#include <spdlog/spdlog.h>

#include "IconsFontAwesome6.h"
#include <vector>

/* Do not use:
 *
 * ImGui_ImplVulkanH_SelectSurfaceFormat
 * ImGui_ImplVulkanH_SelectPresentMode
 * ImGui_ImplVulkanH_GetMinImageCountFromPresentMode
 * ImGui_ImplVulkanH_CreateWindowSwapChain
 * ImGui_ImplVulkanH_CreateOrResizeWindow
 * ImGui_ImplVulkanH_DestroyWindow
 *
 * struct ImGui_ImplVulkanH_Window
 */

static vk::raii::RenderPass create_renderpass(vk::raii::Device & device, vk::Format format, bool clear)
{
	vk::AttachmentDescription attachment{
	        .format = format,
	        .samples = vk::SampleCountFlagBits::e1,
	        .loadOp = clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eDontCare,
	        .storeOp = vk::AttachmentStoreOp::eStore,
	        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
	        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
	        .initialLayout = vk::ImageLayout::eUndefined,
	        .finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::AttachmentReference color_attachment{
	        .attachment = 0,
	        .layout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	vk::SubpassDescription subpass = {
	        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
	        .colorAttachmentCount = 1,
	        .pColorAttachments = &color_attachment,
	};

	vk::SubpassDependency dependency = {
	        .srcSubpass = VK_SUBPASS_EXTERNAL,
	        .dstSubpass = 0,
	        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
	        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
	        .srcAccessMask = {},
	        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
	};

	return vk::raii::RenderPass(
	        device, vk::RenderPassCreateInfo{
	                        .attachmentCount = 1,
	                        .pAttachments = &attachment,
	                        .subpassCount = 1,
	                        .pSubpasses = &subpass,
	                        .dependencyCount = 1,
	                        .pDependencies = &dependency,
	                });
}

static void check_vk_result(VkResult result)
{
	if (result < 0)
	{
		spdlog::error("Vulkan error in Dear ImGui: {}", vk::to_string((vk::Result)result));
		abort();
	}
}

const std::vector<ImWchar> get_ranges_ja()
{
	auto atlas = std::make_unique<ImFontAtlas>();
	auto vec = std::vector<ImWchar>();
	auto ranges = atlas->GetGlyphRangesJapanese();
	for (int i = 0;; i++)
	{
		if (ranges[i] == 0)
			break;
		vec.push_back(ranges[i]);
	}
	return vec;
}

const std::map<std::string, std::vector<ImWchar>> glyph_ranges_per_language =
        {
                {"fr", {0x0020, 0x017f, 0}}, // Basic Latin + Latin Supplement + Latin Extended-A
                {"ja", get_ranges_ja()}};

std::optional<std::pair<ImVec2, float>> imgui_context::ray_plane_intersection(const imgui_context::controller_state & in) const
{
	if (!in.active)
		return {};

	auto M = glm::transpose(glm::mat3_cast(orientation_)); // world-to-plane transform

	glm::vec3 controller_direction = glm::column(glm::mat3_cast(in.aim_orientation), 2);

	// Compute all vectors in the reference frame of the GUI plane
	glm::vec3 ray_start = M * (in.aim_position - position_);
	glm::vec3 ray_dir = M * controller_direction;

	if (ray_dir.z > 0.0001f)
	{
		glm::vec2 coord;

		// ray_start + lambda × ray_dir ∈ imgui plane
		// => ray_start.z + lambda × ray_dir.z = 0
		float lambda = -ray_start.z / ray_dir.z;

		coord.x = ray_start.x + lambda * ray_dir.x;
		coord.y = ray_start.y + lambda * ray_dir.y;

		// Convert from mesh coordinates to imgui coordinates
		coord = coord / scale_;

		if (fabs(coord.x) <= 0.5 && fabs(coord.y) <= 0.5)
			return std::make_pair(ImVec2(
			                              (0.5 + coord.x) * size.width,
			                              (0.5 - coord.y) * size.height),
			                      -lambda);
	}

	return {};
}

imgui_context::imgui_frame & imgui_context::get_frame(vk::Image destination)
{
	for (auto & i: frames)
	{
		if (i.destination == destination)
			return i;
	}

	auto & frame = frames.emplace_back();

	frame.destination = destination;

	// Only 1 mipmap level for the framebuffer view
	frame.image_view_framebuffer = vk::raii::ImageView(
	        device,
	        vk::ImageViewCreateInfo{
	                .image = destination,
	                .viewType = vk::ImageViewType::e2D,
	                .format = format,
	                .subresourceRange = {
	                        .aspectMask = vk::ImageAspectFlagBits::eColor,
	                        .baseMipLevel = 0,
	                        .levelCount = 1,
	                        .baseArrayLayer = 0,
	                        .layerCount = 1,
	                },
	        });

	frame.framebuffer = vk::raii::Framebuffer(
	        device,
	        vk::FramebufferCreateInfo{
	                .renderPass = *renderpass,
	                .attachmentCount = 1,
	                .pAttachments = &*frame.image_view_framebuffer,
	                .width = size.width,
	                .height = size.height,
	                .layers = 1,
	        });

	return frame;
}

static const std::array pool_sizes =
        {
                vk::DescriptorPoolSize{
                        .type = vk::DescriptorType::eCombinedImageSampler,
                        .descriptorCount = 100,
                }};

static const vk::DescriptorSetLayoutBinding layout_bindings{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eFragment};

imgui_context::imgui_context(vk::raii::PhysicalDevice physical_device, vk::raii::Device & device, uint32_t queue_family_index, vk::raii::Queue & queue, XrSpace world, std::span<controller> controllers_, xr::swapchain & swapchain, glm::vec2 size) :
        physical_device(physical_device),
        device(device),
        queue_family_index(queue_family_index),
        queue(queue),
        descriptor_pool(device,
                        vk::DescriptorPoolCreateInfo{
                                .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                                .maxSets = pool_sizes[0].descriptorCount,
                                .poolSizeCount = pool_sizes.size(),
                                .pPoolSizes = pool_sizes.data(),
                        }),
        ds_layout(device, vk::DescriptorSetLayoutCreateInfo{.bindingCount = 1, .pBindings = &layout_bindings}),
        renderpass(create_renderpass(device, swapchain.format(), true)),
        command_pool(device,
                     vk::CommandPoolCreateInfo{
                             .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
                             .queueFamilyIndex = queue_family_index,
                     }),
        command_buffers(swapchain.images().size()),
        size(swapchain.extent().width, swapchain.extent().height),
        format(swapchain.format()),
        scale_(size.x, size.y),
        swapchain(swapchain),
        context(ImGui::CreateContext()),
        plot_context(ImPlot::CreateContext()),
        io((ImGui::SetCurrentContext(context), ImGui::GetIO())),
        world(world)
{
	controllers.reserve(controllers_.size());
	for (const auto & i: controllers_)
		controllers.emplace_back(i, controller_state{});

	io.IniFilename = nullptr;

	for (command_buffer & cb: command_buffers)
	{
		cb.command_buffer = std::move(device.allocateCommandBuffers({
		        .commandPool = *command_pool,
		        .commandBufferCount = 1,
		})[0]);

		cb.fence = device.createFence(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
	}

	ImGui_ImplVulkan_InitInfo init_info = {
	        .Instance = *application::get_vulkan_instance(),
	        .PhysicalDevice = *application::get_physical_device(),
	        .Device = *application::get_device(),
	        .QueueFamily = application::queue_family_index(),
	        .Queue = *application::get_queue(),
	        .PipelineCache = *application::get_pipeline_cache(),
	        .DescriptorPool = *descriptor_pool,
	        .Subpass = 0,
	        .MinImageCount = 2,
	        .ImageCount = (uint32_t)swapchain.images().size(), // used to cycle between VkBuffers in ImGui_ImplVulkan_RenderDrawData
	        .MSAASamples = VK_SAMPLE_COUNT_1_BIT,
	        .Allocator = nullptr,
	        .CheckVkResultFn = check_vk_result,
	};

	ImGui::SetCurrentContext(context);
	ImPlot::SetCurrentContext(plot_context);
	ImGui_ImplVulkan_Init(&init_info, *renderpass);

	// Load Fonts
	asset roboto("Roboto-Regular.ttf");
	asset font_awesome_regular("Font Awesome 6 Free-Regular-400.otf");
	asset font_awesome_solid("Font Awesome 6 Free-Solid-900.otf");
	{
		auto language = application::get_messages_info().language;

		const ImWchar * range = io.Fonts->GetGlyphRangesDefault();

		if (auto it = glyph_ranges_per_language.find(language); it != glyph_ranges_per_language.end())
			range = it->second.data();

		ImFontConfig config;
		config.FontDataOwnedByAtlas = false;
		io.Fonts->AddFontFromMemoryTTF(const_cast<std::byte *>(roboto.data()), roboto.size(), 30, &config, range);

		config.MergeMode = true;
		config.GlyphMinAdvanceX = 40; // Use if you want to make the icon monospaced
		static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
		io.Fonts->AddFontFromMemoryTTF(const_cast<std::byte *>(font_awesome_regular.data()), font_awesome_regular.size(), 30, &config, icon_ranges);
		io.Fonts->AddFontFromMemoryTTF(const_cast<std::byte *>(font_awesome_solid.data()), font_awesome_solid.size(), 30, &config, icon_ranges);
	}

	{
		ImFontConfig config;
		config.FontDataOwnedByAtlas = false;
		large_font = io.Fonts->AddFontFromMemoryTTF(const_cast<std::byte *>(roboto.data()), roboto.size(), 75, &config);
	}

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	// ImGui::StyleColorsClassic();
	// ImGui::StyleColorsLight();

	ImGuiStyle & style = ImGui::GetStyle();

	style.WindowBorderSize = 0;
}

void imgui_context::new_frame(XrTime display_time)
{
	ImGui::SetCurrentContext(context);
	ImPlot::SetCurrentContext(plot_context);

	if (last_display_time)
		io.DeltaTime = std::min((display_time - last_display_time) * 1e-9f, 0.1f);
	last_display_time = display_time;

	float scroll_scale = io.DeltaTime * 3;

	size_t new_focused_controller = focused_controller;

	std::vector<controller_state> new_states;

	for (auto && [index, controller]: utils::enumerate(controllers))
	{
		auto & [ctrl, state] = controller;

		controller_state & new_state = new_states.emplace_back();

		if (ctrl.hand)
		{
			if (auto joints = ctrl.hand->locate(world, display_time))
			{
				XrHandJointLocationEXT & index_tip = (*joints)[XR_HAND_JOINT_INDEX_TIP_EXT].first;
				if (index_tip.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
				{
					new_state.aim_position = {
					        index_tip.pose.position.x,
					        index_tip.pose.position.y,
					        index_tip.pose.position.z};

					new_state.aim_orientation = orientation_;

					new_state.active = true;
					auto position_distance = ray_plane_intersection(new_state);

					if (position_distance)
					{
						new_state.hover_distance = std::abs(position_distance->second);

						if (std::abs(position_distance->second) < 0.1f)
							new_state.fingertip_hovered = true;
						else
							new_state.active = false;

						if (new_state.hover_distance < 0.02)
							new_state.fingertip_touched = true;
					}
					else
						new_state.hover_distance = 1e10;
				}
			}
			continue;
		}

		if (auto location = application::locate_controller(ctrl.aim, world, display_time))
		{
			new_state.active = true;
			new_state.aim_position = location->first;
			new_state.aim_orientation = location->second;
		}

		if (ctrl.squeeze)
		{
			auto squeeze = application::read_action_float(ctrl.squeeze).value_or(std::pair{0, 0});
			new_state.squeeze_value = squeeze.second;

			// TODO tunable
			if (new_state.squeeze_value < 0.5)
				new_state.squeeze_clicked = false;
			else if (new_state.squeeze_value > 0.8)
				new_state.squeeze_clicked = true;
		}

		if (ctrl.trigger)
		{
			auto trigger = application::read_action_float(ctrl.trigger).value_or(std::pair{0, 0});
			new_state.trigger_value = trigger.second;

			// TODO tunable
			if (new_state.trigger_value < 0.5)
				new_state.trigger_clicked = false;
			else if (new_state.trigger_value > 0.8)
				new_state.trigger_clicked = true;
		}

		if (ctrl.scroll)
		{
			if (auto act = application::read_action_vec2(ctrl.scroll); act)
				new_state.scroll_value = {-act->second.x * scroll_scale, act->second.y * scroll_scale};
			else
				new_state.scroll_value = {0, 0};
		}
	}

	for (auto [new_state, controller]: utils::zip(new_states, controllers))
	{
		if (new_state.hover_distance < 0.02 && controller.second.hover_distance >= 0.02)
			new_state.fingertip_touched = true;
	}

	float closest_hover_distance = 1e10;
	for (auto && [index, new_state]: utils::enumerate(new_states))
	{
		if (new_state.hover_distance < closest_hover_distance && new_state.fingertip_hovered)
		{
			new_focused_controller = index;
			closest_hover_distance = new_state.hover_distance;
		}
		else if (new_state.squeeze_clicked || new_state.trigger_clicked || glm::length(new_state.scroll_value) > 0.01f)
		{
			new_focused_controller = index;
		}
	}

	bool focused_change = new_focused_controller != focused_controller && focused_controller != (size_t)-1;

	// Simulate a pen for the following events
	io.AddMouseSourceEvent(ImGuiMouseSource_Pen);
	if (focused_change && controllers[focused_controller].second.trigger_clicked)
	{
		// Focused controller changed: end the current click
		io.AddMouseButtonEvent(0, false);
		button_pressed = false;
	}

	std::optional<std::pair<ImVec2, float>> position_distance;

	if (new_focused_controller != (size_t)-1)
	{
		position_distance = ray_plane_intersection(new_states[new_focused_controller]);
		auto scroll = new_states[new_focused_controller].scroll_value;

		bool last_trigger = controllers[new_focused_controller].second.trigger_clicked || controllers[new_focused_controller].second.fingertip_touched;
		button_pressed = new_states[new_focused_controller].trigger_clicked ||
		                 (new_states[new_focused_controller].fingertip_touched && !controllers[new_focused_controller].second.fingertip_touched);

		if (position_distance)
		{
			io.AddMousePosEvent(position_distance->first.x, position_distance->first.y);

			if (focused_change || last_trigger != button_pressed)
			{
				io.AddMouseButtonEvent(0, button_pressed);
			}

			if (glm::length(scroll) > 0.01f)
				io.AddMouseWheelEvent(scroll.x, scroll.y);
		}
		else
		{
			if (last_trigger && !button_pressed)
			{
				io.AddMouseButtonEvent(0, button_pressed);
			}
		}
	}
	else
	{
		position_distance = {};
	}

	focused_controller = new_focused_controller;
	for (auto && [controller, next_state]: utils::zip(controllers, new_states))
	{
		controller.second = next_state;
	}

	// Start the Dear ImGui frame
	ImGui_ImplVulkan_NewFrame();

	io.DisplaySize = ImVec2(size.width, size.height);
	io.DisplayFramebufferScale = ImVec2(1, 1);

	// See ImGui_ImplSDL2_ProcessEvent

	ImGui::NewFrame();

	ImDrawList * draw_list = ImGui::GetForegroundDrawList();

	if (position_distance)
	{
		float distance_to_border = std::min({position_distance->first.x,
		                                     size.width - position_distance->first.x,
		                                     position_distance->first.y,
		                                     size.height - position_distance->first.y});

		float radius = 10; // std::clamp<float>(distance_to_border / 4, 0, 10);
		float alpha = std::clamp<float>((distance_to_border - 10) / 50, 0, 0.8);

		ImU32 color_pressed = ImGui::GetColorU32(ImVec4(0, 0.2, 1, alpha));
		ImU32 color_unpressed = ImGui::GetColorU32(ImVec4(1, 1, 1, alpha));

		bool pressed = button_pressed || new_states[new_focused_controller].fingertip_touched;

		draw_list->AddCircleFilled(position_distance->first, radius, pressed ? color_pressed : color_unpressed);
		draw_list->AddCircle(position_distance->first, radius * 1.2, ImGui::GetColorU32(ImVec4(0, 0, 0, alpha)), 0, radius * 0.4);
	}

	image_index = swapchain.acquire();
	swapchain.wait();
}

XrCompositionLayerQuad imgui_context::end_frame()
{
	vk::Image destination = swapchain.images()[image_index].image;

	ImGui::SetCurrentContext(context);
	ImPlot::SetCurrentContext(plot_context);

	ImGui::Render();

	current_command_buffer = (current_command_buffer + 1) % command_buffers.size();

	auto & f = get_frame(destination);
	auto & cb = get_command_buffer().command_buffer;
	auto & fence = get_command_buffer().fence;

	if (auto result = device.waitForFences(*fence, true, 1'000'000'000); result != vk::Result::eSuccess)
		throw std::runtime_error("vkWaitForfences: " + vk::to_string(result));
	device.resetFences(*fence);

	cb.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	vk::ClearValue clear{vk::ClearColorValue(0, 0, 0, 0)};

	cb.beginRenderPass(vk::RenderPassBeginInfo{
	                           .renderPass = *renderpass,
	                           .framebuffer = *f.framebuffer,
	                           .renderArea = {
	                                   .extent = size,
	                           },
	                           .clearValueCount = 1,
	                           .pClearValues = &clear},
	                   vk::SubpassContents::eInline);

	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *cb);

	cb.endRenderPass();

	cb.end();

	queue.submit(vk::SubmitInfo{
	                     .commandBufferCount = 1,
	                     .pCommandBuffers = &*cb,
	             },
	             *fence);

	swapchain.release();

	return XrCompositionLayerQuad{
	        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
	        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
	        .space = world,
	        .eyeVisibility = XrEyeVisibility::XR_EYE_VISIBILITY_BOTH,
	        .subImage = {
	                .swapchain = swapchain,
	                .imageRect = {
	                        .offset = {0, 0},
	                        .extent = {(int32_t)size.width, (int32_t)size.height}},
	        },
	        .pose = pose(),
	        .size = scale(),
	};
}

imgui_context::~imgui_context()
{
	ImGui::SetCurrentContext(context);
	ImPlot::SetCurrentContext(plot_context);

	std::vector<vk::Fence> fences;

	// Release the command buffers without freing them, they will be destroyed with the command pool
	for (auto & f: command_buffers)
	{
		f.command_buffer.release();
		fences.push_back(*f.fence);
	}

	// Wait for fences before ImGui_ImplVulkan_Shutdown is called
	if (auto result = device.waitForFences(fences, true, 1'000'000'000); result != vk::Result::eSuccess)
		spdlog::error("vkWaitForfences: {}", vk::to_string(result));

	ImGui_ImplVulkan_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext(context);
}

ImTextureID imgui_context::load_texture(const std::string & filename, vk::raii::Sampler && sampler)
{
	bool srgb = true;
	image_loader loader(physical_device, device, queue, command_pool);
	loader.load(asset{filename}, srgb);

	vk::raii::DescriptorSet ds = std::move(device.allocateDescriptorSets({
	        .descriptorPool = *descriptor_pool,
	        .descriptorSetCount = 1,
	        .pSetLayouts = &*ds_layout,
	})[0]);

	vk::DescriptorImageInfo image_info{
	        .sampler = *sampler,
	        .imageView = **loader.image_view,
	        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
	};

	vk::WriteDescriptorSet ds_write{
	        .dstSet = *ds,
	        .descriptorCount = 1,
	        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
	        .pImageInfo = &image_info};

	device.updateDescriptorSets(ds_write, nullptr);

	ImTextureID id = *ds;

	textures.emplace(
	        id,
	        texture_data{
	                .sampler = std::move(sampler),
	                // .image = std::move(loader.image),
	                .image_view = std::move(loader.image_view),
	                .descriptor_set = std::move(ds),
	        });

	return id;
}

ImTextureID imgui_context::load_texture(const std::string & filename)
{
	return load_texture(
	        filename,
	        vk::raii::Sampler{
	                device,
	                vk::SamplerCreateInfo{
	                        .magFilter = vk::Filter::eLinear,
	                        .minFilter = vk::Filter::eLinear,
	                        .mipmapMode = vk::SamplerMipmapMode::eLinear,
	                        .borderColor = vk::BorderColor::eFloatTransparentBlack,
	                },
	        });
}

void imgui_context::free_texture(ImTextureID texture)
{
	textures.erase(texture);
}

void imgui_context::set_current()
{
	ImGui::SetCurrentContext(context);
	ImPlot::SetCurrentContext(plot_context);
}
