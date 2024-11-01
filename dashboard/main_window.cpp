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

#include "main_window.h"

#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QEvent>
#include <QMessageBox>

#include <chrono>
#include <cmath>

#include "adb.h"
#include "gui_config.h"
#include "settings.h"
#include "ui_main_window.h"
#include "wivrn_server.h"
#include "wizard.h"

#if WIVRN_CHECK_CAPSYSNICE
#include <sys/capability.h>
#endif

Q_LOGGING_CATEGORY(wivrn_log_category, "wivrn")

enum class server_state
{
	stopped = 0,
	started = 1
};

static QString server_path()
{
	return QCoreApplication::applicationDirPath() + "/wivrn-server";
}

#if WIVRN_CHECK_CAPSYSNICE
static bool server_has_cap_sys_nice()
{
	auto caps = cap_get_file(server_path().toStdString().c_str());

	if (not caps)
		return false;

	char * cap_text = cap_to_text(caps, nullptr);
	qDebug() << "Server capabilities:" << cap_text;
	cap_free(cap_text);

	cap_flag_value_t value{};
	if (cap_get_flag(caps, CAP_SYS_NICE, CAP_EFFECTIVE, &value) < 0)
		return false;

	cap_free(caps);

	return value == CAP_SET;
}
#endif

main_window::main_window()
{
	ui = new Ui::MainWindow;
	ui->setupUi(this);

	if (QSystemTrayIcon::isSystemTrayAvailable())
	{
		action_show.setIcon(QIcon::fromTheme("settings-configure"));
		action_hide.setIcon(QIcon::fromTheme("settings-configure"));
		action_exit.setIcon(QIcon::fromTheme("application-exit"));

		systray_menu.addAction(&action_show);
		systray_menu.addAction(&action_hide);
		systray_menu.addAction(&action_exit);

		systray.setToolTip("WiVRn");
		systray.setContextMenu(&systray_menu);
		systray.show();

		connect(&action_show, &QAction::triggered, this, &QMainWindow::show);
		connect(&action_hide, &QAction::triggered, this, &QMainWindow::hide);
		connect(&action_exit, &QAction::triggered, this, [&]() { QApplication::quit(); });

		connect(&systray, &QSystemTrayIcon::activated, this, [&](QSystemTrayIcon::ActivationReason reason) {
			switch (reason)
			{
				case QSystemTrayIcon::ActivationReason::Trigger:
					setVisible(!isVisible());
					break;

				default:
					break;
			}
		});
	}

	server_process_timeout = new QTimer;
	server_process_timeout->setInterval(std::chrono::seconds(1));
	server_process_timeout->setSingleShot(true);
	connect(server_process_timeout, &QTimer::timeout, this, &main_window::on_server_start_timeout);

	server_interface = new wivrn_server(this);

	connect(server_interface, &wivrn_server::serverRunningChanged, this, &main_window::on_server_running_changed);
	connect(server_interface, &wivrn_server::headsetConnectedChanged, this, &main_window::on_headset_connected_changed);

	on_server_running_changed(server_interface->isServerRunning());
	if (not server_interface->isServerRunning())
		start_server();

	connect(server_interface, &wivrn_server::recommendedEyeSizeChanged, this, &main_window::on_recommended_eye_size_changed);
	connect(server_interface, &wivrn_server::availableRefreshRatesChanged, this, &main_window::on_available_refresh_rates_changed);
	connect(server_interface, &wivrn_server::preferredRefreshRateChanged, this, &main_window::on_preferred_refresh_rate_changed);
	connect(server_interface, &wivrn_server::eyeGazeChanged, this, &main_window::on_eye_gaze_changed);
	connect(server_interface, &wivrn_server::faceTrackingChanged, this, &main_window::on_face_tracking_changed);
	connect(server_interface, &wivrn_server::fieldOfViewChanged, this, &main_window::on_field_of_view_changed);
	connect(server_interface, &wivrn_server::handTrackingChanged, this, &main_window::on_hand_tracking_changed);
	connect(server_interface, &wivrn_server::micChannelsChanged, this, &main_window::on_mic_changed);
	connect(server_interface, &wivrn_server::micSampleRateChanged, this, &main_window::on_mic_changed);
	connect(server_interface, &wivrn_server::speakerChannelsChanged, this, &main_window::on_speaker_changed);
	connect(server_interface, &wivrn_server::speakerSampleRateChanged, this, &main_window::on_speaker_changed);
	connect(server_interface, &wivrn_server::supportedCodecsChanged, this, &main_window::on_supported_codecs_changed);
	connect(server_interface, &wivrn_server::steamCommandChanged, this, &main_window::on_steam_command_changed);

	connect(ui->button_start, &QPushButton::clicked, this, &main_window::start_server);
	connect(ui->button_stop, &QPushButton::clicked, this, &main_window::stop_server);
	connect(ui->button_settings, &QPushButton::clicked, this, &main_window::on_action_settings);
	connect(ui->button_disconnect, &QPushButton::clicked, this, &main_window::disconnect_client);
	connect(ui->button_wizard, &QPushButton::clicked, this, &main_window::on_action_wizard);
	connect(ui->button_details, &QPushButton::toggled, this, &main_window::on_button_details_toggled);

	connect(ui->button_about, &QPushButton::clicked, this, []() { QApplication::aboutQt(); });
	connect(ui->button_exit, &QPushButton::clicked, this, []() { QApplication::quit(); });

	connect(ui->copy_steam_command, &QPushButton::clicked, this, [&]() {
		QGuiApplication::clipboard()->setText(ui->label_steam_command->text());
	});

	usb_device_menu = new QMenu(this);
	ui->button_usb->setMenu(usb_device_menu);
	connect(&adb_service, &adb::android_devices_changed, this, &main_window::on_android_device_list_changed);
	on_android_device_list_changed(adb_service.devices());

#if WIVRN_CHECK_CAPSYSNICE
	QIcon icon = QIcon::fromTheme("dialog-information");
	ui->banner_capsysnice_icon->setPixmap(icon.pixmap(ui->banner_capsysnice_dismiss->height()));
	connect(ui->banner_capsysnice_text, &QLabel::linkActivated, this, &main_window::on_banner_capsysnice);

	if (server_has_cap_sys_nice())
		ui->banner_capsysnice->hide();
#else
	ui->banner_capsysnice->hide();
#endif

	retranslate();
}

