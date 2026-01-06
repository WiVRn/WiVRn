#include "version.h"
#include <KAboutData>
#include <KIconTheme>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QApplication>
#include <QCoroQml>
#include <QCoroQmlTask>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QUrl>
#include <QtQml>

#include <QQuickWindow>
#include <QStringLiteral>

int main(int argc, char * argv[])
{
	KIconTheme::initTheme();
	QApplication app(argc, argv);
	KLocalizedString::setApplicationDomain("wivrn-dashboard");
	app.setOrganizationName(QStringLiteral("wivrn"));
	app.setOrganizationDomain(QStringLiteral("wivrn.github.io"));
	app.setApplicationName(QStringLiteral("wivrn-dashboard"));
	app.setDesktopFileName(QStringLiteral("io.github.wivrn.wivrn"));
	app.setApplicationVersion(QString(wivrn::git_version));

	app.setApplicationDisplayName(i18n("WiVRn dashboard"));

	KAboutData aboutData(
	        QStringLiteral("wivrn-dashboard"),
	        i18nc("@title", "WiVRn"),
	        wivrn::display_version(),
	        i18n("WiVRn server"),
	        KAboutLicense::GPL_V3,
	        i18n("(c) 2022-2026 WiVRn development team"));

	aboutData.setHomepage("https://github.com/WiVRn/WiVRn");
	aboutData.setBugAddress("https://github.com/WiVRn/WiVRn/issues");
	aboutData.setProgramLogo(QIcon(":/qml/wivrn.svg"));

	aboutData.addComponent(
	        i18n("Monado"),
	        i18n("OpenXR runtime"),
	        "",
	        "https://monado.dev/",
	        KAboutLicense::BSL_V1);

	// Set aboutData as information about the app
	KAboutData::setApplicationData(aboutData);

	QCoro::Qml::registerTypes();

	// Work around QTBUG-45105, QTBUG-46074, QTBUG-51112: flicker when resizing
	QQuickWindow::setSceneGraphBackend(QString::fromLatin1("software"));

	QApplication::setStyle(QStringLiteral("breeze"));
	if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
	{
		QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
	}

	QQmlApplicationEngine engine;

	engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
	engine.loadFromModule("io.github.wivrn.wivrn", "Main");

	if (engine.rootObjects().isEmpty())
	{
		return -1;
	}

	return app.exec();
}
