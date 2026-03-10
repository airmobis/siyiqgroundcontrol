#include "GeoWork.h"

#include <QtWidgets/qapplication.h>

#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QGeoCoordinate>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <QVector>
#include <string_view>

#include "MultiVehicleManager.h"
#include "QGCApplication.h"
#include "Vehicle.h"

namespace {
    // REST endpoints.
    constexpr std::string_view
        kUrlSession { "https://api.GeoWork.mobis1.com/vehicles-reporting/session" },
        kUrlState { "https://api.GeoWork.mobis1.com/vehicles-reporting/project-marker-state" },
        kUrlMarker { "https://api.GeoWork.mobis1.com/vehicles-reporting/markers/create" },
        kUrlReportLocation { "https://api.GeoWork.mobis1.com/vehicles-reporting/report-location" };

    // Drop-in replacement for the eponymous C++17 STL function.
    template <typename E>
    constexpr std::underlying_type_t<E> to_underlying(E e) {
        return static_cast<std::underlying_type_t<E>>(e);
    }
}

// ---------------------------------------------------------

GeoWork::GeoWork(QObject* parent)
    : QObject { parent }
    , _videoItemObj { nullptr }
    , _tokenStatus { TokenStatus::None }
    , _settings { "Airmobis", "QGroundControl" } {
    loadSettings();
}

GeoWork* GeoWork::instance() {
    static GeoWork gw;
    return &gw;
}

QByteArray GeoWork::authHeader() const {
    if (_bearerToken.isEmpty()) {
        return QByteArray {};
    }

    QString t { _bearerToken.trimmed() };
    if (!t.startsWith(QStringLiteral("Bearer "))) {
        t = QStringLiteral("Bearer ") + t;
    }

    return t.toUtf8();
}

// Read active vehicle coordinate via QGC singletons
bool GeoWork::_getActiveVehicleCoordinate(double& latOut, double& lonOut, double& altOut) const {
    latOut = lonOut = altOut = 0.0;

    if (qgcApp() == nullptr) {
        qWarning() << "[GeoWork] qgcApp not ready";
        return false;
    }

    MultiVehicleManager* mvm { qgcApp()->toolbox()->multiVehicleManager() };
    if (mvm == nullptr) {
        qWarning() << "[GeoWork] MultiVehicleManager not available";
        return false;
    }

    Vehicle* vehicle { mvm->activeVehicle() };
    if (vehicle == nullptr) {
        qWarning() << "[GeoWork] No active vehicle";
        return false;
    }

    const QGeoCoordinate coord { vehicle->coordinate() };
    if (!coord.isValid()) {
        qWarning() << "[GeoWork] Active vehicle coordinate invalid";
        return false;
    }

    latOut = coord.latitude();
    lonOut = coord.longitude();
    altOut = coord.altitude(); // Possibly NaN if not provided.

    return true;
}

// ======================= Settings =========================

void GeoWork::setDeviceName(const QString& name) {
    const QString n { name.trimmed() };
    if (_deviceName == n)
        return;

    _deviceName = n;
    emit deviceNameChanged();
    saveSettings();
}

void GeoWork::setBearerToken(const QString& token) {
    QString t { token.trimmed() };
    if (!t.startsWith(QStringLiteral("Bearer "))) {
        t = QStringLiteral("Bearer ") + t;
    }

    if (_bearerToken == t)
        return;

    _bearerToken = t;
    emit bearerTokenChanged();

    // Keep a basic presence-based status until validateToken() sets a final state
    TokenStatus newStatus { _bearerToken.isEmpty() ? TokenStatus::None : _tokenStatus };
    if (newStatus != _tokenStatus) {
        _tokenStatus = newStatus;
        emit tokenStatusChanged();
    }

    saveSettings();
}

namespace {
    QString getLocal(const QString& fileUrl) {
        const QUrl url { fileUrl };
        return url.isValid() ? url.toLocalFile() : QString {};
    }
}

