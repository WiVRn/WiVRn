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

#include "wivrn_server.h"
#include "gui_config.h"
#include "magic_enum.hpp"
#include "wivrn_server_dbus.h"
#include <QApplication>
#include <QtLogging>
#include <cassert>
#include <memory>

#if WIVRN_CHECK_CAPSYSNICE
#include <sys/capability.h>
#endif

static QString server_path()
{
	return QCoreApplication::applicationDirPath() + "/wivrn-server";
}

#if WIVRN_CHECK_CAPSYSNICE
static bool has_cap_sys_nice()
{
	auto caps = cap_get_file(server_path().toStdString().c_str());

	if (not caps)
		return false;

	// char * cap_text = cap_to_text(caps, nullptr);
	// qDebug() << "Server capabilities:" << cap_text;
	// cap_free(cap_text);

	cap_flag_value_t value{};
	if (cap_get_flag(caps, CAP_SYS_NICE, CAP_EFFECTIVE, &value) < 0)
		return false;

	cap_free(caps);

	return value == CAP_SET;
}
#endif

wivrn_server::wivrn_server(QObject * parent) :
        QObject(parent)
{
	dbus_watcher.setConnection(QDBusConnection::sessionBus());
	dbus_watcher.addWatchedService("io.github.wivrn.Server");

	connect(&dbus_watcher, &QDBusServiceWatcher::serviceRegistered, this, &wivrn_server::on_server_dbus_registered);
	connect(&dbus_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &wivrn_server::on_server_dbus_unregistered);

	const QStringList services = QDBusConnection::sessionBus().interface()->registeredServiceNames();
	if (services.contains("io.github.wivrn.Server"))
		on_server_dbus_registered();

#if WIVRN_CHECK_CAPSYSNICE
	m_capSysNice = has_cap_sys_nice();
#else
	m_capSysNice = true;
#endif
}

wivrn_server::~wivrn_server()
{
	if (server_process)
	{
		server_process->terminate();
		server_process->waitForFinished(1000);
	}
}

void wivrn_server::start_server()
{
	switch (serverStatus())
	{
		case Status::Stopped:
			serverStatusChanged(m_serverStatus = Status::Starting);
			[[fallthrough]];

		case Status::Restarting:
			assert(server_process == nullptr);

			server_process = std::make_unique<QProcess>();

			// connect(server_process, &QProcess::finished, this, &main_window::on_server_finished);
			// connect(server_process, &QProcess::errorOccurred, this, &main_window::on_server_error_occurred);

			server_process->setProcessChannelMode(QProcess::ForwardedChannels);

			server_process->start(server_path(), QApplication::arguments().mid(1));
			// server_process_timeout->start();

			break;

		case Status::Starting:
		case Status::Started:
		case Status::Stopping:
			qWarning() << "start_server: unexpected status " << std::string(magic_enum::enum_name(serverStatus()));
	}
}

void wivrn_server::stop_server()
{
	if (serverStatus() == Status::Started)
	{
		serverStatusChanged(m_serverStatus = Status::Stopping);
		if (server_interface)
			server_interface->Quit();
	}
	else
		qWarning() << "stop_server: unexpected status " << std::string(magic_enum::enum_name(serverStatus()));
}

void wivrn_server::restart_server()
{
	if (serverStatus() == Status::Started)
	{
		serverStatusChanged(m_serverStatus = Status::Restarting);
		if (server_interface)
			server_interface->Quit();
	}
	else
		qWarning() << "restart_server: unexpected status " << std::string(magic_enum::enum_name(serverStatus()));
}

void wivrn_server::on_server_dbus_registered()
{
	serverStatusChanged(m_serverStatus = Status::Started);

	if (server_interface)
		server_interface->deleteLater();
	if (server_properties_interface)
		server_properties_interface->deleteLater();

	server_interface = std::make_unique<IoGithubWivrnServerInterface>("io.github.wivrn.Server", "/io/github/wivrn/Server", QDBusConnection::sessionBus(), this);
	server_properties_interface = std::make_unique<OrgFreedesktopDBusPropertiesInterface>("io.github.wivrn.Server", "/io/github/wivrn/Server", QDBusConnection::sessionBus(), this);

	connect(server_properties_interface.get(), &OrgFreedesktopDBusPropertiesInterface::PropertiesChanged, this, &wivrn_server::on_server_properties_changed);

	serverStatusChanged(m_serverStatus = Status::Started);
	refresh_server_properties();
}

