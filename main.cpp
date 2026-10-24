#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "mixercontroller.h"
int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Stormage");
    QCoreApplication::setApplicationName("StormMixNative");
    MixerController mixer;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("mixer", &mixer);
    engine.loadFromModule("StormMix", "Main");
    if (engine.rootObjects().isEmpty()) return -1;
    return app.exec();
}
