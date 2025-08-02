/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "load_icon.h"

#include <cairo.h>
#include <fstream>
#include <librsvg/rsvg.h>
#include <limits>
#include <png.h>
#include <span>
#include <unordered_map>

namespace
{
std::vector<std::byte> load_file(const std::filesystem::path & filename)
{
	std::ifstream file(filename, std::ios::binary | std::ios::ate);
	file.exceptions(std::ios_base::badbit | std::ios_base::failbit);

	size_t size = file.tellg();
	file.seekg(0);

	std::vector<std::byte> bytes(size);

	file.read(reinterpret_cast<char *>(bytes.data()), size);
	return bytes;
}

template <typename T>
        requires std::is_integral_v<T>
T read(std::span<std::byte> & buffer)
{
	if (sizeof(T) > buffer.size())
		throw std::runtime_error{"File truncated"};

	T value;
	memcpy(&value, buffer.data(), sizeof(T));
	buffer = buffer.subspan(sizeof(T));

	static_assert(std::endian::native == std::endian::big or std::endian::native == std::endian::little);

	if constexpr (std::endian::native == std::endian::big)
		return std::byteswap(value);
	else
		return value;
}

std::span<std::byte> read(std::span<std::byte> & buffer, size_t size)
{
	if (size > buffer.size())
		throw std::runtime_error{"File truncated"};

	std::span<std::byte> value{buffer.data(), size};
	buffer = buffer.subspan(size);
	return value;
}

std::vector<std::byte> load_svg(const std::filesystem::path & filename, int size)
{
	// TODO check if aspect ratio needs to be handled, dpi

	struct deleter
	{
		void operator()(GFile * ptr)
		{
			g_object_unref(ptr);
		}

		void operator()(RsvgHandle * ptr)
		{
			g_object_unref(ptr);
		}

		void operator()(cairo_surface_t * ptr)
		{
			cairo_surface_destroy(ptr);
		}

		void operator()(cairo_t * ptr)
		{
			cairo_destroy(ptr);
		}
	};

	using GFile_ptr = std::unique_ptr<GFile, deleter>;
	using RsvgHandle_ptr = std::unique_ptr<RsvgHandle, deleter>;
	using cairo_surface_t_ptr = std::unique_ptr<cairo_surface_t, deleter>;
	using cairo_t_ptr = std::unique_ptr<cairo_t, deleter>;

	GError * error{};
	GFile_ptr file{g_file_new_for_path(filename.c_str())};

	RsvgHandle_ptr handle{rsvg_handle_new_from_gfile_sync(file.get(), RSVG_HANDLE_FLAGS_NONE, nullptr, &error)};

	// Create a Cairo image surface and a rendering context for it
	cairo_surface_t_ptr surface{cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size)};
	cairo_t_ptr cr{cairo_create(surface.get())};

	// Set the dots-per-inch
	rsvg_handle_set_dpi(handle.get(), 96.0);

	// Render the handle scaled proportionally into that whole surface
	RsvgRectangle viewport = {
	        .x = 0.0,
	        .y = 0.0,
	        .width = (double)size,
	        .height = (double)size,
	};

	if (!rsvg_handle_render_document(handle.get(), cr.get(), &viewport, &error))
	{
		g_printerr("could not render: %s", error->message);
		return {};
	}

	// Write a PNG file
	std::vector<std::byte> png;
	auto cb = [](void * closure, const unsigned char * data, unsigned int length) -> cairo_status_t {
		std::vector<std::byte> & png = *reinterpret_cast<std::vector<std::byte> *>(closure);

		size_t old_size = png.size();
		png.resize(old_size + length);
		memcpy(&png[old_size], data, length);

		return CAIRO_STATUS_SUCCESS;
	};

	if (cairo_surface_write_to_png_stream(surface.get(), cb, &png) != CAIRO_STATUS_SUCCESS)
	{
		g_printerr("could not write output file");
		return {};
	}

	return png;
}

// See https://gitlab.gnome.org/GNOME/gimp/-/tree/master/plug-ins/file-ico?ref_type=heads

