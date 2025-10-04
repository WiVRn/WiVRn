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

#include "firewall.h"

#include "escape_sandbox.h"
#include "utils/flatpak.h"
#include "wivrn_config.h"

#include <QDebug>
#include <QStandardPaths>
#include <filesystem>
#include <string>

firewall::type_t firewall::detect_type()
{
	if (wivrn::is_flatpak())
	{
		if (not QStandardPaths::findExecutable(
		                "ufw",
		                QStringList({"/run/host/usr/sbin", "/run/host/usr/bin"}))
		                .isEmpty())
			return type_t::ufw;
	}
	else
	{
		if (not QStandardPaths::findExecutable("ufw").isEmpty())
			return type_t::ufw;
	}

	return type_t::none;
}

firewall::firewall() :
        type(detect_type())
{
}

const std::filesystem::path ufw_conf = "etc/ufw/applications.d/wivrn";

static bool need_setup_ufw()
{
	return not std::filesystem::exists(
	        (wivrn::is_flatpak() ? "/run/host" : "/") / ufw_conf);
}

bool firewall::needSetup()
{
	switch (type)
	{
		case type_t::none:
			return false;
		case type_t::ufw:
			return need_setup_ufw();
	}
	assert(false);
	__builtin_unreachable();
}

void firewall::doSetup()
{
	switch (type)
	{
		case type_t::none:
			return;
		case type_t::ufw: {
			pkexec = escape_sandbox("pkexec",
			                        "sh",
			                        "-c",
			                        "printf '[WiVRn]\\ntitle=WiVRn server\ndescription=WiVRn OpenXR streaming server\nports=" + std::to_string(wivrn::default_port) + "' > /" + ufw_conf.string() + " && ufw allow wivrn");
			pkexec->setProcessChannelMode(QProcess::MergedChannels);
			pkexec->start();

			QObject::connect(pkexec.get(), &QProcess::finished, this, [this](int exit_code, QProcess::ExitStatus exit_status) {
				if (exit_status != QProcess::NormalExit or exit_code)
					qWarning() << "ufw configuration exitec with status" << exit_status << "and code" << exit_code;
				needSetupChanged(needSetup());
				pkexec.release()->deleteLater();
			});
			return;
		}
	}
}