bool GeoWork::setBearerTokenFromFile(const QString& fileUrl) {
    // Accept both file:// URL and plain path
    QString local { getLocal(fileUrl) };
    QFile   f { local.isEmpty() ? fileUrl : local };

    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[GeoWork] Cannot open token file:" << fileUrl << f.errorString();
        return false;
    }

    QString txt { QString::fromUtf8(f.readAll()).trimmed() };
    f.close();

    // Strip UTF-8 BOM if present
    if (!txt.isEmpty() && txt.at(0) == QChar(0xFEFF)) {
        txt.remove(0, 1);
    }

    if (txt.isEmpty()) {
        qWarning() << "[GeoWork] Token file is empty:" << fileUrl;
        if (_tokenStatus != TokenStatus::None) {
            _tokenStatus = TokenStatus::None;
            emit tokenStatusChanged();
        }

        return false;
    }

    setBearerToken(txt); // normalizes + persists
    qInfo() << "[GeoWork] Token loaded from" << fileUrl << "len:" << txt.size();

    validateToken(); // set tokenStatus (0/1/2)

    return true;
}

void GeoWork::validateToken() {
    // TODO Replace with a real server validation endpoint if available.
    TokenStatus newStatus { TokenStatus::None }; // NoToken
    if (_bearerToken.isEmpty()) {
        newStatus = TokenStatus::None;
    } else if (_bearerToken.startsWith(QStringLiteral("Bearer ")) && _bearerToken.size() > 40) {
        newStatus = TokenStatus::Valid;
    } else {
        newStatus = TokenStatus::Invalid;
    }

    if (newStatus != _tokenStatus) {
        _tokenStatus = newStatus;
        emit tokenStatusChanged();
    }

    saveSettings();
}

void GeoWork::saveSettings() {
    _settings.setValue(QStringLiteral("deviceName"), _deviceName);
    _settings.setValue(QStringLiteral("bearerToken"), _bearerToken);
    _settings.setValue(QStringLiteral("tokenStatus"), to_underlying(_tokenStatus));
    _settings.sync();
}

void GeoWork::loadSettings() {
    const QString     dn { _settings.value(QStringLiteral("deviceName")).toString() };
    const QString     tk { _settings.value(QStringLiteral("bearerToken")).toString() };
    const TokenStatus st { static_cast<TokenStatus>(_settings.value(QStringLiteral("tokenStatus"), 0).toUInt()) };

    bool any { false };

    if (_deviceName != dn) {
        _deviceName = dn;
        emit deviceNameChanged();
        any = true;
    }

    if (_bearerToken != tk) {
        _bearerToken = tk;
        emit bearerTokenChanged();
        any = true;
    }

    if (_tokenStatus != st) {
        _tokenStatus = st;
        emit tokenStatusChanged();
        any = true;
    }

    if (any) {
        qInfo() << "[GeoWork] Settings loaded. deviceName =" << _deviceName << " tokenLen =" << _bearerToken.size()
                << " tokenStatus =" << to_underlying(_tokenStatus);
    }
}

// ======================= Network ops ======================

