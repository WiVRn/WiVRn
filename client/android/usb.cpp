/*
 * WiVRn VR streaming
 * Copyright (C) 2026  Patrick Nicolas <patricknicolas@laposte.net>
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
#include "jni.h"

#include <atomic>
#include <spdlog/spdlog.h>

std::atomic_bool link_properties_changed = false;

extern "C" void Java_org_meumeu_wivrn_NetworkInfoCallback_onAvailable(JNIEnv * env, jobject instance, jobject network)
{
	spdlog::info("NetworkInfoCallback_onAvailable");
}
extern "C" void Java_org_meumeu_wivrn_NetworkInfoCallback_onBlockedStatusChanged(JNIEnv * env, jobject instance, jobject network, jboolean blocked)
{
	spdlog::info("NetworkInfoCallback_onBlockedStatusChanged");
}
extern "C" void Java_org_meumeu_wivrn_NetworkInfoCallback_onCapabilitiesChanged(JNIEnv * env, jobject instance, jobject network, jobject caps)
{
	spdlog::info("NetworkInfoCallback_onCapabitilitiesChanged");
}
extern "C" void Java_org_meumeu_wivrn_NetworkInfoCallback_onLinkPropertiesChanged(JNIEnv * env, jobject instance, jobject network, jobject jprops)
{
	link_properties_changed = true;
}
extern "C" void Java_org_meumeu_wivrn_NetworkInfoCallback_onLosing(JNIEnv * env, jobject instance, jobject network, jint ms)
{
	spdlog::info("NetworkInfoCallback_onLosing");
}
extern "C" void Java_org_meumeu_wivrn_NetworkInfoCallback_onLost(JNIEnv * env, jobject instance, jobject network)
{
	spdlog::info("NetworkInfoCallback_onLost");
	link_properties_changed = true;
}
extern "C" void Java_org_meumeu_wivrn_NetworkInfoCallback_onUnavailable(JNIEnv * env, jobject instance)
{
	spdlog::info("NetworkInfoCallback_onUnavailable");
	application::get_config().usb_network = false;
}

bool usb_link_properties_changed()
{
	return link_properties_changed.exchange(false);
}
