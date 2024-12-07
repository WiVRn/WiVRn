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

#pragma once

#include <QCoroCore>
#include <QCoroQmlTask>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QProcess>
#include <QtCore>
#include <QtQml/qqmlregistration.h>

class apk_installer : public QObject
{
	Q_OBJECT
	QML_NAMED_ELEMENT(ApkInstaller)
	QML_SINGLETON

	QNetworkAccessManager manager;

	QSaveFile m_apkFile;
	int64_t m_bytesReceived = 0;
	int64_t m_bytesTotal = -1;
	QString m_installStatus = "";
	bool m_cancellable = true;
	bool m_busy = false;

	QUrl m_apkUrl;
	QString m_latestVersion;

public:
	apk_installer();

	// Version information
	Q_PROPERTY(bool isTagged READ isTagged CONSTANT)
	Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
	Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY latestVersionChanged)
	bool isTagged() const;
	QString currentVersion() const;
	QString latestVersion() const
	{
		return m_latestVersion;
	}

	Q_PROPERTY(QString filePath READ filePath NOTIFY filePathChanged)
	Q_PROPERTY(double bytesReceived READ bytesReceived NOTIFY bytesReceivedChanged)
	Q_PROPERTY(double bytesTotal READ bytesTotal NOTIFY bytesTotalChanged)
	Q_PROPERTY(QString installStatus READ installStatus NOTIFY installStatusChanged)
	Q_PROPERTY(bool cancellable READ cancellable NOTIFY cancellableChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(bool apkAvailable READ apkAvailable NOTIFY apkAvailableChanged)

	QString filePath() const
	{
		return m_apkFile.fileName();
	}

	double bytesTotal() const
	{
		return m_bytesTotal;
	}

	double bytesReceived() const
	{
		return m_bytesReceived;
	}

	QString installStatus() const
	{
		return m_installStatus;
	}

	bool cancellable() const
	{
		return m_cancellable;
	}

	bool busy() const
	{
		return m_busy;
	}

	bool apkAvailable() const
	{
		return m_apkUrl != QUrl{};
	}

	Q_INVOKABLE QCoro::QmlTask refreshLatestVersion()
	{
		return doRefreshLatestVersion();
	}

	Q_INVOKABLE QCoro::QmlTask installApk(QString serial)
	{
		return doInstallApk(serial);
	}

	Q_INVOKABLE void cancelInstallApk();

private:
	QCoro::Task<> doRefreshLatestVersion();
	QCoro::Task<> doInstallApk(QString serial);

Q_SIGNALS:
	void latestVersionChanged(QString);
	void filePathChanged(QString);
	void bytesReceivedChanged(double);
	void bytesTotalChanged(double);
	void installStatusChanged(QString);
	void cancellableChanged(bool);
	void busyChanged(bool);
	void apkAvailableChanged(bool);
};