void GeoWork::checkActiveTaskAndFetchState(const QString& stateName) {
    // Cache device name for later use
    if (_deviceName != stateName) {
        _deviceName = stateName.trimmed();
        emit deviceNameChanged();
        saveSettings();
    }

    if (_bearerToken.isEmpty()) {
        qWarning() << "[GeoWork] No bearer token set. Open Geowork Settings and load a token file.";
        if (_tokenStatus != TokenStatus::None) {
            _tokenStatus = TokenStatus::None;
            emit tokenStatusChanged();
        }

        return;
    }

    // 1) Check the current session for an active task
    QNetworkRequest req { QUrl { QString::fromUtf8(kUrlSession.data()) } };
    req.setRawHeader("Authorization", authHeader());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* replySession { _nam.get(req) };

    connect(replySession, &QNetworkReply::finished, this, [this, replySession]() {
        const QByteArray body { replySession->readAll() };

        if (replySession->error() != QNetworkReply::NoError) {
            qWarning() << "[GeoWork] /session error:" << replySession->errorString() << "payload:" << body;
            replySession->deleteLater();

            return;
        }

        QJsonParseError     jerr;
        const QJsonDocument doc { QJsonDocument::fromJson(body, &jerr) };

        if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "[GeoWork] /session JSON parse error:" << jerr.errorString();
            replySession->deleteLater();

            return;
        }

        const QJsonObject root { doc.object() };
        const QJsonObject data { root.value(QStringLiteral("data")).toObject() };
        const QJsonObject activeTask { data.value(QStringLiteral("activeTask")).toObject() };

        if (activeTask.isEmpty()) {
            qInfo() << "[GeoWork] No active task.";
            _projectId.clear();
            emit projectIdChanged();
        } else {
            const QString newProjectId { activeTask.value(QStringLiteral("projectId")).toString() };
            if (_projectId != newProjectId) {
                _projectId = newProjectId;
                emit projectIdChanged();
            }

            qInfo() << "[GeoWork] Active task projectId:" << _projectId;
        }

        replySession->deleteLater();

        // 2) Resolve state for this device/name
        QNetworkRequest reqState { QUrl { QString::fromUtf8(kUrlState.data()) } };
        reqState.setRawHeader("Authorization", authHeader());
        reqState.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject payloadState;
        payloadState.insert(QStringLiteral("name"), _deviceName);

        const QByteArray jsonBody { QJsonDocument(payloadState).toJson(QJsonDocument::Compact) };
        QNetworkReply*   replyState { _nam.post(reqState, jsonBody) };

        connect(replyState, &QNetworkReply::finished, this, [this, replyState]() {
            const QByteArray body2 { replyState->readAll() };

            if (replyState->error() != QNetworkReply::NoError) {
                qWarning() << "[GeoWork] /project-marker-state error:" << replyState->errorString()
                           << "payload:" << body2;
                replyState->deleteLater();

                return;
            }

            QJsonParseError     jerr;
            const QJsonDocument doc2 { QJsonDocument::fromJson(body2, &jerr) };
            if (jerr.error != QJsonParseError::NoError || !doc2.isObject()) {
                qWarning() << "[GeoWork] /project-marker-state JSON parse error:" << jerr.errorString();
                replyState->deleteLater();

                return;
            }

            const QJsonObject root2 { doc2.object() };
            const QJsonObject data2 { root2.value(QStringLiteral("data")).toObject() };
            const QJsonObject state { data2.value(QStringLiteral("state")).toObject() };

            const QString newStateId { state.value(QStringLiteral("id")).toString() };

            if (_stateId != newStateId) {
                _stateId = newStateId;
                emit stateIdChanged();
            }

            if (_stateId.isEmpty()) {
                qWarning() << "[GeoWork] state.id missing in response";
            } else {
                qInfo() << "[GeoWork] stateId:" << _stateId;
            }

            replyState->deleteLater();
        });
    });
}

void GeoWork::createMarker() {
    if (_bearerToken.isEmpty()) {
        qWarning() << "[GeoWork] createMarker(): No bearer token set.";
        return;
    }
    if (_stateId.isEmpty()) {
        qWarning() << "[GeoWork] createMarker(): stateId is empty. Run checkActiveTaskAndFetchState() first.";
        return;
    }

    double lat = 0.0, lon = 0.0, alt = 0.0;
    if (!_getActiveVehicleCoordinate(lat, lon, alt)) {
        qWarning() << "[GeoWork] createMarker(): No valid GPS from active vehicle.";
        return;
    }

    // Build GeoJSON point: [lon, lat] (note order)
    QJsonObject geom;
    geom.insert(QStringLiteral("type"), QStringLiteral("Point"));

    QJsonArray coords;
    coords.append(lon);
    coords.append(lat);

    geom.insert(QStringLiteral("coordinates"), coords);

    // Payload
    QJsonObject payload;
    payload.insert(QStringLiteral("stateId"), _stateId);
    payload.insert(QStringLiteral("geometry"), geom);

    QNetworkRequest req { QUrl { QString::fromUtf8(kUrlMarker.data()) } };
    req.setRawHeader("Authorization", authHeader());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const QByteArray body { QJsonDocument(payload).toJson(QJsonDocument::Compact) };
    QNetworkReply*   reply { _nam.post(req, body) };

    connect(reply, &QNetworkReply::finished, this, [this, reply, lat, lon]() {
        const QByteArray resp { reply->readAll() };

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "[GeoWork] /markers/create error:" << reply->errorString() << "payload:" << resp;
            reply->deleteLater();

            return;
        }

        QJsonParseError     jerr;
        const QJsonDocument doc { QJsonDocument::fromJson(resp, &jerr) };

        if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "[GeoWork] /markers/create JSON parse error:" << jerr.errorString();
            reply->deleteLater();
            return;
        }

        const QJsonObject root { doc.object() };
        const QJsonObject data { root.value(QStringLiteral("data")).toObject() };
        const QJsonObject markerObj { data.value(QStringLiteral("marker")).toObject() };
        const QString     markerId { markerObj.value(QStringLiteral("id")).toString() };

        if (!markerId.isEmpty()) {
            qInfo() << "[GeoWork] Marker created at" << lat << "," << lon << " id:" << markerId;
        } else {
            qInfo() << "[GeoWork] Marker created (no id field)";
        }

        qInfo() << "[GeoWork] createMarker(): success, calling AddPhoto";
        AddPhotoForMarker(markerId);

        reply->deleteLater();
    });
}

