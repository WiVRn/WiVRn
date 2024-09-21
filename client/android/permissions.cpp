/*
 * WiVRn VR streaming
 * Copyright (C) 2024  Guillaume Meunier <guillaume.meunier@centraliens.net>
 * Copyright (C) 2024  Patrick Nicolas <patricknicolas@laposte.net>
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

#include "permissions.h"

#include "application.h"
#include "jnipp.h"

void request_permission(const char * permission, int requestCode)
{
	jni::object<""> act(application::native_app()->activity->clazz);
	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");

	jni::string jpermission(permission);
	auto result = ctx.call<jni::Int>("checkSelfPermission", jpermission);
	if (result != 0 /*PERMISSION_GRANTED*/)
	{
		spdlog::info("{} permission not granted, requesting it", permission);
		jni::array permissions(jpermission);
		act.call<void>("requestPermissions", permissions, jni::Int(requestCode));
	}
	else
	{
		spdlog::info("{} permission already granted", permission);
	}
}
