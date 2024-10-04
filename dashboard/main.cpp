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

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

#include "main_window.h"
#include "version.h"
#include "wizard.h"

int main(int argc, char * argv[])
{
	QApplication app(argc, argv);

	app.setApplicationName("wivrn-dashboard");
	app.setOrganizationName("wivrn");
	app.setApplicationVersion(wivrn::git_version);
	app.setDesktopFileName("io.github.wivrn.wivrn");

	QTranslator translator;

	QLocale locale = QLocale::system();
	if (translator.load(locale, "wivrn", "_", ":/i18n"))
	{
		qDebug() << "Adding translator for " << locale;
		app.installTranslator(&translator);
	}
	else
	{
		qDebug() << "Cannot add translator for " << locale;
	}

	app.setApplicationDisplayName(QObject::tr("WiVRn dashboard"));

	std::unique_ptr<wizard> wizard_window;

	// The main window is created before QWizard::finished so that the
	// server is started in the wizard
	auto main = std::make_unique<main_window>();

	QSettings settings;
	if (settings.value("wizard/first_run", true).toBool())
	{
		wizard_window.reset(new wizard);
		wizard_window->show();

		QObject::connect(wizard_window.get(), &QWizard::finished, [&](int value) {
			settings.setValue("wizard/first_run", false);

			wizard_window.release()->deleteLater();
			main->show();
		});
	}
	else
	{
		main->show();
	}

	return app.exec();
}