// Safely get a nested Fact rawValue: vehicle.<group>.<fact>.rawValue
// Returns {true, value} on success; {false, 0} if any link in the chain is missing.
static std::pair<bool, double> getFactRawDouble(QObject* groupObj, std::string_view factName) {
    if (groupObj == nullptr) {
        return { false, 0.0 };
    }

    const QVariant factVar { groupObj->property(factName.data()) };
    const QObject* factObj { factVar.value<QObject*>() };

    if (!factObj)
        return { false, 0.0 };

    const QVariant raw { factObj->property("rawValue") };
    if (!raw.isValid()) {
        return { false, 0.0 };
    }

    return { true, raw.toDouble() };
}

// Convenience: pull a QObject* sub-object by name from vehicle (e.g., "gps", "battery", …)
static QObject* vehicleSubObject(QObject* v, const char* name) {
    if (v == nullptr) {
        return nullptr;
    }

    return v->property(name).value<QObject*>();
}

// ---- Helpers for robust fact lookup ----
static std::pair<bool, double> getAnyPressure(QObject* groupObj) {
    if (!groupObj) {
        return { false, 0.0 };
    }

    constexpr std::string_view names[] {
        "absPressure",
        "absolutePressure",
        "baroPressure",
        "pressure",
        "staticPressure",
        "pressAbs",
        "ambPressure"
    };

    for (const std::string_view n : names) {
        const auto res = getFactRawDouble(groupObj, n);
        if (res.first) {
            return res;
        }
    }

    return { false, 0.0 };
}

static QVector<QObject*> collectBatteryGroups(QObject* vehicle) {
    QVector<QObject*> out;
    if (!vehicle) {
        return out;
    }

    constexpr std::string_view names[] = {
        "battery",
        "battery1",
        "battery2",
        "battery3"
    };

    for (const std::string_view n : names) {
        QVariant v { vehicle->property(n.data()) };
        if (!v.isValid()) {
            continue;
        }

        if (QObject * o { v.value<QObject*>() }) {
            out.append(o);
        }
    }

    // Also try "batteries" list model, if present.
    QVariant lstVar { vehicle->property("batteries") };
    if (lstVar.isValid()) {
        if (QObject * listObj { lstVar.value<QObject*>() }; listObj != nullptr) {
            const auto children { listObj->findChildren<QObject*>(QString(), Qt::FindDirectChildrenOnly) };
            for (QObject* c : children) {
                if (c != nullptr) {
                    out.append(c);
                }
            }
        }
    }

    // Deduplicate and clamp to 3
    QSet<QObject*>    seen;
    QVector<QObject*> dedup;

    for (QObject* o : out) {
        if (o && !seen.contains(o)) {
            seen.insert(o);
            dedup.append(o);
        }
    }

    while (dedup.size() > 3) {
        dedup.removeLast();
    }

    return dedup;
}

