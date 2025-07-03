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

#include "scene_components.h"
#include <entt/entt.hpp>
#include <spdlog/spdlog.h>

entt::entity find_node_by_name(entt::registry & scene, std::string_view name)
{
	for (auto && [entity, node]: scene.view<components::node>().each())
	{
		if (node.name == name)
			return entity;
	}

	// TODO custom exception
	throw std::runtime_error("Node \"" + (std::string)name + "\" not found");
}

entt::entity find_node_by_name(entt::registry & scene, std::string_view name, entt::entity parent)
{
	for (auto && [entity, node]: scene.view<components::node>().each())
	{
		if (node.name != name)
			continue;

		auto current_parent = node.parent;

		do
		{
			if (current_parent == parent)
				return entity;
			assert(current_parent != scene.get<components::node>(current_parent).parent);
			current_parent = scene.get<components::node>(current_parent).parent;

		} while (current_parent != entt::null);
	}

	// TODO custom exception
	throw std::runtime_error("Node \"" + (std::string)name + "\" not found");
}

std::string get_node_name(const entt::registry & scene, entt::entity entity)
{
	auto name_parent = [&](entt::entity entity) -> std::pair<std::string, entt::entity> {
		const auto & node = scene.get<components::node>(entity);
		if (node.name == "")
			return {std::to_string((int)entity), node.parent};
		else
			return {node.name, node.parent};
	};

	std::string name;
	std::tie(name, entity) = name_parent(entity);

	while (entity != entt::null)
	{
		std::string tmp;
		std::tie(tmp, entity) = name_parent(entity);
		name = tmp + "/" + name;
	}

	return name;
}
