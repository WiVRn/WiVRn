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

#include "application.h"
#include "asset.h"
#include "openxr/openxr.h"
#include "utils/ranges.h"
#include "vulkan/vulkan_enums.hpp"
#include "vulkan/vulkan_handles.hpp"
#include "vulkan/vulkan_to_string.hpp"
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/gtc/matrix_access.hpp>

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

static vk::raii::RenderPass create_renderpass(vk::raii::Device& device, vk::Format format, bool clear)
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

	return vk::raii::RenderPass(device, vk::RenderPassCreateInfo{
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

std::optional<ImVec2> imgui_context::ray_plane_intersection(const imgui_context::controller_state& in)
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

		if (lambda < 0)
		{
			coord.x = ray_start.x + lambda * ray_dir.x;
			coord.y = ray_start.y + lambda * ray_dir.y;

			// Convert from mesh coordinates to imgui coordinates
			coord = coord / scale;

			if (fabs(coord.x) <= 0.5 && fabs(coord.y) <= 0.5)
				return ImVec2(
					(0.5 + coord.x) * size.width,
					(0.5 - coord.y) * size.height);
		}
	}

	return {};
}

imgui_context::imgui_frame& imgui_context::get_frame(vk::Image destination)
{
	for(auto& i: frames)
	{
		if (i.destination == destination)
			return i;
	}

	auto& frame = frames.emplace_back();

	frame.destination = destination;

	// Only 1 mipmap level for the framebuffer view
	frame.image_view_framebuffer = vk::raii::ImageView(device, vk::ImageViewCreateInfo{
		.image = destination,
		.viewType = vk::ImageViewType::e2D,
		.format = format,
		.subresourceRange = {
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0,
			.levelCount = 1,
			.baseArrayLayer = 0,
			.layerCount = 1,
		}
	});

	frame.framebuffer = vk::raii::Framebuffer(device, vk::FramebufferCreateInfo{
		.renderPass = *renderpass,
		.attachmentCount = 1,
		.pAttachments = &*frame.image_view_framebuffer,
		.width = size.width,
		.height = size.height,
		.layers = 1,
	});

	frame.command_buffer = std::move(device.allocateCommandBuffers({
		.commandPool = *command_pool,
		.commandBufferCount = 1,
	})[0]);

	frame.fence = device.createFence(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});

	return frame;
}

static const std::array pool_sizes =
{
	vk::DescriptorPoolSize{
		.type = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = 1,
	}
};

imgui_context::imgui_context(vk::raii::Device& device, uint32_t queue_family_index, vk::raii::Queue& queue, XrSpace world, std::span<controller> controllers_, vk::Extent2D size, float resolution, vk::Format format, int image_count) :
	device(device),
	queue_family_index(queue_family_index),
	queue(queue),
	descriptor_pool(device, vk::DescriptorPoolCreateInfo
	{
		.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		.maxSets = 1,
		.poolSizeCount = pool_sizes.size(),
		.pPoolSizes = pool_sizes.data(),
	}),
	renderpass(create_renderpass(device, format, true)),
	command_pool(device, vk::CommandPoolCreateInfo{
		.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
		.queueFamilyIndex = queue_family_index,
	}),
	size(size),
	format(format),
	scale(size.width / resolution, size.height / resolution),
	context(ImGui::CreateContext()),
	io(ImGui::GetIO()),
	world(world)
{
	controllers.reserve(controllers_.size());
	for(const auto& i:controllers_)
		controllers.emplace_back(i, controller_state{});

	io.IniFilename = nullptr;

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
		.ImageCount = (uint32_t)image_count, // used to cycle between VkBuffers in ImGui_ImplVulkan_RenderDrawData
		.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
		.Allocator = nullptr,
		.CheckVkResultFn = check_vk_result,
	};

	ImGui_ImplVulkan_Init(&init_info, *renderpass);

	// Load Fonts
	ImFontConfig config;
	asset roboto("Roboto-Regular.ttf");
	config.FontDataOwnedByAtlas = false;
	io.Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(roboto.data()), roboto.size(), 45, &config);

	large_font = io.Fonts->AddFontFromMemoryTTF(const_cast<std::byte*>(roboto.data()), roboto.size(), 90, &config);


	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	// ImGui::StyleColorsClassic();
	//ImGui::StyleColorsLight();

	ImGuiStyle& style = ImGui::GetStyle();

	style.ScaleAllSizes(2.5f);

	style.WindowPadding = { 50, 50 };
	style.WindowBorderSize = 10; // Not scaled by ScaleAllSizes
	style.WindowRounding = 25;
	style.ItemSpacing = {50, 50};

	style.FrameRounding = 10;
	style.FramePadding = ImVec2(15, 10);

	style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0,0,0,0.8);

	// TODO: scroll to drag https://github.com/ocornut/imgui/issues/3379
}