namespace {
    int getSatelliteAmount(QObject* gpsObj) {
        auto [ok, v] = getFactRawDouble(gpsObj, "count");
        return ok ? static_cast<int>(v) : 0;
    }
}

void GeoWork::reportLocation() {
    // --- Guards ---
    if (_bearerToken.isEmpty()) {
        return;
    }

    if (!qgcApp()) {
        return;
    }

    MultiVehicleManager* mvm { qgcApp()->toolbox()->multiVehicleManager() };
    if (!mvm) {
        return;
    }

    Vehicle* vehicle { mvm->activeVehicle() };
    if (!vehicle) {
        return;
    }

    // --- GPS position (required) ---
    const QGeoCoordinate coord { vehicle->coordinate() };
    if (!coord.isValid()) {
        return;
    }

    const double lat { coord.latitude() };
    const double lon { coord.longitude() };
    const double alt { coord.altitude() }; // may be NaN; fine in JSON as string

    // --- Timestamp with timezone, ISO 8601 (like Python astimezone().isoformat()) ---
    const QString timestamp { QDateTime::currentDateTime().toString(Qt::ISODateWithMs) };

    // --- Pull additional telemetry via generic Fact access (defensive) ---
    // gps.count
    QObject* gpsObj { vehicleSubObject(vehicle, "gps") };
    int      gpsSatellites { getSatelliteAmount(gpsObj) };

    // groundSpeed (Vehicle usually exposes a Q_PROPERTY "groundSpeed" in m/s)
    double groundSpeed { vehicle->property("groundSpeed").toDouble() };

    // heading (deg)
    double heading { vehicle->property("heading").toDouble() };

    // heading (deg)
    // battery facts (supports up to 3 packs)
    QVector<QObject*> batGroups { collectBatteryGroups(vehicle) };
    double            batVoltage { qQNaN() };
    double            batCurrent { qQNaN() };
    double            batConsumed { qQNaN() }; // QGC usually exposes "mahConsumed" (mAh)
    QList<double>     batVoltages, batCurrents, batMah;

    for (int i { 0 }; i < batGroups.size() && i < 3; ++i) {
        QObject* b     = batGroups[i];
        auto [okV, vV] = getFactRawDouble(b, "voltage");
        auto [okC, vC] = getFactRawDouble(b, "current");
        auto [okM, vM] = getFactRawDouble(b, "mahConsumed");

        if (!okM) {
            auto alt = getFactRawDouble(b, "mah_consumed");
            if (alt.first) {
                okM = true;
                vM  = alt.second;
            }
        }

        if (okV)
            batVoltages.append(vV);

        if (okC)
            batCurrents.append(vC);

        if (okM)
            batMah.append(vM);
    }

    if (!batVoltages.isEmpty())
        batVoltage = batVoltages.first();

    if (!batCurrents.isEmpty())
        batCurrent = batCurrents.first();

    if (!batMah.isEmpty())
        batConsumed = batMah.first();

    // absolute pressure (try several groups and names)
    double pressAbs = qQNaN();

    {
        QObject* envObj { vehicleSubObject(vehicle, "environment") };
        QObject* airObj { vehicleSubObject(vehicle, "air") };
        QObject* sensorsObj { vehicleSubObject(vehicle, "sensors") };
        auto     p1 { getAnyPressure(envObj) };
        auto     p2 { getAnyPressure(airObj) };
        auto     p3 { getAnyPressure(sensorsObj) };

        if (p1.first)
            pressAbs = p1.second;

        else if (p2.first)
            pressAbs = p2.second;

        else if (p3.first)
            pressAbs = p3.second;
    }

    // --- Build payload (same structure as your Python) ---
    QJsonArray meta;

    // satellites
    meta.append(QJsonObject {
        { "key", "satellites" }, { "value", QString::number(gpsSatellites) } //,        { "unit",  "number" }
    });

    // ground speed (m/s)
    if (std::isfinite(groundSpeed)) {
        meta.append(
            QJsonObject { { "key", "ground_speed" }, { "value", QString::number(groundSpeed, 'f', 2) }, { "unit", "m/s" } }
        );
    }

    // heading (deg)
    if (std::isfinite(heading)) {
        meta.append(QJsonObject { { "key", "heading" }, { "value", QString::number(heading) }, { "unit", "deg" } });
    }

    // battery voltage (V)
    if (std::isfinite(batVoltage)) {
        meta.append(QJsonObject { { "key", "bat_voltage" }, { "value", QString::number(batVoltage) }, { "unit", "V" } });
    }

    // battery current (A)
    if (std::isfinite(batCurrent)) {
        meta.append(QJsonObject { { "key", "bat_current" }, { "value", QString::number(batCurrent) }, { "unit", "A" } });
    }

    // total consumption – QGC exposes mAh; your Python key was "current_consumed"
    if (std::isfinite(batConsumed)) {
        meta.append(QJsonObject {
            { "key", "current_consumed" }, { "value", QString::number(batConsumed) }, { "unit", "mA" }
            // clarify unit; change to "A" / "Ah" if your backend expects that
        });
        // per-pack battery values (up to 3)
        for (int i = 0; i < batVoltages.size() && i < 3; ++i) {
            meta.append(QJsonObject { { "key", QString("bat%1_voltage").arg(i + 1) },
                                      { "value", QString::number(batVoltages[i]) },
                                      { "unit", "V" } });
        }
        for (int i = 0; i < batCurrents.size() && i < 3; ++i) {
            meta.append(QJsonObject { { "key", QString("bat%1_current").arg(i + 1) },
                                      { "value", QString::number(batCurrents[i]) },
                                      { "unit", "A" } });
        }
        for (int i = 0; i < batMah.size() && i < 3; ++i) {
            meta.append(QJsonObject {
                { "key", QString("bat%1_mah").arg(i + 1) }, { "value", QString::number(batMah[i]) }, { "unit", "mA" } });
        }
    }

    // absolute pressure (if available)
    if (std::isfinite(pressAbs)) {
        meta.append(QJsonObject { { "key", "press_abs" }, { "value", QString::number(pressAbs, 'f', 2) }, { "units", "Pa" } });
    }

    // altitude (from coordinate)
    if (std::isfinite(alt)) {
        meta.append(QJsonObject { { "key", "altitude" }, { "value", QString::number(alt, 'f', 1) }, { "units", "m" } });
    }

    QJsonObject
        loc { { "latitude", lat }, { "longitude", lon }, { "timestamp", timestamp }, { "meta", meta } },
        payload { { "locations", QJsonArray { loc } } };

    // --- POST it ---
    QNetworkRequest req { QUrl { QString::fromUtf8(kUrlReportLocation.data()) } };
    req.setRawHeader("Authorization", authHeader()); // "Bearer …"
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const QByteArray jsonBody { QJsonDocument(payload).toJson(QJsonDocument::Compact) };
    QNetworkReply*   nrep { _nam.post(req, jsonBody) };

    connect(nrep, &QNetworkReply::finished, this, [nrep]() {
        const QByteArray r { nrep->readAll() };

        if (nrep->error() != QNetworkReply::NoError) {
            qWarning() << "[GeoWork] /report-location error:" << nrep->errorString() << "payload:" << r;
            nrep->deleteLater();

            return;
        }

        // qInfo() << "[GeoWork] /report-location OK";
        nrep->deleteLater();
    });
}

