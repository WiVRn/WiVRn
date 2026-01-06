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

#pragma once

#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QSize>
#include <QtCore>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "wivrn_qdbus_types.h"

class IoGithubWivrnServerInterface;
class OrgFreedesktopDBusPropertiesInterface;

class headset
{
	Q_GADGET
	QML_VALUE_TYPE(headset)
	Q_PROPERTY(QString name READ name WRITE setName)
	Q_PROPERTY(QString publicKey READ publicKey WRITE setPublicKey)
	Q_PROPERTY(bool hasLastConnection READ hasLastConnection)
	Q_PROPERTY(QDateTime lastConnection READ lastConnection WRITE setLastConnection)

	QString m_name;
	QString m_publicKey;
	QDateTime m_lastConnection;

public:
	headset() = default;
	headset(const headset &) = default;
	headset(headset &&) = default;
	headset & operator=(const headset &) = default;
	headset & operator=(headset &&) = default;
	headset(QString name, QString publicKey, QDateTime lastConnection) :
	        m_name(name), m_publicKey(publicKey), m_lastConnection(lastConnection) {}

	headset(QString name, QString publicKey) :
	        m_name(name), m_publicKey(publicKey) {}

	QString name() const
	{
		return m_name;
	}

	QString publicKey() const
	{
		return m_publicKey;
	}

	QDateTime lastConnection() const
	{
		return m_lastConnection;
	}

	bool hasLastConnection() const
	{
		return !m_lastConnection.isNull();
	}

	void setName(QString value)
	{
		m_name = value;
	}

	void setPublicKey(QString value)
	{
		m_publicKey = value;
	}

	void setLastConnection(QDateTime value)
	{
		m_lastConnection = value;
	}
};

class openVRCompatLib
{
	Q_GADGET
	QML_VALUE_TYPE(openVRCompat)
	QML_ELEMENT
	Q_PROPERTY(QString name READ name CONSTANT)
	Q_PROPERTY(QString path READ path CONSTANT)

	QString m_name;
	QString m_path;

public:
	openVRCompatLib() = default;
	openVRCompatLib(QString name, QString path) :
	        m_name(name), m_path(path) {}

	// localized name of the compat layer (can be the path)
	QString name() const
	{
		return m_name;
	}

	QString path() const
	{
		return m_path;
	}
};

class serverErrorData
{
	Q_GADGET
	QML_VALUE_TYPE(serverError)

	Q_PROPERTY(QString where MEMBER m_where CONSTANT)
	Q_PROPERTY(QString message MEMBER m_message CONSTANT)

	QString m_where;
	QString m_message;

public:
	serverErrorData() = default;
	serverErrorData(QString where, QString message) :
	        m_where(where), m_message(message) {}
};

class wivrn_server : public QObject
{
	Q_OBJECT
	QML_NAMED_ELEMENT(WivrnServer)
	QML_SINGLETON

	std::unique_ptr<IoGithubWivrnServerInterface> server_interface{};
	std::unique_ptr<OrgFreedesktopDBusPropertiesInterface> server_properties_interface{};
	QDBusServiceWatcher dbus_watcher;
	std::unique_ptr<QDBusPendingCallWatcher> get_all_properties_call_watcher;

	QProcess * server_process = nullptr;

public:
	wivrn_server(QObject * parent = nullptr);
	wivrn_server(const wivrn_server &) = delete;
	wivrn_server & operator=(const wivrn_server &) = delete;
	~wivrn_server();

	enum class Status
	{
		FailedToStart,
		Stopped,
		Started,
		Stopping,
		Starting,
		Restarting,
	};

	Q_ENUMS(Status)

	// Server information
	Q_PROPERTY(Status serverStatus READ serverStatus NOTIFY serverStatusChanged)
	Q_PROPERTY(bool headsetConnected READ isHeadsetConnected NOTIFY headsetConnectedChanged)
	Q_PROPERTY(bool sessionRunning READ isSessionRunning NOTIFY sessionRunningChanged)
	Q_PROPERTY(QString jsonConfiguration READ jsonConfiguration WRITE setJsonConfiguration NOTIFY jsonConfigurationChanged)
	Q_PROPERTY(QString serverLogs READ serverLogs NOTIFY serverLogsChanged)
	Q_INVOKABLE void start_server();
	Q_INVOKABLE void stop_server();
	Q_INVOKABLE void restart_server();
	Q_INVOKABLE void open_server_logs();

	// Authentication
	Q_PROPERTY(QString pin READ pin NOTIFY pinChanged)
	Q_PROPERTY(QList<headset> knownKeys READ knownKeys NOTIFY knownKeysChanged)
	Q_PROPERTY(bool pairingEnabled READ isPairingEnabled NOTIFY pairingEnabledChanged)
	Q_PROPERTY(bool encryptionEnabled READ isEncryptionEnabled NOTIFY encryptionEnabledChanged)
	Q_INVOKABLE void revoke_key(QString public_key);
	Q_INVOKABLE void rename_key(QString public_key, QString name);
	Q_INVOKABLE QString enable_pairing(int timeout_secs = 120);
	Q_INVOKABLE void disable_pairing();

