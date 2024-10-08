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

#include "exit_codes.h"
#include "settings.h"
#include "ui_main_window.h"
#include "wivrn_server.h"
#include "wizard.h"

#include "gui_config.h"

Q_LOGGING_CATEGORY(wivrn_log_category, "wivrn")

enum class server_state
{
	stopped = 0,
	started = 1
};

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

	if (server_interface->isServerRunning())
		on_server_dbus_registered();
	else
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

bool main_window::event(QEvent * e)
{
#ifdef WORKAROUND_QTBUG_90005
	if (wizard_window and e->type() == QEvent::WindowActivate)
	{
		wizard_window->activateWindow();
		return true;
	}
	else if (settings_window and e->type() == QEvent::WindowActivate)
	{
		settings_window->activateWindow();
		return true;
	}
	else
#endif
		return QMainWindow::event(e);
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
	}
	else
	{
		action_show.setVisible(true);
		action_hide.setVisible(false);
	}

	QMainWindow::setVisible(visible);
}

void main_window::on_server_running_changed(bool running)
{
	if (running)
	{
		on_server_dbus_registered();

		ui->stacked_widget_server->setCurrentIndex((int)server_state::started);
		ui->group_client->setEnabled(true);

		ui->button_stop->setEnabled(true);
		ui->button_settings->setEnabled(true);
	}
	else
	{
		ui->stacked_widget_server->setCurrentIndex((int)server_state::stopped);
		ui->group_client->setEnabled(false);

		ui->button_start->setEnabled(true);
	}
}

void main_window::on_headset_connected_changed(bool connected)
{
	if (connected)
	{
		ui->label_client_status->setText(tr("Connected"));
	}
	else
	{
		ui->label_client_status->setText(tr("Not connected"));
	}

	ui->button_disconnect->setVisible(connected);
	ui->button_wizard->setHidden(connected);
	ui->button_details->setVisible(connected);
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

void main_window::on_server_dbus_registered()
{
	server_process_timeout->stop();
}

void main_window::on_server_finished(int exit_code, QProcess::ExitStatus status)
{
	disconnect(server_process, &QProcess::finished, this, &main_window::on_server_finished);
	disconnect(server_process, &QProcess::errorOccurred, this, &main_window::on_server_error_occurred);
	server_process->deleteLater();
	server_process = nullptr;

	if (exit_code != 0)
	{
		QString error_message = tr("Unknown error (%1), check logs").arg(exit_code);
		switch (exit_code)
		{
			case wivrn_exit_code::cannot_connect_to_avahi:
				error_message = tr("Cannot connect to avahi, make sure avahi-daemon service is started");
				break;
			case wivrn_exit_code::cannot_create_pipe:
			case wivrn_exit_code::cannot_create_socketpair:
				error_message = tr("Insufficient system resources");
				break;
			case wivrn_exit_code::success:
			case wivrn_exit_code::unknown_error:
				break;
		}

		QMessageBox msgbox /*(this)*/;
		msgbox.setIcon(QMessageBox::Critical);
		msgbox.setText(tr("Server crashed:\n%1").arg(error_message));
		msgbox.setStandardButtons(QMessageBox::Close);

#ifdef WORKAROUND_QTBUG_90005
		setEnabled(false);
#endif
		msgbox.exec();

#ifdef WORKAROUND_QTBUG_90005
		setEnabled(true);
#endif
	}
}

void main_window::on_server_error_occurred(QProcess::ProcessError error)
{
	qDebug() << "on_server_error_occurred" << error;

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

#ifdef WORKAROUND_QTBUG_90005
	setEnabled(false);
#endif
	msgbox.exec();

#ifdef WORKAROUND_QTBUG_90005
	setEnabled(true);
#endif
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

#ifdef WORKAROUND_QTBUG_90005
	setEnabled(false);
#endif
	msgbox.exec();

#ifdef WORKAROUND_QTBUG_90005
	setEnabled(true);
#endif
}

void main_window::on_action_settings()
{
	assert(not settings_window);

	settings_window = new settings(server_interface);

#ifdef WORKAROUND_QTBUG_90005
	setEnabled(false);
	connect(settings_window, &QDialog::finished, this, [&](int r) {
		setEnabled(true);
		settings_window->deleteLater();
		settings_window = nullptr;
	});
#endif

	settings_window->exec();
}

void main_window::on_action_wizard()
{
	assert(not wizard_window);

	wizard_window = new wizard;

#ifdef WORKAROUND_QTBUG_90005
	setDisabled(true);
	connect(wizard_window, &QWizard::finished, this, [&]() {
		setEnabled(true);
		wizard_window->deleteLater();
		wizard_window = nullptr;
	});
#endif

	wizard_window->exec();
}

void main_window::start_server()
{
	// TODO activate by dbus?
	server_process = new QProcess;
	connect(server_process, &QProcess::finished, this, &main_window::on_server_finished);
	connect(server_process, &QProcess::errorOccurred, this, &main_window::on_server_error_occurred);

	server_process->setProcessChannelMode(QProcess::ForwardedChannels);

	server_process->start(QCoreApplication::applicationDirPath() + "/wivrn-server");
	server_process_timeout->start();

	ui->button_start->setEnabled(false);
}

void main_window::stop_server()
{
	if (server_interface)
		server_interface->quit() /*.waitForFinished()*/;
	// server_process.terminate(); // TODO timer and kill
	ui->button_stop->setEnabled(false);
}

void main_window::disconnect_client()
{
	if (server_interface)
		server_interface->disconnect_headset();
}

#include "moc_main_window.cpp"
