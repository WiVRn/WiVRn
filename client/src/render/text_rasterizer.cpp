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

#include "text_rasterizer.h"
#include <cassert>
#include <limits>
#include <system_error>
#include <vector>

#include <hb-ft.h>
#include <hb.h>

#include "utils/check.h"
#include "vk/buffer.h"

#ifdef XR_USE_PLATFORM_ANDROID
#include <android/font.h>
#include <android/font_matcher.h>
#include <android/system_fonts.h>
#endif

namespace
{
struct : std::error_category
{
	const char * name() const noexcept override
	{
		return "freetype";
	}

	std::string message(int condition) const override
	{
		return FT_Error_String(condition);
	}
} error_category;
} // namespace

std::error_category & freetype_error_category()
{
	return error_category;
}

// namespace {
//     const hb_tag_t KernTag = HB_TAG('k', 'e', 'r', 'n'); // kerning operations
//     const hb_tag_t LigaTag = HB_TAG('l', 'i', 'g', 'a'); // standard ligature substitution
//     const hb_tag_t CligTag = HB_TAG('c', 'l', 'i', 'g'); // contextual ligature substitution
//
//     static hb_feature_t LigatureOff = { LigaTag, 0, 0, std::numeric_limits<unsigned int>::max() };
//     static hb_feature_t LigatureOn  = { LigaTag, 1, 0, std::numeric_limits<unsigned int>::max() };
//     static hb_feature_t KerningOff  = { KernTag, 0, 0, std::numeric_limits<unsigned int>::max() };
//     static hb_feature_t KerningOn   = { KernTag, 1, 0, std::numeric_limits<unsigned int>::max() };
//     static hb_feature_t CligOff     = { CligTag, 0, 0, std::numeric_limits<unsigned int>::max() };
//     static hb_feature_t CligOn      = { CligTag, 1, 0, std::numeric_limits<unsigned int>::max() };
// }

text_rasterizer::text_rasterizer(VkDevice device, VkPhysicalDevice physical_device, VkCommandPool command_pool, VkQueue queue) :
        device(device), physical_device(physical_device), command_pool(command_pool), queue(queue)
{
	std::string font_filename;

#ifdef XR_USE_PLATFORM_ANDROID
	{
		std::u16string ws = u"hello";
		AFontMatcher * font_matcher = AFontMatcher_create();

		AFontMatcher_setFamilyVariant(font_matcher, AFAMILY_VARIANT_DEFAULT);
		AFontMatcher_setLocales(font_matcher, "fr-FR,en-GB");
		AFontMatcher_setStyle(font_matcher, AFONT_WEIGHT_NORMAL, false);

		AFont * font = AFontMatcher_match(font_matcher, "sans-serif", (uint16_t *)ws.c_str(), ws.size(), nullptr);

		font_filename = AFont_getFontFilePath(font);

		AFont_close(font);
		AFontMatcher_destroy(font_matcher);
	}
#else
	font_filename = "/usr/share/fonts/TTF/DejaVuSans.ttf";
#endif
	try
	{
		FT_Error err = FT_Init_FreeType(&freetype);
		if (err)
			throw std::system_error(err, error_category);

		err = FT_New_Face(freetype, font_filename.c_str(), 0, &face);
		if (err)
			throw std::system_error(err, error_category);

		err = FT_Set_Char_Size(face, 0, 200 * 64, 72, 72);
		if (err)
			throw std::system_error(err, error_category);

		font = hb_ft_font_create(face, nullptr);

		buffer = hb_buffer_create();

		if (!hb_buffer_allocation_successful(buffer))
			throw std::runtime_error("hb_buffer_allocation_successful returned false");
	}
	catch (...)
	{
		if (buffer)
			hb_buffer_destroy(buffer);

		if (font)
			hb_font_destroy(font);

		if (freetype)
			FT_Done_FreeType(freetype);

		throw;
	}

	VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	CHECK_VK(vkCreateFence(device, &fence_info, nullptr, &fence));
}

text_rasterizer::~text_rasterizer()
{
	if (device && fence)
		vkDestroyFence(device, fence, nullptr);

	hb_buffer_destroy(buffer);
	hb_font_destroy(font);
	FT_Done_FreeType(freetype);
}

