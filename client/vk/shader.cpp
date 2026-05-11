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
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>
#include <spirv_reflect.h>
#include <vulkan/vulkan_to_string.hpp>

template <typename T, typename F>
std::vector<T *> enumerate(F && f, SpvReflectShaderModule & reflected_shader_module)
{
	uint32_t count;
	[[maybe_unused]] SpvReflectResult result = f(&reflected_shader_module, &count, nullptr);
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	std::vector<T *> data(count);
	result = f(&reflected_shader_module, &count, data.data());
	assert(result == SPV_REFLECT_RESULT_SUCCESS);

	return data;
}

shader_loader::shader_loader(vk::raii::Device & device) :
        device(device)
{}

std::shared_ptr<shader> shader_loader::operator()(std::span<const uint32_t> spirv)
{
	vk::ShaderModuleCreateInfo create_info{
	        .codeSize = spirv.size_bytes(),
	        .pCode = spirv.data(),
	};

	std::vector<shader::input> inputs;
	std::vector<shader::specialization_constant> specialization_constants;

	SpvReflectShaderModule reflect;
	if (auto result = spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NONE, spirv.size_bytes(), spirv.data(), &reflect); result == SPV_REFLECT_RESULT_SUCCESS)
	{
		for (SpvReflectInterfaceVariable * variable: enumerate<SpvReflectInterfaceVariable>(spvReflectEnumerateInputVariables, reflect))
		{
			int array_size = 1;
			for (int i = 0; i < variable->array.dims_count; i++)
				array_size *= variable->array.dims[i];

			if (variable->built_in == -1)
				inputs.emplace_back(
				        variable->location,
				        variable->name,
				        (vk::Format)variable->format,
				        array_size);
		}

		for (SpvReflectSpecializationConstant * variable: enumerate<SpvReflectSpecializationConstant>(spvReflectEnumerateSpecializationConstants, reflect))
			specialization_constants.emplace_back(variable->constant_id, variable->name);

		for (SpvReflectDescriptorSet * variable: enumerate<SpvReflectDescriptorSet>(spvReflectEnumerateDescriptorSets, reflect))
		{
		}

		for (SpvReflectDescriptorBinding * variable: enumerate<SpvReflectDescriptorBinding>(spvReflectEnumerateDescriptorBindings, reflect))
		{
		}

		spvReflectDestroyShaderModule(&reflect);
	}
	else
		spdlog::error("Cannot reflect shader: {}", magic_enum::enum_name(result));

	return std::make_shared<shader>(vk::raii::ShaderModule{device, create_info}, inputs, specialization_constants);
}

std::shared_ptr<shader> shader_loader::operator()(const std::string & name)
{
	std::span<const uint32_t> spirv;
	try
	{
		spirv = shaders.at(name);
	}
	catch (std::exception & e)
	{
		spdlog::error("Cannot load shader {}: {}", name, e.what());
		throw;
	}

	return (*this)(shaders.at(name));
}