void wivrn_server::on_server_dbus_unregistered()
{
	if (serverStatus() != Status::Restarting)
		serverStatusChanged(m_serverStatus = Status::Stopped);

	if (server_process)
		server_process.release()->deleteLater();

	if (server_interface)
		server_interface.release()->deleteLater();

	if (server_properties_interface)
		server_properties_interface.release()->deleteLater();

	if (isHeadsetConnected())
		headsetConnectedChanged(m_headsetConnected = false);

	if (serverStatus() == Status::Restarting)
		start_server();
}

void wivrn_server::grant_cap_sys_nice()
{
#if WIVRN_CHECK_CAPSYSNICE
	if (not setcap_process)
	{
		setcap_process = std::make_unique<QProcess>();
		setcap_process->setProgram("pkexec");
		setcap_process->setArguments({"setcap", "CAP_SYS_NICE=+ep", server_path()});
		setcap_process->setProcessChannelMode(QProcess::MergedChannels);
		setcap_process->start();

		QObject::connect(setcap_process.get(), &QProcess::finished, this, [this](int exit_code, QProcess::ExitStatus exit_status) {
			// Exit codes:
			// 0: setcap successful
			// 1: setcap failed
			// 126: pkexec: not authorized or authentication error
			// 127: pkexec: dismissed by user

			if (exit_status == QProcess::NormalExit and exit_code == 0)
			{
				if (not has_cap_sys_nice())
				{
					qDebug() << "pkexec setcap returned successfully but the server does not have the CAP_SYS_NICE capability";
				}
				else
				{
					qDebug() << "setcap sucessful";
					capSysNiceChanged(m_capSysNice = true);
				}
			}
			else
			{
				qWarning() << "setcap exited with status" << exit_status << "and code" << exit_code;
			}

			setcap_process.release()->deleteLater();
		});
	}
#endif
}

void wivrn_server::refresh_server_properties()
{
	if (!server_properties_interface)
		return;

	QDBusPendingReply<QVariantMap> props_pending = server_properties_interface->GetAll("io.github.wivrn.Server");

	if (get_all_properties_call_watcher)
		get_all_properties_call_watcher.release()->deleteLater();
	get_all_properties_call_watcher = std::make_unique<QDBusPendingCallWatcher>(props_pending, this);

	connect(get_all_properties_call_watcher.get(), &QDBusPendingCallWatcher::finished, [this, props_pending]() {
		on_server_properties_changed("io.github.wivrn.Server", props_pending.value(), {});
	});
}

