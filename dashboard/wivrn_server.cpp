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
#include "wivrn_server_dbus.h"

Q_DECLARE_METATYPE(field_of_view)

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
}

wivrn_server::~wivrn_server()
{
}

void wivrn_server::on_server_dbus_registered()
{
	if (server_interface)
		server_interface->deleteLater();
	if (server_properties_interface)
		server_properties_interface->deleteLater();

	server_interface = new IoGithubWivrnServerInterface("io.github.wivrn.Server", "/io/github/wivrn/Server", QDBusConnection::sessionBus(), this);
	server_properties_interface = new OrgFreedesktopDBusPropertiesInterface("io.github.wivrn.Server", "/io/github/wivrn/Server", QDBusConnection::sessionBus(), this);

	connect(server_properties_interface, &OrgFreedesktopDBusPropertiesInterface::PropertiesChanged, this, &wivrn_server::on_server_properties_changed);

	serverRunningChanged(m_serverRunning = true);
	refresh_server_properties();
}

void wivrn_server::on_server_dbus_unregistered()
{
	if (server_interface)
		server_interface->deleteLater();
	server_interface = nullptr;

	if (server_properties_interface)
		server_properties_interface->deleteLater();
	server_properties_interface = nullptr;

	serverRunningChanged(m_serverRunning = false);

	if (isHeadsetConnected())
		headsetConnectedChanged(m_headsetConnected = false);
}

void wivrn_server::refresh_server_properties()
{
	if (!server_properties_interface)
		return;

	QDBusPendingReply<QVariantMap> props_pending = server_properties_interface->GetAll("io.github.wivrn.Server");

	if (get_all_properties_call_watcher)
		get_all_properties_call_watcher->deleteLater();
	get_all_properties_call_watcher = new QDBusPendingCallWatcher{props_pending, this};

	connect(get_all_properties_call_watcher, &QDBusPendingCallWatcher::finished, [this, props_pending]() {
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

void wivrn_server::quit()
{
	if (server_interface)
		server_interface->Quit();
}

#include "moc_wivrn_server.cpp"