void imgui_context::new_frame(XrTime display_time)
{
	if (last_display_time)
		io.DeltaTime = std::min((display_time - last_display_time) * 1e-9f, 0.1f);
	last_display_time = display_time;

	float scroll_scale = io.DeltaTime * 3;

	size_t new_focused_controller = focused_controller;

	std::vector<controller_state> new_states;

	for(auto&& [index, controller]: utils::enumerate(controllers))
	{
		auto& [ctrl, state] = controller;

		controller_state& new_state = new_states.emplace_back();

		if (auto location = application::locate_controller(ctrl.aim, world, display_time); location)
		{
			new_state.active = true;
			new_state.aim_position = location->first;
			new_state.aim_orientation = location->second;
		}
		else
		{
			new_state.active = false;
		}

		if (ctrl.squeeze)
		{
			auto squeeze = application::read_action_float(ctrl.squeeze).value_or(std::pair{0,0});
			new_state.squeeze_value = squeeze.second;

			// TODO tunable
			if (new_state.squeeze_value < 0.5)
				new_state.squeeze_clicked = false;
			else if (new_state.squeeze_value > 0.8)
				new_state.squeeze_clicked = true;
		}

		if (ctrl.trigger)
		{
			auto trigger = application::read_action_float(ctrl.trigger).value_or(std::pair{0,0});
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
				new_state.scroll_value = { -act->second.x * scroll_scale, act->second.y * scroll_scale };
			else
				new_state.scroll_value = {0, 0};
		}

		if (new_state.squeeze_clicked || new_state.trigger_clicked || glm::length(new_state.scroll_value) > 0.01f)
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

	std::optional<ImVec2> position;
	float trigger;

	if (new_focused_controller != (size_t)-1)
	{
		position = ray_plane_intersection(new_states[new_focused_controller]);
		trigger = new_states[new_focused_controller].trigger_value;
		auto scroll = new_states[new_focused_controller].scroll_value;

		bool last_trigger = controllers[new_focused_controller].second.trigger_clicked;

		if (position)
		{
			io.AddMousePosEvent(position->x, position->y);

			button_pressed = new_states[new_focused_controller].trigger_clicked;

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
		position = {};
		trigger = 0;
	}

	focused_controller = new_focused_controller;
	for(auto&& [controller, next_state]: utils::zip(controllers, new_states))
	{
		controller.second = next_state;
	}

	// Start the Dear ImGui frame
	ImGui::SetCurrentContext(context);
	ImGui_ImplVulkan_NewFrame();

	io.DisplaySize = ImVec2(size.width, size.height);
	io.DisplayFramebufferScale = ImVec2(1, 1);

	// See ImGui_ImplSDL2_ProcessEvent

	ImGui::NewFrame();

	ImDrawList* draw_list = ImGui::GetForegroundDrawList();

	if (position && io.WantCaptureMouse)
	{
		ImU32 color_pressed = ImGui::GetColorU32(ImVec4(0, 0.2, 1, 0.8));
		ImU32 color_unpressed = ImGui::GetColorU32(ImVec4(1, 1, 1, 0.8));

		draw_list->AddCircleFilled(*position, 10, button_pressed ? color_pressed : color_unpressed);
		draw_list->AddCircle(*position, 12, ImGui::GetColorU32(ImVec4(0,0,0,0.8)), 0, 4);
	}
}

void imgui_context::render(vk::Image destination)
{
	ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

	if (is_minimized)
		return;

	auto& f = get_frame(destination);
	auto& cb = f.command_buffer;

	device.waitForFences(*f.fence, true, 1'000'000'000); // TODO check timeout
	device.resetFences(*f.fence);

	cb.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	vk::ClearValue clear{vk::ClearColorValue(0, 0, 0, 0)};

	cb.beginRenderPass(vk::RenderPassBeginInfo{
		.renderPass = *renderpass,
		.framebuffer = *f.framebuffer,
		.renderArea = {
			.extent = size,
		},
		.clearValueCount = 1,
		.pClearValues = &clear
	}, vk::SubpassContents::eInline);

	ImGui_ImplVulkan_RenderDrawData(draw_data, *cb);

	cb.endRenderPass();

	cb.end();

	queue.submit(vk::SubmitInfo{
		.commandBufferCount = 1,
		.pCommandBuffers = &*cb,
	}, *f.fence);

	// Use the aliasing constructor
	// return std::shared_ptr<vk::raii::ImageView>{viewport, &vp.frames[vp.frameindex].image_view_texture};
}

imgui_context::~imgui_context()
{
	// Release the command buffers, they will be destroyed with the command pool
	for(auto& f: frames)
		f.command_buffer.release();

	// Wait for fences before ImGui_ImplVulkan_Shutdown is called
	std::vector<vk::Fence> fences;
	for(auto& f: frames)
		fences.push_back(*f.fence);

	device.waitForFences(fences, true, 1'000'000'000);


	ImGui_ImplVulkan_Shutdown();
	ImGui::DestroyContext(context);
}