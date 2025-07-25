/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
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

#include "apps.h"

#include "application.h"

#include <ranges>

Apps::Apps(QObject * parent)
{
	auto apps = wivrn::list_applications();

	for (const auto & [id, app]: apps)
	{
		m_apps.push_back(
		        vrApp(
		                QString::fromStdString(app.name.at("")),
		                QString::fromStdString(app.exec)));
	}

	std::ranges::sort(m_apps, [](const vrApp & a, const vrApp & b) {
		return a.name() < b.name();
	});
}

#include "moc_apps.cpp"
