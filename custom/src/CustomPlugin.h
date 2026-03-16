#include "QGCCorePlugin.h"
#include <QObject>

class CustomPlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    CustomPlugin(QGCApplication* app, QGCToolbox *toolbox);
    ~CustomPlugin() = default;

    QQmlApplicationEngine* createQmlApplicationEngine(QObject* parent) final;
};