main_window::~main_window()
{
	delete server_process_timeout;
	server_process_timeout = nullptr;

	if (server_process)
	{
		// Disconnect signals to avoid receiving signals about the server crashing
		disconnect(server_process, &QProcess::finished, this, &main_window::on_server_finished);
		disconnect(server_process, &QProcess::errorOccurred, this, &main_window::on_server_error_occurred);
		server_process->terminate();
		server_process->waitForFinished();
		delete server_process;
		server_process = nullptr;
	}

	delete ui;
	ui = nullptr;
}

void main_window::changeEvent(QEvent * e)
{
	QWidget::changeEvent(e);

	switch (e->type())
	{
		case QEvent::LanguageChange:
			retranslate();
			break;

		default:
			break;
	}
}

void main_window::retranslate()
{
	ui->retranslateUi(this);
	ui->label_how_to_connect->setText(tr("Start the WiVRn app on your headset and connect to \"%1\".").arg(server_interface->hostname()) + "\n\n" + tr("If the server is not visible or the connection fails, check that port 5353 (UDP) and 9757 (TCP and UDP) are open in your firewall."));

	action_show.setText(tr("&Show GUI"));
	action_hide.setText(tr("&Hide GUI"));
	action_exit.setText(tr("&Exit"));

	if (server_interface->isHeadsetConnected())
	{
		on_headset_connected_changed(true);

		on_recommended_eye_size_changed(server_interface->recommendedEyeSize());
		on_available_refresh_rates_changed(server_interface->availableRefreshRates());
		on_preferred_refresh_rate_changed(server_interface->preferredRefreshRate());
		on_eye_gaze_changed(server_interface->eyeGaze());
		on_face_tracking_changed(server_interface->faceTracking());
		on_field_of_view_changed(server_interface->fieldOfView());
		on_hand_tracking_changed(server_interface->handTracking());
		on_mic_changed();
		on_speaker_changed();
		on_supported_codecs_changed(server_interface->supportedCodecs());
		on_steam_command_changed(server_interface->steamCommand());
	}
	else
	{
		on_headset_connected_changed(false);
	}
}

void main_window::closeEvent(QCloseEvent * event)
{
	QMainWindow::closeEvent(event);
	QCoreApplication::quit();
}

void main_window::setVisible(bool visible)
{
	if (visible)
	{
		action_show.setVisible(false);
		action_hide.setVisible(true);
		adb_service.start();
	}
	else
	{
		action_show.setVisible(true);
		action_hide.setVisible(false);
		adb_service.stop();
	}

	QMainWindow::setVisible(visible);
}

