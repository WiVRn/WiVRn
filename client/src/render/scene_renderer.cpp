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
#include "scene_renderer.h"
#include "application.h"
#include "spdlog/spdlog.h"
#include "utils/strings.h"

namespace
{
VkFormat gltf_to_vkformat(int component, int bits, int pixel_type, bool srgb)
{
	switch (pixel_type)
	{
		case TINYGLTF_COMPONENT_TYPE_BYTE:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
			assert(bits == 8);
			break;

		case TINYGLTF_COMPONENT_TYPE_SHORT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
			assert(bits == 32);
			break;

		case TINYGLTF_COMPONENT_TYPE_INT:
		case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
		case TINYGLTF_COMPONENT_TYPE_FLOAT:
			assert(bits == 32);
			break;

		case TINYGLTF_COMPONENT_TYPE_DOUBLE:
			assert(bits == 64);
		default:
			break;
	}

	switch (component)
	{
		case 1:
			switch (pixel_type)
			{
				case TINYGLTF_COMPONENT_TYPE_BYTE:
					return VK_FORMAT_R8_SNORM;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					return srgb ? VK_FORMAT_R8_SRGB : VK_FORMAT_R8_UNORM;

				case TINYGLTF_COMPONENT_TYPE_SHORT:
					return VK_FORMAT_R16_SNORM;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					return VK_FORMAT_R16_UNORM;

				case TINYGLTF_COMPONENT_TYPE_INT:
					return VK_FORMAT_R32_SINT;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					return VK_FORMAT_R32_UINT;

				case TINYGLTF_COMPONENT_TYPE_FLOAT:
					return VK_FORMAT_R32_SFLOAT;

				case TINYGLTF_COMPONENT_TYPE_DOUBLE:
					return VK_FORMAT_R64_SFLOAT;

				default:
					return VK_FORMAT_UNDEFINED;
			}
		case 2:
			switch (pixel_type)
			{
				case TINYGLTF_COMPONENT_TYPE_BYTE:
					return VK_FORMAT_R8G8_SNORM;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					return srgb ? VK_FORMAT_R8G8_SRGB : VK_FORMAT_R8G8_UNORM;

				case TINYGLTF_COMPONENT_TYPE_SHORT:
					return VK_FORMAT_R16G16_SNORM;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					return VK_FORMAT_R16G16_UNORM;

				case TINYGLTF_COMPONENT_TYPE_INT:
					return VK_FORMAT_R32G32_SINT;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					return VK_FORMAT_R32G32_UINT;

				case TINYGLTF_COMPONENT_TYPE_FLOAT:
					return VK_FORMAT_R32G32_SFLOAT;

				case TINYGLTF_COMPONENT_TYPE_DOUBLE:
					return VK_FORMAT_R64G64_SFLOAT;

				default:
					return VK_FORMAT_UNDEFINED;
			}
		case 3:
			switch (pixel_type)
			{
				case TINYGLTF_COMPONENT_TYPE_BYTE:
					return VK_FORMAT_R8G8B8_SNORM;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					return srgb ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM;

				case TINYGLTF_COMPONENT_TYPE_SHORT:
					return VK_FORMAT_R16G16B16_SNORM;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					return VK_FORMAT_R16G16B16_UNORM;

				case TINYGLTF_COMPONENT_TYPE_INT:
					return VK_FORMAT_R32G32B32_SINT;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					return VK_FORMAT_R32G32B32_UINT;

				case TINYGLTF_COMPONENT_TYPE_FLOAT:
					return VK_FORMAT_R32G32B32_SFLOAT;

				case TINYGLTF_COMPONENT_TYPE_DOUBLE:
					return VK_FORMAT_R64G64B64_SFLOAT;

				default:
					return VK_FORMAT_UNDEFINED;
			}
		case 4:
			switch (pixel_type)
			{
				case TINYGLTF_COMPONENT_TYPE_BYTE:
					return VK_FORMAT_R8G8B8A8_SNORM;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					return srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

				case TINYGLTF_COMPONENT_TYPE_SHORT:
					return VK_FORMAT_R16G16B16A16_SNORM;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					return VK_FORMAT_R16G16B16A16_UNORM;

				case TINYGLTF_COMPONENT_TYPE_INT:
					return VK_FORMAT_R32G32B32A32_SINT;

				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					return VK_FORMAT_R32G32B32A32_UINT;

				case TINYGLTF_COMPONENT_TYPE_FLOAT:
					return VK_FORMAT_R32G32B32A32_SFLOAT;

				case TINYGLTF_COMPONENT_TYPE_DOUBLE:
					return VK_FORMAT_R64G64B64A64_SFLOAT;

				default:
					return VK_FORMAT_UNDEFINED;
			}
		default:
			return VK_FORMAT_UNDEFINED;
	}
}

int bytes_per_pixel(VkFormat format)
{
	switch (format)
	{
		case VK_FORMAT_R8_SINT:
		case VK_FORMAT_R8_UINT:
		case VK_FORMAT_R8_SNORM:
		case VK_FORMAT_R8_UNORM:
		case VK_FORMAT_R8_SSCALED:
		case VK_FORMAT_R8_USCALED:
		case VK_FORMAT_R8_SRGB:
			return 1;

		case VK_FORMAT_R16_SINT:
		case VK_FORMAT_R16_UINT:
		case VK_FORMAT_R16_SNORM:
		case VK_FORMAT_R16_UNORM:
		case VK_FORMAT_R16_SSCALED:
		case VK_FORMAT_R16_USCALED:
			return 2;

		case VK_FORMAT_R32_SINT:
		case VK_FORMAT_R32_UINT:
		case VK_FORMAT_R32_SFLOAT:
			return 4;

		case VK_FORMAT_R64_SFLOAT:
			return 8;

		case VK_FORMAT_R8G8_SINT:
		case VK_FORMAT_R8G8_UINT:
		case VK_FORMAT_R8G8_SNORM:
		case VK_FORMAT_R8G8_UNORM:
		case VK_FORMAT_R8G8_SSCALED:
		case VK_FORMAT_R8G8_USCALED:
		case VK_FORMAT_R8G8_SRGB:
			return 2;

		case VK_FORMAT_R16G16_SINT:
		case VK_FORMAT_R16G16_UINT:
		case VK_FORMAT_R16G16_SNORM:
		case VK_FORMAT_R16G16_UNORM:
		case VK_FORMAT_R16G16_SSCALED:
		case VK_FORMAT_R16G16_USCALED:
			return 4;

		case VK_FORMAT_R32G32_SINT:
		case VK_FORMAT_R32G32_UINT:
		case VK_FORMAT_R32G32_SFLOAT:
			return 8;

		case VK_FORMAT_R64G64_SFLOAT:
			return 16;

		case VK_FORMAT_R8G8B8_SINT:
		case VK_FORMAT_R8G8B8_UINT:
		case VK_FORMAT_R8G8B8_SNORM:
		case VK_FORMAT_R8G8B8_UNORM:
		case VK_FORMAT_R8G8B8_SSCALED:
		case VK_FORMAT_R8G8B8_USCALED:
		case VK_FORMAT_R8G8B8_SRGB:
			return 3;

		case VK_FORMAT_R16G16B16_SINT:
		case VK_FORMAT_R16G16B16_UINT:
		case VK_FORMAT_R16G16B16_SNORM:
		case VK_FORMAT_R16G16B16_UNORM:
		case VK_FORMAT_R16G16B16_SSCALED:
		case VK_FORMAT_R16G16B16_USCALED:
			return 6;

		case VK_FORMAT_R32G32B32_SINT:
		case VK_FORMAT_R32G32B32_UINT:
		case VK_FORMAT_R32G32B32_SFLOAT:
			return 12;

		case VK_FORMAT_R64G64B64_SFLOAT:
			return 24;

		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SSCALED:
		case VK_FORMAT_R8G8B8A8_USCALED:
		case VK_FORMAT_R8G8B8A8_SRGB:
			return 4;

		case VK_FORMAT_R16G16B16A16_SINT:
		case VK_FORMAT_R16G16B16A16_UINT:
		case VK_FORMAT_R16G16B16A16_SNORM:
		case VK_FORMAT_R16G16B16A16_UNORM:
		case VK_FORMAT_R16G16B16A16_SSCALED:
		case VK_FORMAT_R16G16B16A16_USCALED:
			return 8;

		case VK_FORMAT_R32G32B32A32_SINT:
		case VK_FORMAT_R32G32B32A32_UINT:
		case VK_FORMAT_R32G32B32A32_SFLOAT:
			return 16;

		case VK_FORMAT_R64G64B64A64_SFLOAT:
			return 32;

		default:
			return 0;
	}
}
} // namespace

