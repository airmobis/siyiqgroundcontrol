#pragma once

#include <QGeoCoordinate>
#include <QNetworkAccessManager>
#include <QObject>
#include <QJsonArray>
#include <QSettings>
#include <QtQml/qqml.h>

class QQuickItem;

class GeoWork : public QObject {
private:
    Q_OBJECT

    Q_PROPERTY(QString projectId READ projectId NOTIFY projectIdChanged)
    Q_PROPERTY(QString stateId READ stateId NOTIFY stateIdChanged)

    // User-configurable
    Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
    Q_PROPERTY(QString bearerToken READ bearerToken WRITE setBearerToken NOTIFY bearerTokenChanged)

    // 0 = NoToken (grey), 1 = Valid (green), 2 = Invalid (red)
    Q_PROPERTY(int tokenStatus READ tokenStatus NOTIFY tokenStatusChanged)

    Q_PROPERTY(QJsonArray projectStates READ projectStates NOTIFY projectStatesChanged)

    QML_SINGLETON
    QML_ELEMENT

public:
    enum class TokenStatus : std::uint8_t {
        None,
        Valid,
        Invalid
    };

    explicit GeoWork(QObject* parent = nullptr);

    static GeoWork* instance();

    // Getters for QML

    QString 	projectId() const { return _projectId; }
    QString 	stateId() const { return _stateId; }
    QString 	deviceName() const { return _deviceName; }
    QString 	bearerToken() const { return _bearerToken; }
    int     	tokenStatus() const { return static_cast<int>(_tokenStatus); }
    QJsonArray 	projectStates() const { return _states; }

public slots:
    // --- Main flow used by QML ---
    // 1) Check session; if active task, POST project-marker-state {name}; cache stateId
    void checkActiveTaskAndFetchState(const QString& stateName);

    // 2) Create a marker using current GPS from active vehicle
    void createMarker();

    // ---- Minimal additions ----
    Q_INVOKABLE void setVideoItem(QObject* videoItem); // bind the live video surface from QML
    Q_INVOKABLE void AddPhoto();
    Q_INVOKABLE void AddPhotoForMarker(const QString& markerId);
    Q_INVOKABLE void autoBindVideo(); // try to locate video item automatically

    // --- Settings helpers (persisted via QSettings) ---
    void setDeviceName(const QString& name);
    void setBearerToken(const QString& token);           // accepts token with or without "Bearer "
    bool setBearerTokenFromFile(const QString& fileUrl); // read token text from file URL or path

    // Token validation (heuristic placeholder; swap for real endpoint later)
    void validateToken();

    // Explicit persist/load if you want to call from elsewhere
    void saveSettings();
    void loadSettings();

    void reportLocation();

signals:
    void projectIdChanged();
    void stateIdChanged();
    void deviceNameChanged();
    void bearerTokenChanged();
    void tokenStatusChanged();
    void projectStatesChanged();
    void photoSaved(const QString& savedPath);
    void photoSaveFailed(const QString& reason);

private:
    QByteArray authHeader() const;

    // --- Minimal additions for frame capture ---
    void        _captureAndSave();
    void        _uploadPhotoToMarker(const QString& markerId, const QString& photoPath);
    QQuickItem* _findVideoItemRecursive(QQuickItem* root) const; // internal helper
    QObject*    _videoItemObj { nullptr };

    // Returns true if we could read a valid lat/lon from QGC's active vehicle
    bool _getActiveVehicleCoordinate(double& latOut, double& lonOut, double& altOut) const;

    QNetworkAccessManager _nam;

    // Cached runtime state
    QString _projectId;
    QString _stateId;

    // Persisted user settings
    QString _deviceName;      // e.g., "BLUE001"
    QString _bearerToken;     // normalized to start with "Bearer "

    TokenStatus _tokenStatus;

    // Organization/App for QSettings (Android/desktop-safe)
    QSettings _settings { "Airmobis", "QGroundControl" };

    QJsonArray _states;
};