void main_window::on_android_device_list_changed(const std::vector<adb::device> & devices)
{
	ui->button_usb->setDisabled(devices.empty());

	if (devices.empty())
		ui->button_usb->setToolTip(tr("No device detected"));
	else
		ui->button_usb->setToolTip("");

	std::vector<std::string> to_be_removed;
	for (auto & [serial, action]: usb_actions)
	{
		if (std::ranges::none_of(devices, [&](const adb::device & x) { return x.serial() == serial; }))
		{
			qDebug() << "Removed" << QString::fromStdString(serial);

			usb_device_menu->removeAction(action.get());
			to_be_removed.push_back(serial);
		}
	}

	for (const auto & serial: to_be_removed)
	{
		usb_actions.erase(serial);
	}

	for (auto & device: devices)
	{
		if (std::ranges::none_of(usb_actions, [&](const std::pair<const std::string, std::unique_ptr<QAction>> & x) { return x.first == device.serial(); }))
		{
			qDebug() << "Detected" << QString::fromStdString(device.serial());
			for (auto & [key, value]: device.properties())
			{
				qDebug() << "    " << QString::fromStdString(key) << ":" << QString::fromStdString(value);
			}

			std::unique_ptr<QAction> device_action = std::make_unique<QAction>(this);

			if (auto model = device.properties().find("model"); model != device.properties().end())
				device_action->setText(QString::fromStdString(model->second));
			else
				device_action->setText("Unknown model");

			usb_device_menu->addAction(device_action.get());

			device_action->setData(QString::fromStdString(device.serial()));
			connect(device_action.get(), &QAction::triggered, this, [this, serial = device.serial()]() {
				on_action_usb(serial);
			});

			usb_actions.insert(std::make_pair(device.serial(), std::move(device_action)));
		}
	}
}

void main_window::on_banner_capsysnice(const QString & link)
{
#if WIVRN_CHECK_CAPSYSNICE
	if (link == "setcap" and not setcap_process)
	{
		setcap_process = new QProcess(this);
		setcap_process->setProgram("pkexec");
		setcap_process->setArguments({"setcap", "CAP_SYS_NICE=+ep", server_path()});
		setcap_process->setProcessChannelMode(QProcess::MergedChannels);
		setcap_process->start();

		connect(setcap_process, &QProcess::finished, this, [this](int exit_code, QProcess::ExitStatus exit_status) {
			// Exit codes:
			// 0: setcap successful
			// 1: setcap failed
			// 126: pkexec: not authorized or authentication error
			// 127: pkexec: dismissed by user
			qDebug() << "pkexec setcap exited with code" << exit_code << "and status" << exit_status;

			qDebug() << "--------";
			for (auto line: setcap_process->readAllStandardOutput().split('\n'))
			{
				QDebug dbg = qDebug();
				dbg.noquote();
				dbg << "  " << QString::fromStdString(line.toStdString());
			}
			qDebug() << "--------";

			if (exit_status == QProcess::NormalExit)
			{
				if (exit_code == 0)
				{
					if (not server_has_cap_sys_nice())
					{
						qDebug() << "pkexec setcap returned successfully but the server does not have the CAP_SYS_NICE capability";
					}
					else
					{
						ui->banner_capsysnice->hide();

						// Don't restart if it wasn't already started
						if (server_interface)
						{
							QMessageBox msgbox;
							msgbox.setIcon(QMessageBox::Information);
							msgbox.setText(tr("You have to restart the WiVRn server to use the CAP_SYS_NICE capability.\nDo you want to restart it now?"));

							if (server_interface->isHeadsetConnected())
							{
								msgbox.setText(msgbox.text() + "\n\n" + tr("This will disconnect the currently connected headset."));
							}

							auto restart_button = msgbox.addButton(tr("Restart WiVRn"), QMessageBox::YesRole);
							auto close_button = msgbox.addButton(QMessageBox::Close);

							msgbox.exec();

							if (msgbox.clickedButton() == restart_button)
							{
								server_process_restart = true;
								stop_server();
							}
						}
					}
				}
			}
			else
			{
				QMessageBox msgbox;
				msgbox.setIcon(QMessageBox::Critical);
				msgbox.setText(tr("Cannot start setcap: %1").arg(exit_status));

				auto close_button = msgbox.addButton(QMessageBox::Close);

				msgbox.exec();
			}

			setcap_process->deleteLater();
			setcap_process = nullptr;
		});
	}
#endif
}

void main_window::on_server_running_changed(bool running)
{
	if (running)
	{
		qDebug() << "Server started";
		server_process_timeout->stop();

		ui->stacked_widget_server->setCurrentIndex((int)server_state::started);
		ui->group_client->setEnabled(true);

		ui->button_stop->setEnabled(true);
		ui->button_settings->setEnabled(true);
	}
	else
	{
		qDebug() << "Server stopped";
		ui->stacked_widget_server->setCurrentIndex((int)server_state::stopped);
		ui->group_client->setEnabled(false);

		ui->button_start->setEnabled(true);
	}
}

