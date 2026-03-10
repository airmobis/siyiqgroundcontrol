/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick                  2.12
import QtQuick.Controls         2.4
import QtQuick.Dialogs          1.3
import QtQuick.Layouts          1.12

import QtLocation               5.3
import QtPositioning            5.3
import QtQuick.Window           2.2
import QtQml.Models             2.1

import QGroundControl               1.0
import QGroundControl.Controllers   1.0
import QGroundControl.Controls      1.0
import QGroundControl.FactSystem    1.0
import QGroundControl.FlightDisplay 1.0
import QGroundControl.FlightMap     1.0
import QGroundControl.Palette       1.0
import QGroundControl.ScreenTools   1.0
import QGroundControl.Vehicle       1.0

Item {
    id: _root

    // These mirror the stock layer API expected by FlyView.qml
    property var parentToolInsets               // provided by parent
    property var totalToolInsets:   _toolInsets // overlay exposes its insets back to parent
    property var mapControl                      // provided by parent

    // Pass-through insets object
    QGCToolInsets {
        id: _toolInsets
        leftEdgeTopInset:       parentToolInsets.leftEdgeTopInset
        leftEdgeCenterInset:    parentToolInsets.leftEdgeCenterInset
        leftEdgeBottomInset:    parentToolInsets.leftEdgeBottomInset
        rightEdgeTopInset:      parentToolInsets.rightEdgeTopInset
        rightEdgeCenterInset:   parentToolInsets.rightEdgeCenterInset
        rightEdgeBottomInset:   parentToolInsets.rightEdgeBottomInset
        topEdgeLeftInset:       parentToolInsets.topEdgeLeftInset
        topEdgeCenterInset:     parentToolInsets.topEdgeCenterInset
        topEdgeRightInset:      parentToolInsets.topEdgeRightInset
        bottomEdgeLeftInset:    parentToolInsets.bottomEdgeLeftInset
        bottomEdgeCenterInset:  parentToolInsets.bottomEdgeCenterInset
        bottomEdgeRightInset:   parentToolInsets.bottomEdgeRightInset
    }

    Component.onCompleted: {
        console.log("[geowork][qml] overlay completed, attempting autoBindVideo")
        if (typeof GeoWork !== 'undefined') GeoWork.autoBindVideo();
        try {
            console.log("[GeoWork] [QML] GeoWork object:", GeoWork)
            console.log("[GeoWork] [QML] tokenStatus:", GeoWork.tokenStatus, "deviceName:", GeoWork.deviceName)
        } catch (e) {
            console.warn("[GeoWork] [QML] GeoWork access failed:", e)
        }
        try {
            var v = QGroundControl.multiVehicleManager.activeVehicle
            console.log("[GeoWork] [QML] activeVehicle exists?", !!v)
            if (v && v.gps && v.gps.count) console.log("[GeoWork] [QML] sats:", v.gps.count.rawValue)
        } catch (e2) {
            console.warn("[GeoWork] [QML] vehicle access failed:", e2)
        }
    }

    // ---- Loader (required) – settings panel is loaded by qrc path ----
    Loader {
        id: geoPanel
        anchors.fill: _root
        asynchronous: false
        source: "qrc:/airmobis/GeoWorkSettingsPanel.qml"
        onStatusChanged: {
             console.log("[GeoWork] [QML] loader status:", status)
            if (status === Loader.Ready && item) {
                item.anchors.fill = _root
                item.visible = false
                console.log("[GeoWork] Settings panel loaded")
            } else if (status === Loader.Error) {
                console.warn("[GeoWork] [QML] settings load ERROR; status:", status, "source:", source)
            }
        }
    }

    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: GeoWork.reportLocation()
    }

    // ---- Background fetch every 5 minutes ----
    Timer {
        id: geoworkPoll
        interval: 5 * 60 * 1000
        repeat: true
        running: true
        triggeredOnStart: false
        onTriggered: {
            if (GeoWork.tokenStatus === 1) {
                var nm = GeoWork.deviceName && GeoWork.deviceName.length ? GeoWork.deviceName : "BLUE001"
                GeoWork.checkActiveTaskAndFetchState(nm)
            }
        }
    }

    // ---- Right/middle Geowork control pod (2 buttons, semi-transparent) ----
    Rectangle {
        id: geoworkPod
        width: ScreenTools.defaultFontPixelWidth * 26
        height: ScreenTools.defaultFontPixelHeight * 8
        radius: 10
        color: "#66000000"    // semi-transparent
        border.width: 3
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: ScreenTools.defaultFontPixelWidth
        z: 50

        Column {
            anchors.fill: parent
            anchors.margins: ScreenTools.defaultFontPixelWidth
            spacing: ScreenTools.defaultFontPixelHeight * 0.6

            anchors.horizontalCenter: parent.horizontalCenter

            // ---- Settings button with OK/BAD icon ----
            Button {
                id: settingsBtn
                width: parent.width - (ScreenTools.defaultFontPixelWidth * 2)
                implicitHeight: ScreenTools.defaultFontPixelHeight * 2.2

                background: Rectangle {
                    radius: 6
                    color: "#444444"      // darker grey
                    opacity: 0.95
                }

                contentItem: Row {
                    spacing: ScreenTools.defaultFontPixelWidth * 0.6
                    anchors.verticalCenter: parent.verticalCenter

                    Image {
                        id: settingsIcon
                        source: (function(){
                            try {
                                return GeoWork.tokenStatus === 1
                                    ? "qrc:/airmobis/setting_OK.svg"
                                    : "qrc:/airmobis/setting_BAD.svg"
                            } catch (e) {
                                console.warn("[GeoWork] [QML] settings icon binding error:", e)
                                return "qrc:/airmobis/setting_BAD.svg"
                            }
                        })()

                        fillMode: Image.PreserveAspectFit
                        width: ScreenTools.defaultFontPixelHeight * 1.4
                        height: width
                    }

                    Text {
                        text: "Settings"
                        color: "white"
                        font.pixelSize: ScreenTools.defaultFontPixelHeight * 1.2
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                onClicked: if (geoPanel.item) geoPanel.item.open()
            }

            // ---- Create Marker button with stateful behavior ----
            Item {
                id: createBtn
                width: parent.width - (ScreenTools.defaultFontPixelWidth * 2)
                height: ScreenTools.defaultFontPixelHeight * 2.6

                // Vehicle access (like your working baseline ~lines 140-146)
                readonly property var  vehicle:  QGroundControl.multiVehicleManager.activeVehicle
                readonly property bool connected: !!vehicle
                readonly property int  sats: connected ? vehicle.gps.count.rawValue : 0

                // GeoWork state
                readonly property bool hasToken: GeoWork.tokenStatus === 1
                readonly property bool taskActive: GeoWork.stateId && GeoWork.stateId !== ""

                // Modes
                readonly property bool modeTransparent: !hasToken
                readonly property bool modeActive: hasToken && connected && sats >= 3 && taskActive

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    opacity: createBtn.modeTransparent ? 0.20 : 1.0

                    Image {
                        id: cmIcon
                        anchors.centerIn: parent
                        fillMode: Image.PreserveAspectFit
                        width: ScreenTools.defaultFontPixelHeight * 3
                        height: width
                        visible: !createBtn.modeTransparent

                        source: createBtn.modeActive
                                ? "qrc:/airmobis/icon-active.svg"
                                : "qrc:/airmobis/icon-off.svg"
                    }

                    MouseArea {
                        anchors.fill: parent
                        enabled: !createBtn.modeTransparent
                        onClicked: {
                            if (createBtn.modeActive) {
                                GeoWork.createMarker()
                            } else if (createBtn.modeOff) {
                                // Re-fetch to check if a task started meanwhile
                                var nm = GeoWork.deviceName && GeoWork.deviceName.length ? GeoWork.deviceName : "BLUE001"
                                GeoWork.checkActiveTaskAndFetchState(nm)
                            }
                        }
                    }
                }
            }

            GridLayout {
                id: criteriaIndicator

                columns: 4

                readonly property string enabledColor: "#FFFF00"
                readonly property string disabledColor: "#B0B0B0"

                // Bearer token.
                Label {
                    text: "TOK"
                    font.bold: true
                    color: createBtn.hasToken ? criteriaIndicator.enabledColor : criteriaIndicator.disabledColor
                    Layout.fillWidth: true
                }

                // Vehicular connection.
                Label {
                    text: "CON"
                    font.bold: true
                    color: createBtn.connected ? criteriaIndicator.enabledColor : criteriaIndicator.disabledColor
                    Layout.fillWidth: true
                }

                // Active task.
                Label {
                    text: "TSK"
                    font.bold: true
                    color: createBtn.taskActive ? criteriaIndicator.enabledColor : criteriaIndicator.disabledColor
                    Layout.fillWidth: true
                }

                // Sufficient sattelite connectivity.
                Label {
                    text: "SAT"
                    font.bold: true
                    color: createBtn.sats >= 3 ? criteriaIndicator.enabledColor : criteriaIndicator.disabledColor
                    Layout.fillWidth: true
                }
            }
        }
    }

    Timer {
        id: geoworkHeartbeat
        interval: 3000; running: true; repeat: true
        // onTriggered: console.log("[geowork][qml] heartbeat; has GeoWork:", typeof GeoWork !== 'undefined')
    }

}
