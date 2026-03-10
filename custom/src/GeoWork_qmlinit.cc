#include <GeoWork.h>
#include <QDebug>
#include <QtQml>

// TODO: Is this really necessary?
static void initGeoWorkQml() {
    qmlRegisterModule("GeoWork", 1, 0);
    qmlRegisterSingletonType<GeoWork>(
        "GeoWork", 1, 0, "GeoWork",
        [](QQmlEngine*, QJSEngine*) -> QObject* {
            // WARNING: Leave this instance heap-allocated;
            // Qt manually deletes these things.
            static GeoWork* instance { new GeoWork {} };
            return instance;
        }
    );
}

Q_COREAPP_STARTUP_FUNCTION(initGeoWorkQml)