// ======== Minimal additions: frame capture to Downloads ========
void GeoWork::setVideoItem(QObject* videoItem) {
    _videoItemObj = videoItem;
    qInfo() << "[GeoWork] setVideoItem:" << videoItem;
}

void GeoWork::AddPhoto() {
    qInfo() << "[GeoWork] AddPhoto(): invoked";

    // Must run on GUI thread for grabToImage
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, &GeoWork::AddPhoto, Qt::QueuedConnection);

        return;
    }

    _captureAndSave();
}

void GeoWork::_captureAndSave() {
    qInfo() << "[GeoWork] _captureAndSave(): begin";
    QQuickItem* item { qobject_cast<QQuickItem*>(_videoItemObj) };
    if (!item) {
        qWarning() << "[GeoWork] AddPhoto: video item not set";
        emit photoSaveFailed(QStringLiteral("Video item not set. Call setVideoItem(...) first."));

        return;
    }

    QSharedPointer<QQuickItemGrabResult> grab = item->grabToImage();
    if (!grab) {
        qWarning() << "[GeoWork] AddPhoto: grabToImage returned null";
        emit photoSaveFailed(QStringLiteral("grabToImage returned null."));

        return;
    }

    QObject::connect(grab.data(), &QQuickItemGrabResult::ready, this, [this, grab]() {
        const QImage img { grab->image() };
        if (img.isNull()) {
            qWarning() << "[GeoWork] AddPhoto: captured image is null";
            emit photoSaveFailed(QStringLiteral("Captured image is null."));
            return;
        }

        // Target: Herelink Downloads directory (Android)
        QString downloads { QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) };
        if (downloads.isEmpty()) {
            // Fallback to Pictures or home
            downloads = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            if (downloads.isEmpty()) {
                downloads = QDir::homePath();
            }
        }

        QDir {}.mkpath(downloads);

        const QString ts { QDateTime::currentDateTime().toString("yyyyMMdd_hhmmsszzz") };
        const QString filePath { downloads + QStringLiteral("/GeoWork_%1.jpg").arg(ts) };

        QImageWriter writer { filePath.toUtf8(), "jpeg" };
        writer.setQuality(90);
        if (!writer.write(img)) {
            emit photoSaveFailed(QStringLiteral("Failed to write jpg: ") + writer.errorString());

            return;
        }

        qInfo() << "[GeoWork] Saved frame to" << filePath;

        emit photoSaved(filePath);
    });
}