text text_rasterizer::render(std::string_view s)
{
	hb_buffer_reset(buffer);

	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	hb_buffer_set_script(buffer, HB_SCRIPT_LATIN);
	hb_buffer_set_language(buffer, hb_language_from_string(s.data(), s.size()));

	hb_buffer_add_utf8(buffer, s.data(), s.size(), 0, s.size());

	// std::vector<hb_feature_t> features;
	// hb_shape(font, buffer, features.data(), features.size());

	hb_shape(font, buffer, nullptr, 0);

	unsigned int glyphCount;
	hb_glyph_info_t * glyphInfo = hb_buffer_get_glyph_infos(buffer, &glyphCount);
	hb_glyph_position_t * glyphPos = hb_buffer_get_glyph_positions(buffer, &glyphCount);

	int x_min = std::numeric_limits<int>::max();
	int x_max = std::numeric_limits<int>::min();
	int y_min = std::numeric_limits<int>::max();
	int y_max = std::numeric_limits<int>::min();

	FT_Int32 flags = FT_LOAD_DEFAULT;

	std::vector<uint8_t> rendered_text;

	for (int pass = 0; pass < 2; pass++)
	{
		FT_F26Dot6 x = 0;
		FT_F26Dot6 y = 0;
		for (size_t i = 0; i < glyphCount; ++i)
		{
			FT_Error err = FT_Load_Glyph(face, glyphInfo[i].codepoint, flags);
			if (err)
				throw std::system_error(err, error_category);

			err = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
			if (err)
				throw std::system_error(err, error_category);

			FT_GlyphSlot slot = face->glyph;
			FT_Bitmap bitmap = face->glyph->bitmap;

			int x0 = (x + glyphPos[i].x_offset) / 64 + slot->bitmap_left;
			int y0 = (y + glyphPos[i].y_offset) / 64 + slot->bitmap_top;
			int x1 = x0 + bitmap.width;
			int y1 = y0 - bitmap.rows;

			assert(x1 >= x0);
			assert(y1 <= y0);

			switch (pass)
			{
				case 0:
					x_min = std::min(x_min, x0);
					x_max = std::max(x_max, x1);
					y_min = std::min(y_min, y1);
					y_max = std::max(y_max, y0);
					break;

				case 1: {
					uint8_t * src = bitmap.buffer;
					for (unsigned int iy = 0; iy < bitmap.rows; iy++)
					{
						uint8_t * dst = rendered_text.data() +
						                (y_max - y0 + iy) * (x_max - x_min) + (x0 - x_min);

						for (unsigned int ix = 0; ix < bitmap.width; ix++)
						{
							*dst = std::max(*dst, *src);
							dst++;
							src++;
						}
					}
				}
				break;
			}

			x += glyphPos[i].x_advance;
			y += glyphPos[i].y_advance;
		}

		if (pass == 0)
		{
			assert(x_max > x_min);
			assert(y_max > y_min);
			rendered_text.resize((x_max - x_min) * (y_max - y_min), 0);
		}
	}
#ifdef TEST
	return {.bitmap = std::move(rendered_text),
	        .size = {
	                .width = uint32_t(x_max - x_min),
	                .height = uint32_t(y_max - y_min),
	        }};
#else

	VkImageCreateInfo image_info{
	        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
	        .imageType = VK_IMAGE_TYPE_2D,
	        .format = text::format,
	        .extent = {
	                .width = uint32_t(x_max - x_min),
	                .height = uint32_t(y_max - y_min),
	                .depth = 1},
	        .mipLevels = 1,
	        .arrayLayers = 1,
	        .samples = VK_SAMPLE_COUNT_1_BIT,
	        .tiling = VK_IMAGE_TILING_OPTIMAL,
	        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
	vk::image image(device, image_info);
	vk::device_memory memory(device, physical_device, image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	VkBufferCreateInfo buffer_info{
	        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
	        .size = rendered_text.size(),
	        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	vk::buffer staging_buffer(device, buffer_info);
	vk::device_memory staging_memory(device, physical_device, staging_buffer, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	staging_memory.map_memory();
	memcpy(staging_memory.data(), rendered_text.data(), rendered_text.size());

	VkCommandBufferAllocateInfo cmdbufinfo{
	        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
	        .commandPool = command_pool,
	        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
	        .commandBufferCount = 1,
	};
	VkCommandBuffer cmdbuf;
	CHECK_VK(vkAllocateCommandBuffers(device, &cmdbufinfo, &cmdbuf));

	VkCommandBufferBeginInfo cmdbufbegininfo{
	        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	vkBeginCommandBuffer(cmdbuf, &cmdbufbegininfo);

	VkImageMemoryBarrier barrier{
	        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
	        .srcAccessMask = 0,
	        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
	        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
	        .image = image,
	        .subresourceRange =
	                {
	                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	                        .baseMipLevel = 0,
	                        .levelCount = 1,
	                        .baseArrayLayer = 0,
	                        .layerCount = 1,
	                },
	};
	vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	VkBufferImageCopy copy_info{.bufferOffset = 0,
	                            .bufferRowLength = 0,
	                            .bufferImageHeight = 0,
	                            .imageSubresource =
	                                    VkImageSubresourceLayers{
	                                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
	                                            .mipLevel = 0,
	                                            .baseArrayLayer = 0,
	                                            .layerCount = 1,
	                                    },
	                            .imageOffset = {0, 0, 0},
	                            .imageExtent = image_info.extent};

	vkCmdCopyBufferToImage(cmdbuf, staging_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy_info);

	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = text::layout;
	barrier.subresourceRange.baseMipLevel = 0;
	vkCmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

	CHECK_VK(vkEndCommandBuffer(cmdbuf));

	VkSubmitInfo submit_info{};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.commandBufferCount = 1;
	submit_info.pCommandBuffers = &cmdbuf;
	CHECK_VK(vkQueueSubmit(queue, 1, &submit_info, fence));

	CHECK_VK(vkWaitForFences(device, 1, &fence, VK_TRUE, -1));
	CHECK_VK(vkResetFences(device, 1, &fence));

	return {
	        .image = std::move(image),
	        .memory = std::move(memory),
	        .size = {image_info.extent.width, image_info.extent.height}};
#endif
}

#ifdef TEST
#include <fstream>

int main(int argc, char ** argv)
{
	std::string s;
	for (int i = 1; i < argc; i++)
	{
		s = s + argv[i];
	}

	text_renderer r(VK_NULL_HANDLE, VK_NULL_HANDLE);

	auto text = r.render(s);

	std::ofstream f("text.pgm");

	f << "P5 " << text.size.width << " " << text.size.height << " 255 ";
	f.write((char *)text.bitmap.data(), text.bitmap.size());
}
#endif
