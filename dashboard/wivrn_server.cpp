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
#include "utils/flatpak.h"
#include "wivrn_server_dbus.h"
#include <KLocalization>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QtLogging>
#include <cassert>
#include <memory>
#include <nlohmann/json.hpp>

#if WIVRN_CHECK_CAPSYSNICE
#include <sys/capability.h>
#endif

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

using namespace std::chrono_literals;

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

QString get_server_log_dir()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
	QString state_path = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
#else
	QProcessEnvironment env;
	QString state_path = env.value("XDG_STATE_HOME", env.value("HOME") + "/.local/state");

	// https://github.com/qt/qtbase/blob/dev/src/corelib/io/qstandardpaths_unix.cpp#L27
	const QString org = QCoreApplication::organizationName();
	if (!org.isEmpty())
		state_path += u'/' + org;

	const QString appName = QCoreApplication::applicationName();
	if (!appName.isEmpty())
		state_path += u'/' + appName;

#endif
	QDir{}.mkpath(state_path);

	return state_path;
}

std::unique_ptr<QFile> get_server_log_file()
{
	QString state_path = get_server_log_dir();

	QFileInfoList files = QDir{state_path}.entryInfoList({"server_logs_*.txt"}, QDir::Files, QDir::Name);
	while (files.size() >= 10)
	{
		qDebug() << "Removing log file" << files.front().absoluteFilePath();
		QFile{files.front().absoluteFilePath()}.remove();

		files.pop_front();
	}

	std::unique_ptr<QFile> log_file = std::make_unique<QFile>(state_path + "/server_logs_" + QDateTime::currentDateTime().toString(Qt::ISODate) + ".txt");
	log_file->open(QFile::WriteOnly | QFile::Truncate);

	qDebug() << "Saving logs in" << log_file->fileName();

	return log_file;
}

void wivrn_server::start_server()
{
	switch (serverStatus())
	{
		case Status::FailedToStart:
		case Status::Stopped:
			serverStatusChanged(m_serverStatus = Status::Starting);
			[[fallthrough]];

		case Status::Restarting: {
			server_process = new QProcess(this);

			server_process->setProcessChannelMode(QProcess::MergedChannels);
			server_output.clear();
			server_log_file = get_server_log_file();

			connect(server_process, &QProcess::readyReadStandardOutput, this, &wivrn_server::on_server_ready_read_standard_output);
			connect(server_process, &QProcess::finished, this, [this, p = server_process]() {
				p->deleteLater();

				if (server_process == p)
				{
					server_process = nullptr;

					if (m_serverStatus == Status::Starting)
					{
						qDebug() << "Server finished before registering on dbus";
						serverStatusChanged(m_serverStatus = Status::FailedToStart);
					}
				}
			});

			server_process->start(server_path(), QApplication::arguments().mid(1));
		}

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

void wivrn_server::on_server_ready_read_standard_output()
{
	QByteArray output = server_process->readAllStandardOutput();

	qsizetype index;
	do
	{
		index = output.indexOf('\n');
		QString line;

		if (index >= 0)
		{
			line = QString::fromLatin1(output.first(index + 1));
			output.remove(0, index + 1);
		}
		else
		{
			line = QString::fromLatin1(output);
			output.clear();
		}

		if (server_output.empty() or server_output.back().endsWith("\n"))
		{
			// Start a new line
			QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);

			server_output.push_back("[" + timestamp + "] " + line);
			server_log_file->write(("[" + timestamp + "] " + line).toLatin1());
		}
		else
		{
			server_output.back() += line;
			server_log_file->write(line.toLatin1());
		}
	} while (index >= 0 and output.size() > 0);
	server_log_file->flush();

	serverLogsChanged(serverLogs());
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
		server_process = nullptr;

	if (server_interface)
		server_interface.release()->deleteLater();

	if (server_properties_interface)
		server_properties_interface.release()->deleteLater();

	if (isHeadsetConnected())
		headsetConnectedChanged(m_headsetConnected = false);

	if (isPairingEnabled())
		pairingEnabledChanged(m_isPairingEnabled = false);

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

void wivrn_server::open_server_logs()
{
	QString path = get_server_log_dir();
	qDebug() << "Opening" << path;
	QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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
		jsonConfigurationChanged(m_jsonConfiguration = changed_properties["JsonConfiguration"].toString());
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
			qlonglong last_connection_timestamp;
			keys >> name >> public_key >> last_connection_timestamp;
			keys.endStructure();

			headset h{name, public_key};
			if (last_connection_timestamp)
				h.setLastConnection(QDateTime::fromSecsSinceEpoch(last_connection_timestamp));

			m_knownKeys.push_back(h);
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
	jsonConfigurationChanged(new_configuration);
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
	qDebug() << "Enabling pairing for" << timeout_secs << "seconds";
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

QString wivrn_server::host_path(QString path)
{
	if (not wivrn::is_flatpak())
		return path;

	QDBusInterface docs("org.freedesktop.portal.Documents", "/org/freedesktop/portal/documents", "org.freedesktop.portal.Documents", QDBusConnection::sessionBus());
	if (not docs.isValid())
		return path;

	QString id = QFileInfo(path).dir().dirName();
	[[maybe_unused]] static auto r = qDBusRegisterMetaType<QMap<QString, QByteArray>>();
	QDBusReply<QMap<QString, QByteArray>> res = docs.call("GetHostPaths", QStringList(id));
	if ((not res.isValid()) or res.value().empty())
		return path;

	path = res.value().first();
	if (path.endsWith('\0'))
		path.chop(1);
	return path;
}

QList<openVRCompatLib> wivrn_server::openVRCompat() const
{
	if (wivrn::is_flatpak())
		return {
		        openVRCompatLib(i18n("xrizer"), "xrizer"),
		        openVRCompatLib(i18n("Open Composite"), "OpenComposite"),
		};

	QList<openVRCompatLib> result;
	for (auto path: std::ranges::split_view(std::string_view(OVR_COMPAT_SEARCH_PATH), std::string_view(":")))
	{
		if (std::filesystem::path fs = std::string_view(path); std::filesystem::exists(fs))
			result.push_back(openVRCompatLib(QString::fromStdString(fs.string()), QString::fromStdString(fs.string())));
	}
	return result;
}

void wivrn_server::disconnect_headset()
{
	if (server_interface)
		server_interface->Disconnect();
}

void wivrn_server::copy_steam_command()
{
	QGuiApplication::clipboard()->setText(m_steamCommand);
}

#include "moc_wivrn_server.cpp"