// ======== GeoWork: auto-bind video item by scanning QML scene ========
void GeoWork::autoBindVideo() {
    qInfo() << "[GeoWork] autoBindVideo(): start";

    const QWindowList wins { QGuiApplication::allWindows() };
    for (QWindow* w : wins) {
        QQuickWindow* qw { qobject_cast<QQuickWindow*>(w) };
        if (!qw)
            continue;

        QQuickItem* root { qw->contentItem() };
        if (!root)
            continue;

        QQuickItem* hit { _findVideoItemRecursive(root) };
        if (hit) {
            _videoItemObj = hit;

            qInfo() << "[GeoWork] autoBindVideo(): found item" << hit << "objectName=" << hit->objectName()
                    << "class=" << hit->metaObject()->className();
            return;
        }
    }

    qWarning() << "[GeoWork] autoBindVideo(): no video item found";
}

QQuickItem* GeoWork::_findVideoItemRecursive(QQuickItem* item) const {
    if (item == nullptr) {
        return nullptr;
    }

    const QByteArray cname { item->metaObject()->className() };
    const QString    oname { item->objectName() };

    // Heuristics: look for GLVideoItem, VideoItem, VideoBackground, or names containing "video"
    const bool
        classLooksVideo { cname.contains("GLVideoItem") || cname.contains("VideoItem") || cname.contains("VideoBackground") },
        nameLooksVideo { oname.contains("video", Qt::CaseInsensitive) };

    if (classLooksVideo || nameLooksVideo) {
        return item;
    }

    const QList<QQuickItem*> children { item->childItems() };
    for (QQuickItem* c : children) {
        if (QQuickItem* r = _findVideoItemRecursive(c))
            return r;
    }

    return nullptr;
}