struct ico_file_entry
{
	int width;  // Width of icon in pixels
	int height; // Height of icon in pixels
	int bpp;    // 1, 4, 8, 16, 24 or 32 bits per pixel
	std::span<std::byte> data;
};

int rowstride(int width, int bpp)
{
	switch (bpp)
	{
		case 1:
			if ((width % 32) == 0)
				return width / 8;
			else
				return 4 * (width / 32 + 1);

			// case 4:
			// 	if ((width % 8) == 0)
			// 		return width / 2;
			// 	else
			// 		return 4 * (width / 8 + 1);
			//
			// case 8:
			// 	if ((width % 4) == 0)
			// 		return width;
			// 	else
			// 		return 4 * (width / 4 + 1);

		case 24:
			if (((width * 3) % 4) == 0)
				return width * 3;
			else
				return 4 * (width * 3 / 4 + 1);

		case 32:
			return width * 4;

		default:
			return 0;
	}
}

std::vector<std::byte> load_ico(const std::filesystem::path & filename, int size, int index = -1)
{
	const uint32_t ICO_PNG_MAGIC = 0x474e5089;

	try
	{
		auto ico = load_file(filename);

		std::span header{ico};

		auto reserved = read<uint16_t>(header);
		auto resource_type = read<uint16_t>(header);
		auto icon_count = read<uint16_t>(header);

		if (reserved != 0 or resource_type != 1)
			return {};

		std::vector<ico_file_entry> entries;
		int max_bpp = 0;

		for (int i = 0; i < icon_count; i++)
		{
			ico_file_entry entry;
			entry.width = read<uint8_t>(header);
			entry.height = read<uint8_t>(header);
			auto num_colors = read<uint8_t>(header);
			auto reserved = read<uint8_t>(header); // reserved
			auto planes = read<uint16_t>(header);
			entry.bpp = read<uint16_t>(header);
			auto size = read<uint32_t>(header);
			auto offset = read<uint32_t>(header);

			entry.data = std::span{ico}.subspan(offset, size);

			if (entry.width == 0)
				entry.width = 256;
			if (entry.height == 0)
				entry.height = 256;
			if (reserved != 0)
				return {};
			if (planes != 0 and planes != 1)
				return {};

			// Ignore 16bpp and less
			if (entry.bpp < 24)
				continue;

			entries.push_back(entry);

			max_bpp = std::max(max_bpp, entry.bpp);
		}

		const ico_file_entry & best_entry = index >= 0 ? entries.at(index) : [&]() {
			const ico_file_entry * best_entry;

			// Look for the smallest multiple of the requested size
			int criterion = std::numeric_limits<int>::max();

			for (const auto & entry: entries)
			{
				if (entry.bpp != max_bpp)
					continue;

				if (entry.width % size != 0 or entry.height % size != 0)
					continue;

				if (std::min(entry.width, entry.height) / size < criterion)
				{
					criterion = std::min(entry.width, entry.height);
					best_entry = &entry;
				}
			}
			if (best_entry)
				return *best_entry;

			// No multiple found: pick the largest icon
			criterion = 0;

			for (const auto & entry: entries)
			{
				if (entry.bpp != max_bpp)
					continue;

				if (entry.width * entry.height > criterion)
				{
					criterion = entry.width * entry.height;
					best_entry = &entry;
				}
			}
			return *best_entry;
		}();

		auto data = best_entry.data;
		auto magic = read<uint32_t>(data);

		if (magic == ICO_PNG_MAGIC)
		{
			// PNG file
			std::vector<std::byte> png_data;
			png_data.insert(png_data.end(), data.begin(), data.end());
			return png_data;
		}
		else if (magic == 40)
		{
			auto width = read<uint32_t>(data);       // Width of image in pixels
			auto height = read<uint32_t>(data);      // Height of image in pixels
			auto planes = read<uint16_t>(data);      // Must be 1
			auto bpp = read<uint16_t>(data);         // 1, 4, 8, 16, 24, 32
			auto compression = read<uint32_t>(data); // Must be 0 for icons
			auto image_size = read<uint32_t>(data);  // Size of image (without this header)
			auto x_res = read<uint32_t>(data);
			auto y_res = read<uint32_t>(data);
			auto used_colors = read<uint32_t>(data);
			auto important_colors = read<uint32_t>(data);

			if (planes != 1 or compression != 0)
				return {};

			if (bpp != 1 and
			    bpp != 4 and
			    bpp != 8 and
			    bpp != 16 and
			    bpp != 24 and
			    bpp != 32)
				return {};

			auto w = width;
			auto h = height / 2;

			// Don't bother supporting paletted icons
			// std::vector<uint32_t> palette;
			//
			// if (bpp <= 8)
			// {
			// 	// Load the palette
			// 	if (used_colors == 0)
			// 		used_colors = 1 << bpp;
			//
			// 	palette.resize(used_colors);
			// 	for (int i = 0; i < palette.size(); i++)
			// 		palette[i] = read<uint32_t>(data);
			// }

			size_t xor_stride = rowstride(w, bpp);
			size_t and_stride = rowstride(w, 1);
			size_t dst_stride = 4 * w;

			std::span<std::byte> xor_map = read(data, xor_stride * h);
			std::span<std::byte> and_map = read(data, and_stride * h);

			std::vector<uint8_t> dest_buffer;
			dest_buffer.resize(h * dst_stride);

			switch (bpp)
			{
				case 24:
					for (int y = 0; y < h; y++)
					{
						uint8_t * src_xor_row = (uint8_t *)xor_map.data() + xor_stride * y;
						uint8_t * src_and_row = (uint8_t *)and_map.data() + and_stride * y;
						uint8_t * dst_row = dest_buffer.data() + dst_stride * (h - 1 - y);

						for (int x = 0, i = 0; x < w; x++)
						{
							uint8_t b = *src_xor_row++; // Red
							uint8_t g = *src_xor_row++; // Green
							uint8_t r = *src_xor_row++; // Blue

							// Get alpha from AND mask (0: opaque => 0xff, 1: transparent => 0)
							uint8_t a = src_and_row[x / 8] & (1 << (7 - x % 8)) ? 0 : 0xff;

							*dst_row++ = b;
							*dst_row++ = g;
							*dst_row++ = r;
							*dst_row++ = a;
						}
					}

				case 32:
					for (int y = 0; y < h; y++)
					{
						uint8_t * src_xor_row = (uint8_t *)xor_map.data() + xor_stride * y;
						uint8_t * dst_row = dest_buffer.data() + dst_stride * (h - 1 - y);

						memcpy(dst_row, src_xor_row, dst_stride);
					}
					break;
			}

			png_image png{
			        .version = PNG_IMAGE_VERSION,
			        .width = w,
			        .height = h,
			        .format = PNG_FORMAT_BGRA,
			};

			std::vector<std::byte> png_data{65536};
			png_alloc_size_t png_size = png_data.size();
			png_image_write_to_memory(&png, png_data.data(), &png_size, false /* convert_to_8_bit*/, dest_buffer.data(), w * 4, nullptr);

			if (png_size > png_data.size())
			{
				png_data.resize(png_size);
				png_image_write_to_memory(&png, png_data.data(), &png_size, false /* convert_to_8_bit*/, dest_buffer.data(), w * 4, nullptr);
			}
			else
				png_data.resize(png_size);

			return png_data;
		}
	}
	catch (std::exception & e)
	{
	}

	return {};
}

std::unordered_map<std::filesystem::path, std::vector<std::byte>> icon_cache;

} // namespace

const std::vector<std::byte> & wivrn::load_icon(const std::filesystem::path & filename, int size)
{
	auto it = icon_cache.find(filename);
	if (it != icon_cache.end())
		return it->second;

	std::vector<std::byte> icon;

	if (filename.extension() == ".svg")
	{
		icon = load_svg(filename, size);
	}
	else if (filename.extension() == ".ico")
	{
		// Steam icon (Windows)
		icon = load_ico(filename, size);
	}
	else if (filename.extension() == ".zip")
	{
		// Steam icon (Linux)
		// TODO
	}
	else
	{
		// TODO read arbitrary format / rewrite as png
		icon = load_file(filename);
	}

	return icon_cache.emplace(filename, std::move(icon)).first->second;
}
