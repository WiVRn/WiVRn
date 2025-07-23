/*
 * WiVRn VR streaming
 * Copyright (C) 2025  Patrick Nicolas <patricknicolas@laposte.net>
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
#include "application.h"

#include <iostream>

int main()
{
	auto apps = wivrn::list_applications();

	std::cout << "Parsed " << apps.size() << " applications" << std::endl;
	for (const auto & [k, app]: apps)
	{
		std::cout << "======================" << std::endl;
		std::cout << k << std::endl;
		for (const auto & [locale, name]: app.name)
		{
			std::cout << "Name";
			if (not locale.empty())
				std::cout << "[" << locale << "]";
			std::cout << "=" << name << std::endl;
		}
		std::cout << "Exec=" << app.exec << std::endl;
		if (app.path)
			std::cout << "Path=" << *app.path << std::endl;
	}
}