void GeoWork::AddPhotoForMarker(const QString& markerId) {
    qInfo() << "[GeoWork] AddPhotoForMarker(): invoked markerId=" << markerId;
    if (markerId.isEmpty()) {
        qWarning() << "[GeoWork] AddPhotoForMarker(): empty markerId";
        return;
    }

    // Ensure GUI thread for grabToImage.
    if (QThread::currentThread() != qApp->thread()) {
        QMetaObject::invokeMethod(this, [this, markerId]() { AddPhotoForMarker(markerId); }, Qt::QueuedConnection);
        return;
    }

    QQuickItem* item { qobject_cast<QQuickItem*>(_videoItemObj) };
    if (!item) {
        qWarning() << "[GeoWork] AddPhotoForMarker(): video item not set";
        return;
    }

    QSharedPointer<QQuickItemGrabResult> grab { item->grabToImage() };
    if (!grab) {
        qWarning() << "[GeoWork] AddPhotoForMarker(): grabToImage returned null";
        return;
    }

    QObject::connect(grab.data(), &QQuickItemGrabResult::ready, this, [this, markerId, grab]() {
        const QImage img { grab->image() };
        if (img.isNull()) {
            qWarning() << "[GeoWork] AddPhotoForMarker(): captured image is null";
            return;
        }

        // Save to Downloads as before (user requirement)
        QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (downloads.isEmpty()) {
            downloads = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            if (downloads.isEmpty())
                downloads = QDir::homePath();
        }

        QDir().mkpath(downloads);

        const QString ts { QDateTime::currentDateTime().toString("yyyyMMdd_hhmmsszzz") };
        const QString filePath { downloads + QStringLiteral("/GeoWork_%1.jpg").arg(ts) };

        QImageWriter writer(filePath.toUtf8(), "jpeg");
        writer.setQuality(90);

        if (!writer.write(img)) {
            qWarning() << "[GeoWork] AddPhotoForMarker(): failed to write JPEG:" << writer.errorString();
            return;
        }

        qInfo() << "[GeoWork] Saved frame to" << filePath;

        // Then upload it to a marker (like Python).
        _uploadPhotoToMarker(markerId, filePath);
    });
}

void GeoWork::_uploadPhotoToMarker(const QString& markerId, const QString& photoPath) {
    if (_bearerToken.isEmpty()) {
        qWarning() << "[GeoWork] _uploadPhotoToMarker(): No bearer token set";
        return;
    }

    QFileInfo fi { photoPath };
    if (!fi.exists() || !fi.isFile()) {
        qWarning() << "[GeoWork] _uploadPhotoToMarker(): file missing" << photoPath;
        return;
    }

    // No recompress: send the captured file as-is.
    QString pathToUpload { photoPath };

    // Build URL: .../vehicles-reporting/markers/add-image?marker=<id>
    QUrl      url(QString::fromUtf8("https://api.GeoWork.mobis1.com/vehicles-reporting/markers/add-image"));
    QUrlQuery q;

    q.addQueryItem(QStringLiteral("marker"), markerId);
    url.setQuery(q);

    QNetworkRequest req { url };
    req.setRawHeader("Authorization", authHeader());
    // No Content-Type header here; QHttpMultiPart sets it with the boundary

    QFile& file { *new QFile(pathToUpload, this) };

    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "[GeoWork] _uploadPhotoToMarker(): cannot open" << pathToUpload;
        file.deleteLater();

        return;
    }

    QHttpMultiPart& multi { *new QHttpMultiPart(QHttpMultiPart::FormDataType, this) };
    QHttpPart       filePart;

    filePart.setHeader(
        QNetworkRequest::ContentDispositionHeader,
        QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"")
                     .arg(fi.fileName()))
    );

    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("image/jpeg")));
    filePart.setBodyDevice(&file);
    file.setParent(&multi);
    multi.append(filePart);

    qInfo() << "[GeoWork] POST" << url.toString() << "file=" << fi.fileName();

    QNetworkReply& reply { *_nam.post(req, &multi) };

    multi.setParent(&reply);
    reply.setProperty("gw_markerId", markerId);
    reply.setProperty("gw_photoPath", photoPath);

    connect(&reply, &QNetworkReply::finished, this, [&reply]() {
        const QString
            markerId { reply.property("gw_markerId").toString() },
            photoPath { reply.property("gw_photoPath").toString() };

        if (reply.error() != QNetworkReply::NoError) {
            qWarning() << "[GeoWork] /markers/add-image error:" << reply.errorString() << "markerId =" << markerId;
        } else {
            const QByteArray body = reply.readAll();
            qInfo() << "[GeoWork] /markers/add-image OK, bytes:" << body.size() << "markerId =" << markerId;
        }

        reply.deleteLater();
    });
}