void wivrn_server::on_server_properties_changed(const QString & interface_name, const QVariantMap & changed_properties, const QStringList & invalidated_properties)
{
	if (interface_name != IoGithubWivrnServerInterface::staticInterfaceName())
		return;

	if (changed_properties.contains("HeadsetConnected"))
	{
		headsetConnectedChanged(m_headsetConnected = changed_properties["HeadsetConnected"].toBool());
	}

	if (changed_properties.contains("JsonConfiguration"))
	{
		m_jsonConfiguration = changed_properties["JsonConfiguration"].toString();
	}

	if (changed_properties.contains("Pin"))
	{
		pinChanged(m_pin = changed_properties["Pin"].toString());
	}

	if (changed_properties.contains("KnownKeys"))
	{
		m_knownKeys.clear();

		const auto keys = qvariant_cast<QDBusArgument>(changed_properties["KnownKeys"]);
		keys.beginArray();

		while (not keys.atEnd())
		{
			keys.beginStructure();
			QString public_key;
			QString name;
			keys >> name >> public_key;
			keys.endStructure();

			m_knownKeys.push_back(headset_key{public_key, name});
		}
		keys.endArray();

		knownKeysChanged(m_knownKeys);
	}

	if (changed_properties.contains("PairingEnabled"))
	{
		pairingEnabledChanged(m_isPairingEnabled = changed_properties["PairingEnabled"].toBool());
	}

	if (changed_properties.contains("EncryptionEnabled"))
	{
		encryptionEnabledChanged(m_isEncryptionEnabled = changed_properties["EncryptionEnabled"].toBool());
	}

	if (changed_properties.contains("RecommendedEyeSize"))
	{
		const auto arg = qvariant_cast<QDBusArgument>(changed_properties["RecommendedEyeSize"]);

		arg.beginStructure();
		arg >> m_recommendedEyeSize.rwidth() >> m_recommendedEyeSize.rheight();
		arg.endStructure();
		recommendedEyeSizeChanged(m_recommendedEyeSize);
	}

	if (changed_properties.contains("AvailableRefreshRates"))
	{
		const auto rates = qvariant_cast<QDBusArgument>(changed_properties["AvailableRefreshRates"]);

		m_availableRefreshRates.clear();
		rates.beginArray();
		while (!rates.atEnd())
		{
			double element;
			rates >> element;
			m_availableRefreshRates.push_back(element);
		}
		rates.endArray();

		availableRefreshRatesChanged(m_availableRefreshRates);
	}

	if (changed_properties.contains("PreferredRefreshRate"))
	{
		preferredRefreshRateChanged(m_preferredRefreshRate = changed_properties["PreferredRefreshRate"].toFloat());
	}

	if (changed_properties.contains("EyeGaze"))
	{
		eyeGazeChanged(m_eyeGaze = changed_properties["EyeGaze"].toBool());
	}

	if (changed_properties.contains("FaceTracking"))
	{
		faceTrackingChanged(m_faceTracking = changed_properties["FaceTracking"].toBool());
	}

	if (changed_properties.contains("FieldOfView"))
	{
		const auto fovs = qvariant_cast<QDBusArgument>(changed_properties["FieldOfView"]);
		fovs.beginArray();

		QStringList fov_str;
		m_fieldOfView.clear();
		while (!fovs.atEnd())
		{
			fovs.beginStructure();
			double left, right, up, down;
			fovs >> left >> right >> up >> down;
			fovs.endStructure();
			m_fieldOfView.push_back(field_of_view(left, right, up, down));
		}
		fovs.endArray();

		fieldOfViewChanged(m_fieldOfView);
	}

	if (changed_properties.contains("HandTracking"))
	{
		handTrackingChanged(m_handTracking = changed_properties["HandTracking"].toBool());
	}

	if (changed_properties.contains("MicChannels"))
	{
		micChannelsChanged(m_micChannels = changed_properties["MicChannels"].toInt());
	}

	if (changed_properties.contains("MicSampleRate"))
	{
		micSampleRateChanged(m_micSampleRate = changed_properties["MicSampleRate"].toInt());
	}

	if (changed_properties.contains("SpeakerChannels"))
	{
		speakerChannelsChanged(m_speakerChannels = changed_properties["SpeakerChannels"].toInt());
	}

	if (changed_properties.contains("SpeakerSampleRate"))
	{
		speakerSampleRateChanged(m_speakerSampleRate = changed_properties["SpeakerSampleRate"].toInt());
	}

	if (changed_properties.contains("SupportedCodecs"))
	{
		supportedCodecsChanged(m_supportedCodecs = changed_properties["SupportedCodecs"].toStringList());
	}

	if (changed_properties.contains("SteamCommand"))
	{
		steamCommandChanged(m_steamCommand = changed_properties["SteamCommand"].toString());
	}
}

void wivrn_server::setJsonConfiguration(QString new_configuration)
{
	server_interface->setJsonConfiguration(m_jsonConfiguration = new_configuration);
}

void wivrn_server::revoke_key(QString public_key)
{
	server_interface->RevokeKey(public_key);
}

void wivrn_server::rename_key(QString public_key, QString name)
{
	server_interface->RenameKey(public_key, name);
}

QString wivrn_server::enable_pairing(int timeout_secs)
{
	return server_interface->EnablePairing(timeout_secs).value();
}

void wivrn_server::disable_pairing()
{
	server_interface->DisablePairing();
}

QString wivrn_server::hostname()
{
	static auto _hostname = []() -> QString {
		OrgFreedesktopDBusPropertiesInterface hostname1("org.freedesktop.hostname1", "/org/freedesktop/hostname1", QDBusConnection::systemBus());

		for (auto property: {"PrettyHostname", "StaticHostname", "Hostname"})
		{
			QString name = hostname1.Get("org.freedesktop.hostname1", property).value().variant().toString();

			if (name != "")
				return name;
		};

		char buf[HOST_NAME_MAX];
		int code = gethostname(buf, sizeof(buf));
		if (code == 0)
			return buf;

		qDebug() << "Failed to get hostname";
		return "no-hostname";
	}();

	return _hostname;
}

void wivrn_server::disconnect_headset()
{
	if (server_interface)
		server_interface->Disconnect();
}

#include "moc_wivrn_server.cpp"
