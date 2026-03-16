#include "CustomPlugin.h"
#include "GeoWork.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>

CustomPlugin::CustomPlugin(QGCApplication *app, QGCToolbox* toolbox)
    : QGCCorePlugin(app, toolbox)
{
    qDebug() << "[GeoWork] Initialized CustomPlugin";
}

QQmlApplicationEngine* CustomPlugin::createQmlApplicationEngine(QObject* parent)
{
    QQmlApplicationEngine* const qml{QGCCorePlugin::createQmlApplicationEngine(parent)};
    qml->rootContext()->setContextProperty("GeoWork", GeoWork::instance());

    return qml;
}
