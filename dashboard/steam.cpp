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

#include "steam.h"

#include "escape_sandbox.h"
#include "utils/flatpak.h"
#include "utils/ini.h"
#include "utils/strings.h"

#include <QDebug>
#include <algorithm>
#include <filesystem>

bool steam::snap() const
{
	if (wivrn::is_flatpak())
	{
		auto test = escape_sandbox("test", "-f", "/snap/bin/steam");
		test->setProcessChannelMode(QProcess::MergedChannels);
		test->start();
		if (test->waitForFinished())
			return test->exitCode() == 0;
		return false;
	}

	return std::filesystem::exists("/snap/bin/steam");
}

static QString get_flatpak_scope()
{
	for (const QString scope: {"--user", "--system"})
	{
		auto proc = escape_sandbox("flatpak", scope, "info", "com.valvesoftware.Steam");
		proc->setProcessChannelMode(QProcess::MergedChannels);
		proc->start();
		if (proc->waitForFinished() and proc->exitCode() == 0)
			return scope;
	}
	return "";
}

static std::filesystem::path find_dir(const std::filesystem::path & d, const std::filesystem::path & needle)
{
	for (auto copy = d; not copy.empty(); copy = copy.parent_path())
	{
		if (copy.filename() == needle)
			return copy;
	}
	return d;
}

static std::optional<std::string> wivrn_app_path()
{
	if (auto app_path = wivrn::flatpak_key(wivrn::flatpak::section::instance, "app-path"))
	{
		if (app_path->starts_with("/var"))
			return find_dir(*app_path, "io.github.wivrn.wivrn");
		// assume it's in home
		return "xdg-data/flatpak/app/io.github.wivrn.wivrn";
	}
	return {};
}

bool steam::flatpakNeedPerm() const
{
	auto scope = get_flatpak_scope();
	if (scope.isEmpty())
		return false;
	auto proc = escape_sandbox("flatpak", scope, "override", "--show", "com.valvesoftware.Steam");
	proc->setProcessChannelMode(QProcess::SeparateChannels);
	proc->start();
	if (not proc->waitForFinished())
	{
		qWarning() << "timeout trying to get Steam flatpak overrides";
		return false;
	}
	if (proc->exitCode())
	{
		qWarning() << "failed to get Steam flatpak overrides";
		return false;
	}
	std::stringstream overrides(proc->readAllStandardOutput().toStdString());
	wivrn::ini ini(overrides);
	if (auto filesystems = ini.get_optional("Context", "filesystems"))
	{
		auto items = utils::split(*filesystems, ";");
		for (auto & item: items)
			item = item.substr(0, item.find_last_of(':'));
		for (const auto & path: {"xdg-run/wivrn", "xdg-config/openxr", "xdg-config/openvr"})
		{
			if (not std::ranges::contains(items, path))
				return true;
		}
		if (auto path = wivrn_app_path())
		{
			if (not std::ranges::contains(items, path))
				return true;
		}
		return false;
	}
	return true;
}

void steam::fixFlatpakPerm()
{
	auto scope = get_flatpak_scope();
	if (scope.isEmpty())
		return;
	std::unique_ptr<QProcess> proc;
	if (scope == "--system")
		proc = escape_sandbox(
		        "pkexec",
		        "flatpak",
		        "--system",
		        "override",
		        "--filesystem=xdg-run/wivrn:ro",
		        "--filesystem=xdg-config/openxr:ro",
		        "--filesystem=xdg-config/openvr:ro",
		        "--filesystem=" + wivrn_app_path().value_or("xdg-run/wivrn") + ":ro",
		        "com.valvesoftware.Steam");
	else
		proc = escape_sandbox(
		        "flatpak",
		        scope,
		        "override",
		        "--filesystem=xdg-run/wivrn:ro",
		        "--filesystem=xdg-config/openxr:ro",
		        "--filesystem=xdg-config/openvr:ro",
		        "--filesystem=" + wivrn_app_path().value_or("xdg-run/wivrn") + ":ro",
		        "com.valvesoftware.Steam");
	proc->setProcessChannelMode(QProcess::SeparateChannels);
	proc->start();
	if (not proc->waitForFinished())
	{
		qWarning() << "timeout trying to set Steam flatpak overrides";
		return;
	}
	if (proc->exitCode())
	{
		qWarning() << "failed to set Steam flatpak overrides";
		return;
	}
	flatpakNeedPermChanged();
}
