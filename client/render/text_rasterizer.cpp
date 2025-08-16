/*
 * WiVRn VR streaming
 * Copyright (C) 2022-2024 Guillaume Meunier <guillaume.meunier@centraliens.net>
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
#include "application.h"
#include "vk/allocation.h"
#include <cassert>
#include <limits>
#include <system_error>
#include <vector>

#include <hb-ft.h>
#include <hb.h>

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_raii.hpp>

#ifdef __ANDROID__
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

text_rasterizer::text_rasterizer(vk::raii::Device & device, vk::raii::PhysicalDevice & physical_device, vk::raii::CommandPool & command_pool, vk::raii::Queue & queue) :
        device(device),
        physical_device(physical_device),
        command_pool(command_pool),
        queue(queue),
        fence(device, vk::FenceCreateInfo{})
{
	std::string font_filename;

#ifdef __ANDROID__
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
}

image_allocation text_rasterizer::create_image(vk::Extent2D size)
{
	return {device,
	        vk::ImageCreateInfo{
	                .imageType = vk::ImageType::e2D,
	                .format = text::format,
	                .extent = {
	                        .width = size.width,
	                        .height = size.height,
	                        .depth = 1,
	                },
	                .mipLevels = 1,
	                .arrayLayers = 1,
	                .samples = vk::SampleCountFlagBits::e1,
	                .tiling = vk::ImageTiling::eOptimal,
	                .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
	                .sharingMode = vk::SharingMode::eExclusive,
	                .initialLayout = vk::ImageLayout::eUndefined,
	        },
	        VmaAllocationCreateInfo{
	                .flags = 0,
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        },
	        "text_rasterizer image"};
}

buffer_allocation text_rasterizer::create_buffer(size_t size)
{
	return {device,
	        vk::BufferCreateInfo{
	                .size = size,
	                .usage = vk::BufferUsageFlagBits::eTransferSrc,
	        },
	        VmaAllocationCreateInfo{
	                .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
	                .usage = VMA_MEMORY_USAGE_AUTO,
	        },
	        "text_rasterizer buffer"};
}

text_rasterizer::~text_rasterizer()
{
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

	text return_value;

	return_value.size.width = x_max - x_min;
	return_value.size.height = y_max - y_min;
	return_value.image = create_image(return_value.size);

	buffer_allocation staging_buffer = create_buffer(rendered_text.size());

	void * mapped = staging_buffer.map();
	memcpy(mapped, rendered_text.data(), rendered_text.size());

	vk::raii::CommandBuffers cmdbufs(device, {
	                                                 .commandPool = *command_pool,
	                                                 .level = vk::CommandBufferLevel::ePrimary,
	                                                 .commandBufferCount = 1,
	                                         });
	vk::raii::CommandBuffer & cmdbuf = cmdbufs[0];
	application::set_debug_reports_name(*cmdbuf, "text_rasterizer command buffer");

	cmdbuf.begin({});

	vk::ImageMemoryBarrier barrier{
	        .srcAccessMask = vk::AccessFlagBits::eNone,
	        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
	        .oldLayout = vk::ImageLayout::eUndefined,
	        .newLayout = vk::ImageLayout::eTransferDstOptimal,
	        .image = return_value.image,
	        .subresourceRange = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .baseMipLevel = 0,
	                .levelCount = 1,
	                .baseArrayLayer = 0,
	                .layerCount = 1,
	        },
	};
	cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlags{}, {}, {}, barrier);

	vk::BufferImageCopy copy_info{
	        .bufferOffset = 0,
	        .bufferRowLength = 0,
	        .bufferImageHeight = 0,
	        .imageSubresource = {
	                .aspectMask = vk::ImageAspectFlagBits::eColor,
	                .mipLevel = 0,
	                .baseArrayLayer = 0,
	                .layerCount = 1,
	        },
	        .imageOffset = {0, 0, 0},
	        .imageExtent = {
	                .width = uint32_t(x_max - x_min),
	                .height = uint32_t(y_max - y_min),
	                .depth = 1,
	        },
	};
	cmdbuf.copyBufferToImage(staging_buffer, return_value.image, vk::ImageLayout::eTransferDstOptimal, copy_info);

	barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
	barrier.dstAccessMask = vk::AccessFlagBits::eMemoryRead;
	barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
	barrier.newLayout = text::layout;
	barrier.subresourceRange.baseMipLevel = 0;
	cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe, vk::DependencyFlags{}, {}, {}, barrier);

	cmdbuf.end();

	vk::SubmitInfo submit_info;
	submit_info.setCommandBuffers(*cmdbuf);
	queue.submit(submit_info, *fence);
	if (device.waitForFences(*fence, VK_TRUE, UINT64_MAX) == vk::Result::eTimeout)
		throw std::runtime_error("Vulkan fence timeout");
	device.resetFences(*fence);

	return return_value;
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