scene_renderer::scene_renderer(VkDevice device, VkPhysicalDevice physical_device, VkQueue queue) :
        device(device), physical_device(physical_device), queue(queue)
{
	try
	{
		vkGetPhysicalDeviceProperties(physical_device, &physical_device_properties);

		command_pool = vk::command_pool(device, application::queue_family_index());

		VkFenceCreateInfo fence_info{
		        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		};
		CHECK_VK(vkCreateFence(device, &fence_info, nullptr, &staging_fence));
	}
	catch (...)
	{
		if (device && staging_fence)
			vkDestroyFence(device, staging_fence, nullptr);

		throw;
	}
}

scene_renderer::~scene_renderer()
{
	if (device && staging_fence)
		vkDestroyFence(device, staging_fence, nullptr);
}

void scene_renderer::reserve(size_t size)
{
	if (size > staging_buffer_size)
	{
		VkBufferCreateInfo buffer_info{};
		buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		buffer_info.size = size;
		buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

		staging_buffer = vk::buffer(device, buffer_info);
		staging_memory = vk::device_memory(device, physical_device, staging_buffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		staging_buffer_size = size;
		staging_memory.map_memory();
	}
}

void scene_renderer::load_buffer(VkBuffer b, const void * data, size_t size)
{
	reserve(size);

	memcpy(staging_memory.data(), data, size);

	VkCommandBuffer staging_command_buffer = command_pool.allocate_command_buffer();
	try
	{
		CHECK_VK(vkResetCommandBuffer(staging_command_buffer, 0));
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		CHECK_VK(vkBeginCommandBuffer(staging_command_buffer, &begin_info));

		VkBufferCopy copy_info{
		        .srcOffset = 0,
		        .dstOffset = 0,
		        .size = size};
		vkCmdCopyBuffer(staging_command_buffer, staging_buffer, b, 1, &copy_info);

		CHECK_VK(vkEndCommandBuffer(staging_command_buffer));

		VkSubmitInfo submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &staging_command_buffer;
		CHECK_VK(vkQueueSubmit(queue, 1, &submit_info, staging_fence));

		CHECK_VK(vkWaitForFences(device, 1, &staging_fence, VK_TRUE, -1));
		CHECK_VK(vkResetFences(device, 1, &staging_fence));
	}
	catch (...)
	{
		command_pool.free_command_buffer(staging_command_buffer);
		throw;
	}

	command_pool.free_command_buffer(staging_command_buffer);
}

void scene_renderer::load_image(VkImage i, void * data, VkExtent2D size, VkFormat format, uint32_t mipmap_count, VkImageLayout final_layout)
{
	reserve(size.height * size.width * bytes_per_pixel(format));

	VkCommandBuffer staging_command_buffer = command_pool.allocate_command_buffer();

	try
	{
		CHECK_VK(vkResetCommandBuffer(staging_command_buffer, 0));
		VkCommandBufferBeginInfo begin_info{};
		begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		CHECK_VK(vkBeginCommandBuffer(staging_command_buffer, &begin_info));

		VkImageMemoryBarrier barrier{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		        .srcAccessMask = 0,
		        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
		        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		        .image = i,
		        .subresourceRange = {
		                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		                .baseMipLevel = 0,
		                .levelCount = mipmap_count,
		                .baseArrayLayer = 0,
		                .layerCount = 1,
		        },
		};
		vkCmdPipelineBarrier(staging_command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		VkBufferImageCopy copy_info{
		        .bufferOffset = 0,
		        .bufferRowLength = 0,
		        .bufferImageHeight = 0,
		        .imageSubresource = VkImageSubresourceLayers{
		                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		                .mipLevel = 0,
		                .baseArrayLayer = 0,
		                .layerCount = 1,
		        },
		        .imageOffset = {0, 0, 0},
		        .imageExtent = {size.width, size.height, 1}};
		vkCmdCopyBufferToImage(staging_command_buffer, staging_buffer, i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_info);

		VkOffset3D size_src = {(int32_t)size.width, (int32_t)size.height, 1};
		for (uint32_t mipmap = 1; mipmap < mipmap_count; mipmap++)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.subresourceRange.baseMipLevel = mipmap - 1;
			barrier.subresourceRange.levelCount = 1;
			vkCmdPipelineBarrier(staging_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

			VkOffset3D size_dst = {std::max(1, size_src.x / 2), std::max(1, size_src.y / 2), 1};
			VkImageBlit blit_info{
			        .srcSubresource = {
			                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			                .mipLevel = mipmap - 1,
			                .baseArrayLayer = 0,
			                .layerCount = 1,
			        },
			        .srcOffsets = {{0, 0, 0}, size_src},
			        .dstSubresource = {
			                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			                .mipLevel = mipmap,
			                .baseArrayLayer = 0,
			                .layerCount = 1,
			        },
			        .dstOffsets = {{0, 0, 0}, size_dst},
			};
			vkCmdBlitImage(staging_command_buffer, i, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, i, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit_info, VK_FILTER_LINEAR);

			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = final_layout;
			barrier.subresourceRange.baseMipLevel = mipmap - 1;
			vkCmdPipelineBarrier(staging_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

			size_src = size_dst;
		}

		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = final_layout;
		barrier.subresourceRange.baseMipLevel = mipmap_count - 1;
		vkCmdPipelineBarrier(staging_command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		CHECK_VK(vkEndCommandBuffer(staging_command_buffer));

		VkSubmitInfo submit_info{};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &staging_command_buffer;
		CHECK_VK(vkQueueSubmit(queue, 1, &submit_info, staging_fence));

		CHECK_VK(vkWaitForFences(device, 1, &staging_fence, VK_TRUE, -1));
		CHECK_VK(vkResetFences(device, 1, &staging_fence));
	}
	catch (...)
	{
		command_pool.free_command_buffer(staging_command_buffer);
		throw;
	}

	command_pool.free_command_buffer(staging_command_buffer);
}

void scene_renderer::cleanup()
{
	for (auto & i: shaders)
		cleanup_shader(*i);
	shaders.clear();

	for (auto & i: images)
		cleanup_image(*i);
	images.clear();

	for (auto & i: buffers)
		cleanup_buffer(*i);
	buffers.clear();

	cleanup_output_images();
}

void scene_renderer::cleanup_output_images()
{
	output_images.clear();

	for (VkImageView image_view: output_image_views)
		vkDestroyImageView(device, image_view, nullptr);
	output_image_views.clear();

	for (VkFramebuffer framebuffer: output_framebuffers)
		vkDestroyFramebuffer(device, framebuffer, nullptr);
	output_framebuffers.clear();
}

void scene_renderer::set_output_images(std::vector<VkImage> output_images_, VkExtent2D output_size_, VkFormat output_format_)
{
	output_images = std::move(output_images_);
	output_size = output_size_;
	output_format = output_format_;

	// Create renderpass
	VkAttachmentReference color_ref{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

	vk::renderpass::info renderpass_info{.attachments = {VkAttachmentDescription{
	                                             .format = output_format,
	                                             .samples = VK_SAMPLE_COUNT_1_BIT,
	                                             .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
	                                             .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
	                                             .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                                             .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	                                     }},
	                                     .subpasses = {VkSubpassDescription{
	                                             .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
	                                             .colorAttachmentCount = 1,
	                                             .pColorAttachments = &color_ref,
	                                     }},
	                                     .dependencies = {}};

	renderpass = vk::renderpass(device, renderpass_info);

	// Create image views and framebuffers
	output_image_views.reserve(output_images.size());
	output_framebuffers.reserve(output_images.size());
	for (VkImage image: output_images)
	{
		VkImageViewCreateInfo iv_info{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		        .image = image,
		        .viewType = VK_IMAGE_VIEW_TYPE_2D,
		        .format = output_format,
		        .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
		        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		                             .baseMipLevel = 0,
		                             .levelCount = 1,
		                             .baseArrayLayer = 0,
		                             .layerCount = 1}};

		VkImageView image_view;
		CHECK_VK(vkCreateImageView(device, &iv_info, nullptr, &image_view));
		output_image_views.push_back(image_view);

		VkFramebufferCreateInfo fb_create_info{
		        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		        .renderPass = renderpass,
		        .attachmentCount = 1,
		        .pAttachments = &image_view,
		        .width = output_size.width,
		        .height = output_size.height,
		        .layers = 1,
		};

		VkFramebuffer framebuffer;
		CHECK_VK(vkCreateFramebuffer(device, &fb_create_info, nullptr, &framebuffer));
		output_framebuffers.push_back(framebuffer);
	}
}

void scene_renderer::cleanup_shader(shader & s)
{
	for (auto & i: s.descriptor_pools)
		vkDestroyDescriptorPool(device, i, nullptr);

	if (s.pipeline)
		vkDestroyPipeline(device, s.pipeline, nullptr);
	if (s.pipeline_layout)
		vkDestroyPipelineLayout(device, s.pipeline_layout, nullptr);
	if (s.descriptor_set_layout)
		vkDestroyDescriptorSetLayout(device, s.descriptor_set_layout, nullptr);

	s.descriptor_pools.clear();
	s.pipeline = VK_NULL_HANDLE;
	s.pipeline_layout = VK_NULL_HANDLE;
	s.descriptor_set_layout = VK_NULL_HANDLE;
}

scene_renderer::shader * scene_renderer::create_shader(std::string name, VkPrimitiveTopology topology, std::vector<VkDescriptorSetLayoutBinding> uniform_bindings, std::vector<VkVertexInputBindingDescription> vertex_bindings, std::vector<VkVertexInputAttributeDescription> vertex_attributes)
{
	auto s = std::make_unique<shader>();

	try
	{
		// Create graphics pipeline
		s->vertex_shader = vk::shader(device, name + ".vert").release();
		s->fragment_shader = vk::shader(device, name + ".frag").release();

		VkDescriptorSetLayoutCreateInfo layout_info{
		        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		        .bindingCount = (uint32_t)uniform_bindings.size(),
		        .pBindings = uniform_bindings.data(),
		};

		CHECK_VK(vkCreateDescriptorSetLayout(device, &layout_info, nullptr, &s->descriptor_set_layout));

		VkPipelineLayoutCreateInfo pipeline_layout_create_info{
		        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		        .setLayoutCount = 1,
		        .pSetLayouts = &s->descriptor_set_layout,
		};

		CHECK_VK(vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr, &s->pipeline_layout));

		VkPipelineColorBlendAttachmentState pcbas{.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
		                                                            VK_COLOR_COMPONENT_G_BIT |
		                                                            VK_COLOR_COMPONENT_B_BIT};

		vk::pipeline::graphics_info pipeline_info{
		        .shader_stages =
		                {{.stage = VK_SHADER_STAGE_VERTEX_BIT, .module = s->vertex_shader, .pName = "main"},
		                 {.stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = s->fragment_shader, .pName = "main"}},
		        .vertex_input_bindings = vertex_bindings,
		        .vertex_input_attributes = vertex_attributes,
		        .InputAssemblyState = {.topology = topology},
		        .viewports = {VkViewport{.x = 0,
		                                 .y = 0,
		                                 .width = (float)output_size.width,
		                                 .height = (float)output_size.height,
		                                 .minDepth = 0,
		                                 .maxDepth = 1}},
		        .scissors = {VkRect2D{
		                .offset = {0, 0},
		                .extent = output_size,
		        }},
		        .RasterizationState = {.polygonMode = VK_POLYGON_MODE_FILL, .lineWidth = 1},
		        .MultisampleState = {.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT},
		        .ColorBlendState = {.attachmentCount = 1, .pAttachments = &pcbas},
		        .dynamic_states = {},
		        .renderPass = renderpass,
		        .subpass = 0,
		};

		s->pipeline = vk::pipeline(device, pipeline_info, s->pipeline_layout).release();
	}
	catch (...)
	{
		cleanup_shader(*s);
		throw;
	}

	return shaders.emplace_back(std::move(s)).get();
}

void scene_renderer::cleanup_image(scene_renderer::image & i)
{
	if (i.image)
		vkDestroyImage(device, i.image, nullptr);

	if (i.memory)
		vkFreeMemory(device, i.memory, nullptr);

	i.image = VK_NULL_HANDLE;
	i.memory = VK_NULL_HANDLE;
}

scene_renderer::image * scene_renderer::create_image(void * data, VkExtent2D size, VkFormat format)
{
	auto i = std::make_unique<image>();

	try
	{
		uint32_t mipmaps = std::log2(std::max(size.width, size.height)) + 1;

		VkImageCreateInfo image_info{
		        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		        .flags = 0,
		        .imageType = VK_IMAGE_TYPE_2D,
		        .format = format,
		        .extent = {size.width, size.height, 1},
		        .mipLevels = mipmaps,
		        .arrayLayers = 1,
		        .samples = VK_SAMPLE_COUNT_1_BIT,
		        .tiling = VK_IMAGE_TILING_OPTIMAL,
		        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		};

		i->image = vk::image(device, image_info).release();
		i->memory = vk::device_memory(device, physical_device, i->image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).release();

		load_image(i->image, data, size, format, mipmaps);
	}
	catch (...)
	{
		cleanup_image(*i);
		throw;
	}

	return images.emplace_back(std::move(i)).get();
}

void scene_renderer::cleanup_buffer(scene_renderer::buffer & b)
{
	if (b.buffer)
		vkDestroyBuffer(device, b.buffer, nullptr);
	if (b.memory)
		vkFreeMemory(device, b.memory, nullptr);

	b.buffer = VK_NULL_HANDLE;
	b.memory = VK_NULL_HANDLE;
}

scene_renderer::buffer * scene_renderer::create_buffer(const void * data, size_t size, VkBufferUsageFlags usage)
{
	auto b = std::make_unique<buffer>();

	try
	{
		VkBufferCreateInfo buffer_info{
		        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		        .size = size,
		        .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
		        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
		};

		b->buffer = vk::buffer(device, buffer_info).release();
		b->memory = vk::device_memory(device, physical_device, b->buffer, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT).release();

		load_buffer(b->buffer, data, size);
	}
	catch (...)
	{
		cleanup_buffer(*b);
		throw;
	}

	return buffers.emplace_back(std::move(b)).get();
}

scene_renderer::model * scene_renderer::load_gltf(const std::string & filename)
{
	std::string err, warn;

	auto m = std::make_unique<model>();

	bool success = false;

	if (filename.ends_with(".glb"))
		success = gltf_loader.LoadBinaryFromFile(&m->gltf_model, &err, &warn, filename);
	else if (filename.ends_with(".gltf"))
		success = gltf_loader.LoadASCIIFromFile(&m->gltf_model, &err, &warn, filename);
	else
		err = "Wrong file extension, must be .gltf or .glb";

	if (warn != "")
		spdlog::warn("Loading {}: {}", filename, utils::trim(warn));

	if (err != "")
		spdlog::error("Loading {}: {}", filename, utils::trim(err));

	if (!success)
		throw std::runtime_error("GLTF error: " + utils::trim(err));

	for (tinygltf::Buffer & i: m->gltf_model.buffers)
	{
		m->buffers.push_back(create_buffer(i.data, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
	}

	std::vector<bool> srgb(m->gltf_model.images.size(), false);
	for (tinygltf::Material & i: m->gltf_model.materials)
	{
		if (i.emissiveTexture.index >= 0 && i.emissiveTexture.index < (int)srgb.size())
			srgb[i.emissiveTexture.index] = true;

		if (i.pbrMetallicRoughness.baseColorTexture.index >= 0 && i.pbrMetallicRoughness.baseColorTexture.index < (int)srgb.size())
			srgb[i.pbrMetallicRoughness.baseColorTexture.index] = true;
	}

	int n = 0;
	for (tinygltf::Image & i: m->gltf_model.images)
	{
		VkFormat format = gltf_to_vkformat(i.component, i.bits, i.pixel_type, srgb[n++]);
		m->images.push_back(create_image(i.image.data(), {(uint32_t)i.width, (uint32_t)i.height}, format));
	}

	return models.emplace_back(std::move(m)).get();
}