void main_window::on_headset_connected_changed(bool connected)
{
	if (connected)
	{
		qDebug() << "Headset connected";
		ui->label_client_status->setText(tr("Connected"));
	}
	else
	{
		qDebug() << "Headset disconnected";
		ui->label_client_status->setText(tr("Not connected"));
	}

	ui->button_disconnect->setVisible(connected);
	ui->button_wizard->setHidden(connected);
	ui->button_details->setVisible(connected);
	ui->button_usb->setHidden(connected);
	ui->headset_properties->setVisible(connected and ui->button_details->isChecked());

	ui->label_how_to_connect->setHidden(connected);
}

void main_window::on_button_details_toggled(bool checked)
{
	ui->headset_properties->setVisible(server_interface->isHeadsetConnected() and checked);
}

void main_window::on_recommended_eye_size_changed(QSize size)
{
	ui->label_eye_size->setText(tr("%1 \u2a2f %2").arg(size.width()).arg(size.height()));
}

void main_window::on_available_refresh_rates_changed(const std::vector<float> & rates)
{
	QStringList rates_str;
	for (float size: rates)
		rates_str.push_back(tr("%1 Hz").arg(size));

	ui->label_refresh_rates->setText(rates_str.join(", "));
}

void main_window::on_preferred_refresh_rate_changed(float rate)
{
	ui->label_prefered_refresh_rate->setText(tr("%1 Hz").arg(std::round(rate)));
}

void main_window::on_eye_gaze_changed(bool supported)
{
	ui->label_eye_gaze_tracking->setText(supported ? tr("Supported") : tr("Not supported"));
}

void main_window::on_face_tracking_changed(bool supported)
{
	ui->label_face_tracking->setText(supported ? tr("Supported") : tr("Not supported"));
}

void main_window::on_field_of_view_changed(const std::vector<field_of_view> & fovs)
{
	if (fovs.size() >= 2)
	{
		ui->label_field_of_view->setText(tr("Left eye: %1° \u2a2f %2°, right eye: %3° \u2a2f %4°")
		                                         .arg((fovs[0].angleRight - fovs[0].angleLeft) * 180 / M_PI, 0, 'f', 1)
		                                         .arg((fovs[0].angleUp - fovs[0].angleDown) * 180 / M_PI, 0, 'f', 1)
		                                         .arg((fovs[1].angleRight - fovs[1].angleLeft) * 180 / M_PI, 0, 'f', 1)
		                                         .arg((fovs[1].angleUp - fovs[1].angleDown) * 180 / M_PI, 0, 'f', 1));
	}
}

void main_window::on_hand_tracking_changed(bool supported)
{
	ui->label_hand_tracking->setText(supported ? tr("Supported") : tr("Not supported"));
}

void main_window::on_mic_changed()
{
	auto channels = server_interface->micChannels();
	auto sample_rate = server_interface->micSampleRate();

	if (channels and sample_rate)
		ui->label_mic->setText(tr("%n channel(s), %1 Hz", nullptr, channels).arg(sample_rate));
	else
		ui->label_mic->setText(tr("N/A"));
}

void main_window::on_speaker_changed()
{
	auto channels = server_interface->speakerChannels();
	auto sample_rate = server_interface->speakerSampleRate();

	if (channels and sample_rate)
		ui->label_speaker->setText(tr("%n channel(s), %1 Hz", nullptr, channels).arg(sample_rate));
	else
		ui->label_speaker->setText(tr("N/A"));
}

void main_window::on_supported_codecs_changed(QStringList value)
{
	ui->label_codecs->setText(value.join(", "));
}

void main_window::on_steam_command_changed(QString value)
{
	ui->label_steam_command->setText(value);
}

void main_window::on_server_finished(int exit_code, QProcess::ExitStatus status)
{
	qDebug() << "Server exited with code" << exit_code << ", status" << status;
	server_process_timeout->stop();

	disconnect(server_process, &QProcess::finished, this, &main_window::on_server_finished);
	disconnect(server_process, &QProcess::errorOccurred, this, &main_window::on_server_error_occurred);
	server_process->deleteLater();
	server_process = nullptr;

	if (exit_code != 0)
	{
		QString error_message = tr("Unknown error (%1), check logs").arg(exit_code);
		switch (exit_code)
		{
			case EXIT_SUCCESS:
			case EXIT_FAILURE:
				break;
			case 2:
			case 3:
				error_message = tr("Insufficient system resources");
				break;
			case 4:
				error_message = tr("Cannot connect to avahi, make sure avahi-daemon service is started");
				break;
		}

		QMessageBox msgbox;
		msgbox.setIcon(QMessageBox::Critical);
		msgbox.setText(tr("Server crashed:\n%1").arg(error_message));
		msgbox.setStandardButtons(QMessageBox::Close);
		msgbox.exec();
	}

	if (server_process_restart)
	{
		server_process_restart = false;
		start_server();
	}
}

