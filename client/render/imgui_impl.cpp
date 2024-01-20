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
#include "utils/ranges.h"
#include "vulkan/vulkan_enums.hpp"
#include "vulkan/vulkan_handles.hpp"
#include "vulkan/vulkan_to_string.hpp"
#include <spdlog/spdlog.h>
#include "vk/allocation.h"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

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

static vk::raii::RenderPass create_renderpass(vk::raii::Device& device, vk::Format format, vk::ImageLayout final_layout, bool clear)
{
        vk::AttachmentDescription attachment{
		.format = format,
		.samples = vk::SampleCountFlagBits::e1,
		.loadOp = clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eDontCare,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
		.stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
		.initialLayout = vk::ImageLayout::eUndefined,
		.finalLayout = final_layout,
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

static std::optional<glm::vec2> ray_plane_intersection(glm::vec3 plane_center, glm::quat plane_orientation, std::pair<glm::vec3, glm::vec3> ray)
{
	auto M = glm::transpose(glm::mat3_cast(plane_orientation)); // world-to-plane transform

	glm::vec3 ray_start = M * (ray.first - plane_center);
	glm::vec3 ray_dir = M * ray.second;

	// spdlog::debug("ray_start = {}, {}, {}", ray_start.x, ray_start.y, ray_start.z);
	// spdlog::debug("ray_dir = {}, {}, {}", ray_dir.x, ray_dir.y, ray_dir.z);

	if (ray_dir.z < 0.0001f)
	{
		glm::vec2 coord;

		coord.x = ray_start.x + ray_dir.x * ray_start.z / ray_dir.z;
		coord.y = ray_start.y + ray_dir.y * ray_start.z / ray_dir.z;

		if (fabs(coord.x) <= 0.5 && fabs(coord.y) <= 0.5)
			return coord;
	}

	return {};
}

static std::pair<glm::vec3, glm::vec3> compute_ray(glm::vec3 controller_position, glm::quat controller_orientation)
{
	return std::make_pair(controller_position, glm::transpose(glm::mat3_cast(controller_orientation)) * glm::vec3(0, 0, -1));
}

imgui_viewport::imgui_viewport(vk::raii::Device& device, vk::raii::CommandPool& command_pool, vk::RenderPass renderpass, vk::Extent2D size, vk::Format format) :
	device(device),
	size(size)
{
	uint32_t num_mipmaps = 1;

	for(auto& frame:frames)
	{
		frame.image = image_allocation(
			vk::ImageCreateInfo{
				.imageType = vk::ImageType::e2D,
				 .format = format,
				 .extent = {
					size.width,
					size.height,
					1
				 },
				 .mipLevels = num_mipmaps,
				 .arrayLayers = 1,
				 .samples = vk::SampleCountFlagBits::e1,
				 .tiling = vk::ImageTiling::eOptimal,
				 .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled,
				 .initialLayout = vk::ImageLayout::eUndefined,
			},
			VmaAllocationCreateInfo{
				.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
			}
		);

		frame.image_view = vk::raii::ImageView(device, vk::ImageViewCreateInfo{
			.image = (vk::Image)frame.image,
			.viewType = vk::ImageViewType::e2D,
			.format = format,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = num_mipmaps,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		});

		frame.framebuffer = vk::raii::Framebuffer(device, vk::FramebufferCreateInfo{
			.renderPass = renderpass,
			.attachmentCount = 1,
			.pAttachments = &*frame.image_view,
			.width = size.width,
			.height = size.height,
			.layers = 1,
		});

		frame.command_buffer = std::move(device.allocateCommandBuffers({
			.commandPool = *command_pool,
			.commandBufferCount = 1,
		})[0]);

		frame.fence = device.createFence(vk::FenceCreateInfo{.flags = vk::FenceCreateFlagBits::eSignaled});
	}
}

imgui_context::imgui_context(vk::raii::Device& device, uint32_t queue_family_index, vk::raii::Queue& queue) :
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
	renderpass(create_renderpass(device, vk::Format::eR8G8B8A8Unorm, vk::ImageLayout::eShaderReadOnlyOptimal, true)),
	command_pool(device, vk::CommandPoolCreateInfo{
		.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient,
		.queueFamilyIndex = queue_family_index,
	}),
	context(ImGui::CreateContext()),
	io(ImGui::GetIO())
{
	viewport = std::make_shared<imgui_viewport>(device, command_pool, *renderpass, vk::Extent2D{1000, 1000}, vk::Format::eR8G8B8A8Unorm);

	// See example_sdl2_vulkan/main.cpp

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();
	//ImGui::StyleColorsLight();

	ImGui_ImplVulkan_InitInfo init_info = {
		.Instance = *application::get_vulkan_instance(),
		.PhysicalDevice = *application::get_physical_device(),
		.Device = *application::get_device(),
		.QueueFamily = application::queue_family_index(),
		.Queue = *application::get_queue(),
		.PipelineCache = *application::get_pipeline_cache(),
		.DescriptorPool = *descriptor_pool,
		.Subpass = 0,
		.MinImageCount = imgui_viewport::frames_in_flight,
		.ImageCount = imgui_viewport::frames_in_flight,
		.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
		.Allocator = nullptr,
		.CheckVkResultFn = check_vk_result,
	};

	ImGui_ImplVulkan_Init(&init_info, *renderpass);


	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != nullptr);

}

void imgui_context::new_frame(std::span<imgui_inputs> inputs)
{
	// Poll and handle events (inputs, window resize, etc.)
	// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
	// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
	// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
	// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
	// SDL_Event event;
	// while (SDL_PollEvent(&event))
	// {
	// 	ImGui_ImplSDL2_ProcessEvent(&event);
	// 	if (event.type == SDL_QUIT)
	// 		done = true;
	// 	if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
	// 		done = true;
	// }
 //
	// // Resize swap chain?
	// if (g_SwapChainRebuild)
	// {
	// 	int width, height;
	// 	SDL_GetWindowSize(window, &width, &height);
	// 	if (width > 0 && height > 0)
	// 	{
	// 		ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
	// 		ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, &g_MainWindowData, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
	// 		g_MainWindowData.FrameIndex = 0;
	// 		g_SwapChainRebuild = false;
	// 	}
	// }

	// Compute the intersection of the controllers' ray and the imgui plane
	std::vector<glm::vec2> ray_pos;
	for(auto& i: inputs)
	{
		if (!i.active)
			continue;

		auto& vp = *viewport;

		std::pair<glm::vec3, glm::vec3> ray = compute_ray(i.controller_position, i.controller_orientation);
		std::optional<glm::vec2> pos = ray_plane_intersection(vp.position, vp.orientation, ray); // between -0.5 and 0.5

		if (pos)
			ray_pos.push_back(*pos + glm::vec2(0.5, 0.5));;

	}

	// Start the Dear ImGui frame
	ImGui_ImplVulkan_NewFrame();

	io.DisplaySize = ImVec2(viewport->size.width, viewport->size.height);
	io.DisplayFramebufferScale = ImVec2(1, 1);

	// See ImGui_ImplSDL2_ProcessEvent
	io.DeltaTime = 0.016; // TODO
	io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); // TODO
	// io.AddMouseSourceEvent(event->wheel.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
	// io.AddMouseWheelEvent(wheel_x, wheel_y);

	// io.AddMouseSourceEvent(event->button.which == SDL_TOUCH_MOUSEID ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
	// io.AddMouseButtonEvent(mouse_button, (event->type == SDL_MOUSEBUTTONDOWN));
	// bd->MouseButtonsDown = (event->type == SDL_MOUSEBUTTONDOWN) ? (bd->MouseButtonsDown | (1 << mouse_button)) : (bd->MouseButtonsDown & ~(1 << mouse_button));

	ImGui::NewFrame();

	ImDrawList* draw_list = ImGui::GetForegroundDrawList();

	// spdlog::debug("{} intersections", ray_pos.size());
	for(glm::vec2 i: ray_pos)
	{
		ImVec2 j(i.x * 1000, 1000 - i.y * 1000);
		draw_list->AddCircle(j, 30, ImGui::GetColorU32(ImVec4(1, 0, 0, 0.8)), 20, 10);

		// spdlog::debug(" - {}, {}", i.x, i.y);
	}
}