	// Headset information, valid only if HeadsetConnected is true
	Q_PROPERTY(QSize recommendedEyeSize READ recommendedEyeSize NOTIFY recommendedEyeSizeChanged)
	Q_PROPERTY(std::vector<float> availableRefreshRates READ availableRefreshRates NOTIFY availableRefreshRatesChanged)
	Q_PROPERTY(float preferredRefreshRate READ preferredRefreshRate NOTIFY preferredRefreshRateChanged)
	Q_PROPERTY(bool eyeGaze READ eyeGaze NOTIFY eyeGazeChanged)
	Q_PROPERTY(bool faceTracking READ faceTracking NOTIFY faceTrackingChanged)
	Q_PROPERTY(std::vector<field_of_view> fieldOfView READ fieldOfView NOTIFY fieldOfViewChanged)
	Q_PROPERTY(bool handTracking READ handTracking NOTIFY handTrackingChanged)
	Q_PROPERTY(int micChannels READ micChannels NOTIFY micChannelsChanged)
	Q_PROPERTY(int micSampleRate READ micSampleRate NOTIFY micSampleRateChanged)
	Q_PROPERTY(int speakerChannels READ speakerChannels NOTIFY speakerChannelsChanged)
	Q_PROPERTY(int speakerSampleRate READ speakerSampleRate NOTIFY speakerSampleRateChanged)
	Q_PROPERTY(QStringList supportedCodecs READ supportedCodecs NOTIFY supportedCodecsChanged)
	Q_PROPERTY(QString systemName READ systemName NOTIFY systemNameChanged)
	Q_PROPERTY(QString steamCommand READ steamCommand NOTIFY steamCommandChanged)

	// hostnamed
	Q_PROPERTY(QString hostname READ hostname CONSTANT)

	// flatpak API
	Q_INVOKABLE QString host_path(QString path);

	// System information
	Q_PROPERTY(QList<openVRCompatLib> openVRCompat READ openVRCompat CONSTANT)

	Status serverStatus() const
	{
		return m_serverStatus;
	}

	bool isHeadsetConnected() const
	{
		return m_headsetConnected;
	}

	bool isSessionRunning() const
	{
		return m_sessionRunning;
	}

	QString jsonConfiguration() const
	{
		return m_jsonConfiguration;
	}

	void setJsonConfiguration(QString);

	QString pin() const
	{
		return m_pin;
	}

	QList<headset> knownKeys() const
	{
		return m_knownKeys;
	}

	bool isPairingEnabled() const
	{
		return m_isPairingEnabled;
	}

	bool isEncryptionEnabled() const
	{
		return m_isEncryptionEnabled;
	}

	QSize recommendedEyeSize() const
	{
		return m_recommendedEyeSize;
	}

	const std::vector<float> & availableRefreshRates() const
	{
		return m_availableRefreshRates;
	}

	float preferredRefreshRate() const
	{
		return m_preferredRefreshRate;
	}

	bool eyeGaze() const
	{
		return m_eyeGaze;
	}

	bool faceTracking() const
	{
		return m_faceTracking;
	}

	const std::vector<field_of_view> & fieldOfView() const
	{
		return m_fieldOfView;
	}

	bool handTracking() const
	{
		return m_handTracking;
	}

	int micChannels() const
	{
		return m_micChannels;
	}

	int micSampleRate() const
	{
		return m_micSampleRate;
	}

	int speakerChannels() const
	{
		return m_speakerChannels;
	}

	int speakerSampleRate() const
	{
		return m_speakerSampleRate;
	}

	QStringList supportedCodecs() const
	{
		return m_supportedCodecs;
	}

	QString systemName() const
	{
		return m_systemName;
	}

	QString steamCommand() const
	{
		return m_steamCommand;
	}

	QString hostname();

	QString serverLogs()
	{
		return server_output.join("");
	}

	QList<openVRCompatLib> openVRCompat() const;

	Q_INVOKABLE void disconnect_headset();
	Q_INVOKABLE void copy_steam_command();

private:
	void on_server_dbus_registered();
	void on_server_dbus_unregistered();
	void on_server_properties_changed(const QString & interface_name, const QVariantMap & changed_properties, const QStringList & invalidated_properties);

	void on_server_ready_read_standard_output();

	std::unique_ptr<QFile> server_log_file;
	QStringList server_output;

	void refresh_server_properties();

	Status m_serverStatus{Status::Stopped};
	bool m_headsetConnected{};
	bool m_sessionRunning{};
	QString m_jsonConfiguration{};

	QString m_pin{};
	QList<headset> m_knownKeys;
	bool m_isPairingEnabled{};
	bool m_isEncryptionEnabled{};

	QSize m_recommendedEyeSize{};
	std::vector<float> m_availableRefreshRates{};
	float m_preferredRefreshRate{};
	bool m_eyeGaze{};
	bool m_faceTracking{};
	std::vector<field_of_view> m_fieldOfView{};
	bool m_handTracking{};
	int m_micChannels{};
	int m_micSampleRate{};
	int m_speakerChannels{};
	int m_speakerSampleRate{};
	QStringList m_supportedCodecs{};
	QString m_systemName{};
	QString m_steamCommand{};

Q_SIGNALS:
	void serverStatusChanged(Status);
	void headsetConnectedChanged(bool);
	void sessionRunningChanged(bool);
	void jsonConfigurationChanged(QString);
	void needMonadoVulkanLayerChanged(bool);
	void pinChanged(QString);
	void knownKeysChanged(QList<headset>);
	void pairingEnabledChanged(bool);
	void encryptionEnabledChanged(bool);

	void recommendedEyeSizeChanged(QSize);
	void availableRefreshRatesChanged(const std::vector<float> &);
	void preferredRefreshRateChanged(float);
	void eyeGazeChanged(bool);
	void faceTrackingChanged(bool);
	void fieldOfViewChanged(const std::vector<field_of_view> &);
	void handTrackingChanged(bool);
	void micChannelsChanged(int);
	void micSampleRateChanged(int);
	void speakerChannelsChanged(int);
	void speakerSampleRateChanged(int);
	void supportedCodecsChanged(QStringList);
	void steamCommandChanged(QString);
	void systemNameChanged(QString);
	void serverLogsChanged(QString);
	void serverError(serverErrorData);
};
