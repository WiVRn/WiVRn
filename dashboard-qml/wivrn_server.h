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
#include <QObject>
#include <QProcess>
#include <QSize>
#include <QtQml/qqmlregistration.h>
#include <memory>

#include "wivrn_qdbus_types.h"

class IoGithubWivrnServerInterface;
class OrgFreedesktopDBusPropertiesInterface;

class wivrn_server : public QObject
{
	Q_OBJECT
	QML_NAMED_ELEMENT(WivrnServer)
	QML_SINGLETON

	std::unique_ptr<IoGithubWivrnServerInterface> server_interface{};
	std::unique_ptr<OrgFreedesktopDBusPropertiesInterface> server_properties_interface{};
	QDBusServiceWatcher dbus_watcher;
	std::unique_ptr<QDBusPendingCallWatcher> get_all_properties_call_watcher;

	std::unique_ptr<QProcess> server_process;
	std::unique_ptr<QProcess> setcap_process;

public:
	struct headset_key
	{
		QString public_key;
		QString name;
	};
	using headset_keys = std::vector<headset_key>;

	wivrn_server(QObject * parent = nullptr);
	wivrn_server(const wivrn_server &) = delete;
	wivrn_server & operator=(const wivrn_server &) = delete;
	~wivrn_server();

	enum class Status
	{
		Stopped,
		Started,
		Stopping,
		Starting,
		Restarting,
	};

	Q_ENUMS(Status)

	// Server information
	Q_PROPERTY(Status serverStatus READ serverStatus NOTIFY serverStatusChanged)
	Q_PROPERTY(bool capSysNice READ capSysNice NOTIFY capSysNiceChanged)
	Q_PROPERTY(bool headsetConnected READ isHeadsetConnected NOTIFY headsetConnectedChanged)
	Q_PROPERTY(QString jsonConfiguration READ jsonConfiguration WRITE setJsonConfiguration)
	Q_INVOKABLE void start_server();
	Q_INVOKABLE void stop_server();
	Q_INVOKABLE void restart_server();
	Q_INVOKABLE void grant_cap_sys_nice();

	// Authentication
	Q_PROPERTY(QString pin READ pin NOTIFY pinChanged)
	Q_PROPERTY(headset_keys knownKeys READ knownKeys NOTIFY knownKeysChanged)
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
	Q_PROPERTY(QString steamCommand READ steamCommand NOTIFY steamCommandChanged)

	// hostnamed
	Q_PROPERTY(QString hostname READ hostname())

	Status serverStatus() const
	{
		return m_serverStatus;
	}

	bool capSysNice() const
	{
		return m_capSysNice;
	}

	bool isHeadsetConnected() const
	{
		return m_headsetConnected;
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

	headset_keys knownKeys() const
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

	QString steamCommand() const
	{
		return m_steamCommand;
	}

	QString hostname();

	Q_INVOKABLE void disconnect_headset();

private:
	void on_server_dbus_registered();
	void on_server_dbus_unregistered();
	void on_server_properties_changed(const QString & interface_name, const QVariantMap & changed_properties, const QStringList & invalidated_properties);

	void refresh_server_properties();

	Status m_serverStatus{Status::Stopped};
	bool m_capSysNice{};
	bool m_headsetConnected{};
	QString m_jsonConfiguration{};

	QString m_pin{};
	headset_keys m_knownKeys;
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
	QString m_steamCommand{};

Q_SIGNALS:
	void serverStatusChanged(Status);
	void capSysNiceChanged(bool);
	void headsetConnectedChanged(bool);
	void pinChanged(QString);
	void knownKeysChanged(headset_keys);
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
};