void main_window::on_server_error_occurred(QProcess::ProcessError error)
{
	qDebug() << "on_server_error_occurred" << error;
	server_process_timeout->stop();

	QString error_message;
	switch (error)
	{
		case QProcess::ProcessError::FailedToStart:
			error_message = tr("Failed to start");
			break;
		case QProcess::ProcessError::Crashed:
			error_message = tr("Crashed");
			break;
		case QProcess::ProcessError::Timedout:
			error_message = tr("Time out");
			break;
		case QProcess::ProcessError::ReadError:
			error_message = tr("Read error");
			break;
		case QProcess::ProcessError::WriteError:
			error_message = tr("Write error");
			break;
		case QProcess::ProcessError::UnknownError:
			error_message = tr("Unknown error");
			break;
	}

	QMessageBox msgbox /*(this)*/;
	msgbox.setIcon(QMessageBox::Critical);
	msgbox.setText(tr("Failed to start server:\n%1").arg(error_message));
	msgbox.setStandardButtons(QMessageBox::Close);
	msgbox.exec();
}

void main_window::on_server_start_timeout()
{
	qDebug() << "on_server_start_timeout";

	if (server_process)
	{
		// Disconnect signals to avoid receiving signals about the server crashing
		disconnect(server_process, &QProcess::finished, this, &main_window::on_server_finished);
		disconnect(server_process, &QProcess::errorOccurred, this, &main_window::on_server_error_occurred);
		server_process->terminate();
		server_process->deleteLater();
		server_process = nullptr;
	}

	QMessageBox msgbox /*(this)*/;
	msgbox.setIcon(QMessageBox::Critical);
	msgbox.setText(tr("Timeout starting server"));
	msgbox.setStandardButtons(QMessageBox::Close);

	msgbox.exec();
}

void main_window::on_action_settings()
{
	assert(not settings_window);

	settings_window = new settings(server_interface);
	connect(settings_window, &QDialog::finished, this, [&]() {
		settings_window->deleteLater();
		settings_window = nullptr;
	});
	settings_window->exec();
}

void main_window::on_action_wizard()
{
	assert(not wizard_window);

	wizard_window = new wizard;
	wizard_window->exec();
}

void main_window::on_action_usb(const std::string & serial)
{
	auto devs = adb_service.devices();

	if (devs.empty())
		return;

	adb::device dev;
	for (auto & i: adb_service.devices())
	{
		if (i.serial() == serial)
		{
			dev = i;
			break;
		}
	}

	if (!dev)
		return;

	// TODO: in another thread
	// TODO: check if adb works
	bool ok = false;
	for (auto & app: dev.installed_apps())
	{
		if (app.starts_with("org.meumeu.wivrn.") or app == "org.meumeu.wivrn")
		{
			dev.reverse_forward(9757, 9757);
			dev.start(app, "android.intent.action.VIEW", "wivrn+tcp://127.0.0.1:9757");
			ok = true;
			break;
		}
	}

	if (not ok)
	{
		QMessageBox msgbox /*(this)*/;

		msgbox.setIcon(QMessageBox::Critical);
		msgbox.setText(tr("The WiVRn app is not installed on your headset."));
		msgbox.setStandardButtons(QMessageBox::Close);
		msgbox.exec();
	}
}

void main_window::start_server()
{
	qDebug() << "Starting server";

	// TODO activate by dbus?
	server_process = new QProcess;
	connect(server_process, &QProcess::finished, this, &main_window::on_server_finished);
	connect(server_process, &QProcess::errorOccurred, this, &main_window::on_server_error_occurred);

	server_process->setProcessChannelMode(QProcess::ForwardedChannels);

	server_process->start(server_path());
	server_process_timeout->start();

	ui->button_start->setEnabled(false);
}

void main_window::stop_server()
{
	qDebug() << "Stopping server";

	if (server_interface)
		server_interface->quit() /*.waitForFinished()*/;
	// server_process.terminate(); // TODO timer and kill
	ui->button_stop->setEnabled(false);
}

void main_window::disconnect_client()
{
	qDebug() << "Disconnecting client";

	if (server_interface)
		server_interface->disconnect_headset();
}

#include "moc_main_window.cpp"
