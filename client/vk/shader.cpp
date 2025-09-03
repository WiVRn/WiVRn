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

#include "shader.h"
#include "wivrn_shaders.h"

vk::raii::ShaderModule load_shader(vk::raii::Device & device, const std::vector<uint32_t> & spirv)
{
	vk::ShaderModuleCreateInfo create_info{
	        .codeSize = spirv.size() * sizeof(uint32_t),
	        .pCode = spirv.data(),
	};

	return vk::raii::ShaderModule{device, create_info};
}

vk::raii::ShaderModule load_shader(vk::raii::Device & device, const std::string & name)
{
	return load_shader(device, shaders.at(name));
}
