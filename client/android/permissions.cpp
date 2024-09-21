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

#include <map>
#include <mutex>
#include <set>

static std::mutex permission_callbacks_mutex;
static std::map<int, std::pair<std::string, std::function<void(bool)>>> permission_callbacks;
static int next_permission_request_id;

static bool check_permission(jni::object<jni::details::string_literal<24>{"android/content/Context"}> & ctx, jni::string & permission)
{
	auto result = ctx.call<jni::Int>("checkSelfPermission", permission);
	return result == 0;
}

bool check_permission(const char * permission)
{
	if (not permission)
		return true;

	// Cache positive results to avoid useless jni calls
	static std::set<const char *> permissions;
	static std::mutex mutex;
	std::lock_guard lock(mutex);
	if (permissions.contains(permission))
		return true;

	jni::object<""> act(application::native_app()->activity->clazz);
	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");

	jni::string jpermission(permission);
	bool res = check_permission(ctx, jpermission);
	if (res)
		permissions.insert(permission);
	return res;
}

void request_permission(const char * permission, std::function<void(bool)> callback)
{
	if (not permission)
	{
		callback(true);
		return;
	}

	jni::object<""> act(application::native_app()->activity->clazz);
	auto app = act.call<jni::object<"android/app/Application">>("getApplication");
	auto ctx = app.call<jni::object<"android/content/Context">>("getApplicationContext");

	jni::string jpermission(permission);
	if (check_permission(ctx, jpermission))
	{
		spdlog::info("{} permission already granted", permission);
		callback(true);
	}
	else
	{
		spdlog::info("{} permission not granted, requesting it", permission);
		jni::array permissions(jpermission);

		int request_code;
		{
			std::lock_guard lock(permission_callbacks_mutex);
			request_code = ++next_permission_request_id;
			permission_callbacks.emplace(request_code, std::make_pair(permission, callback));
		}

		act.call<void>("requestPermissions", permissions, jni::Int(request_code));
	}
}

extern "C" __attribute__((visibility("default"))) void Java_org_meumeu_wivrn_MainActivity_onRequestPermissionsResult(
        JNIEnv * env,
        jobject instance,
        int requestCode,
        jobjectArray permissions,
        jintArray grantResults);

void Java_org_meumeu_wivrn_MainActivity_onRequestPermissionsResult(
        JNIEnv * env,
        jobject instance,
        int request_code,
        jobjectArray permissions,
        jintArray grant_results)
{
	std::pair<std::string, std::function<void(bool)>> callback;
	{
		std::lock_guard lock(permission_callbacks_mutex);
		auto it = permission_callbacks.find(request_code);
		if (it == permission_callbacks.end())
		{
			spdlog::info("Ignoring unexpected request code");
			return;
		}

		callback = it->second;
		permission_callbacks.erase(it);
	}

	size_t nb_permissions = std::min(env->GetArrayLength(permissions), env->GetArrayLength(grant_results));

	jint * results = env->GetIntArrayElements(grant_results, nullptr);

	for (size_t i = 0; i < nb_permissions; ++i)
	{
		jstring string = (jstring)(env->GetObjectArrayElement(permissions, i));
		const char * permission = env->GetStringUTFChars(string, nullptr);
		spdlog::info("Permission {} {}", permission, results[i] ? "denied" : "granted");

		if (permission == callback.first)
		{
			callback.second(results[i] == 0);
		}

		env->ReleaseStringUTFChars(string, permission);
	}

	env->ReleaseIntArrayElements(grant_results, results, 0);
}
