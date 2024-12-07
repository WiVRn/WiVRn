#include <KIconTheme>
#include <KLocalizedContext>
#include <KLocalizedString>
#include <QApplication>
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
	KLocalizedString::setApplicationDomain("wivrn");
	QApplication::setOrganizationName(QStringLiteral("WiVRn"));
	QApplication::setOrganizationDomain(QStringLiteral("wivrn.github.io"));
	QApplication::setApplicationName(QStringLiteral("WiVRn dashboard"));
	QApplication::setDesktopFileName(QStringLiteral("io.github.wivrn.wivrn"));

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