std::shared_ptr<vk::raii::ImageView> imgui_context::render()
{
	ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);

	if (is_minimized)
		return {};

	auto &vp = *viewport;
	vp.frameindex = (vp.frameindex + 1) % imgui_viewport::frames_in_flight;
	auto& f = vp.frames[vp.frameindex];
	auto& cb = f.command_buffer;

	device.waitForFences(*f.fence, true, 1'000'000'000); // TODO check timeout
	device.resetFences(*f.fence);

	cb.begin({.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

	vk::ClearValue clear{vk::ClearColorValue(0, 0, 0, 0)};

	cb.beginRenderPass(vk::RenderPassBeginInfo{
		.renderPass = *renderpass,
		.framebuffer = *f.framebuffer,
		.renderArea = {
			.extent = vp.size,
		},
		.clearValueCount = 1,
		.pClearValues = &clear
	}, vk::SubpassContents::eInline);

	ImGui_ImplVulkan_RenderDrawData(draw_data, *cb);

	cb.endRenderPass();

	// TODO: create mipmaps

	cb.end();

	queue.submit(vk::SubmitInfo{
		.commandBufferCount = 1,
		.pCommandBuffers = &*cb,
	}, *f.fence);

	// Use the aliasing constructor
	return std::shared_ptr<vk::raii::ImageView>{viewport, &vp.frames[vp.frameindex].image_view};
}

imgui_context::~imgui_context()
{
	// Release the command buffers, they will be destroyed with the command pool
	for(auto& f: viewport->frames)
		f.command_buffer.release();

	// Wait for fences here and not in imgui_viewport::~imgui_viewport so that the command buffers have finished when ImGui_ImplVulkan_Shutdown is called
	std::array<vk::Fence, imgui_viewport::frames_in_flight> fences;
	for(auto&& [i, f]: utils::enumerate(viewport->frames))
		fences[i] = *f.fence;

	device.waitForFences(fences, true, 1'000'000'000);


	ImGui_ImplVulkan_Shutdown();
	ImGui::DestroyContext(context);
	// ImGui_ImplSDL2_Shutdown();
	// ImGui::DestroyContext();

	// ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);

	// SDL_DestroyWindow(window);
	// SDL_Quit();
}
