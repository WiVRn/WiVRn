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

#include "intent.h"
#include "application.h"
#include <unordered_map>

static int next_request_code;
static std::unordered_map<int, std::function<void(int, intent &&)>> callbacks;

intent::intent(const char * intent_type) :
        obj(jni::new_object<"android/content/Intent">(jni::string{intent_type}))
{
}

intent::intent(jobject jni_obj) :
        obj(jni_obj)
{
}

void intent::set_type(const char * type)
{
	obj.call<jni::object<"android/content/Intent">>("setType", jni::string{type});
}

void intent::add_category(const char * category)
{
	obj.call<jni::object<"android/content/Intent">>("addCategory", jni::string{category});
}

void intent::start(std::function<void(int, intent &&)> callback)
{
	int request_code = next_request_code++;

	callbacks.emplace(request_code, std::move(callback));

	jni::object<""> activity(application::native_app()->activity->clazz);

	activity.call<void>("startActivityForResult", obj, jni::Int{request_code});
}

jni::object<"android/net/Uri"> intent::get_uri()
{
	return obj.call<jni::object<"android/net/Uri">>("getData");
}

extern "C" __attribute__((visibility("default"))) void Java_org_meumeu_wivrn_MainActivity_onActivityResult(JNIEnv * env, jobject instance, int request_code, int result_code, jobject data_obj)
{
	auto it = callbacks.find(request_code);
	if (it == callbacks.end())
		return;

	it->second(result_code, intent{data_obj});

	callbacks.erase(it);
}
