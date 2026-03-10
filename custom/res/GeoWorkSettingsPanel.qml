import QtQuick 2.12
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.12
import QtQuick.Dialogs 1.3
import QtQuick.Window 2.15

import GeoWork 1.0

Item {
    id: panel
    visible: false
    z: 9999

    // Public API
    function open()  { visible = true }
    function close() { visible = false }

    // Needed because we assign to panel.pickedFileUrl in onAccepted
    property string pickedFileUrl: ""

    // Dim background; click to close
    Rectangle {
        anchors.fill: parent
        color: "#AA000000"   // darker than before so it’s clearly visible
        MouseArea { anchors.fill: parent; onClicked: panel.close() }
    }

    // Centered card
    Rectangle {
        id: card
        width: Math.min(parent.width - 40, 500)
        radius: 10
        color: "white"
        border.width: 1
        anchors.centerIn: parent

        readonly property string defaultMarkerColor: "red"

        x: 32
        y: 32

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 16

            spacing: 10

            Label {
                text: "GeoWork Settings"
                font.pointSize: 24
                font.bold: true
                color: "azure"
            }

            // Token status + manual fetch
            RowLayout {
                spacing: 10

                Label {
                    text: "Token status:"
                    color: "white"
                }

                Rectangle {
                    width: 40
                    height: 40
                    radius: 5
                    border.width: 1

                    color: GeoWork.tokenStatus === 1 ? "#21A366"   // green
                         : GeoWork.tokenStatus === 2 ? "#D13438"   // red
                         : "#A0A0A0"                               // grey
                }

                Item {
                    Layout.fillWidth: true
                }

                Button {
                    text: "Fetch state (manual)"
                    onClicked: {
                        var nm = GeoWork.deviceName && GeoWork.deviceName.length ? GeoWork.deviceName : "BLUE001"
                        GeoWork.checkActiveTaskAndFetchState(nm)
                    }
                }
            }

            // Drone name (auto-fetch on change)
            Label {
                text: "Drone name (state name)"
                color: "white"
            }

            TextField {
                id: nameField
                placeholderText: "e.g. BLUE001"
                text: GeoWork.deviceName && GeoWork.deviceName.length ? GeoWork.deviceName : "BLUE001"

                Layout.fillWidth: true

                onEditingFinished: {
                    GeoWork.setDeviceName(text)                 // persist
                    GeoWork.checkActiveTaskAndFetchState(text)  // auto-fetch
                }
            }

            RowLayout {
                Layout.fillWidth: true

                TextField {
                    readOnly: true
                    Layout.fillWidth: true
                    placeholderText: "Choose marker state..."
                    text: markerStateSelector.currentText
                }

                ComboBox {
                    id: markerStateSelector
                    model: GeoWork.projectStates;
                }
            }

            // Token file selection (auto-validate + auto-fetch)
            Label {
                text: "Authorization token file"
                color: "white"
            }

            RowLayout {
                Layout.fillWidth: true

                TextField {
                    id: tokenPath
                    readOnly: true
                    Layout.fillWidth: true
                    placeholderText: "Choose a file..."
                    text: panel.pickedFileUrl
                }

                Button {
                    text: "Choose…"
                    onClicked: tokenChooser.open()
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: panel.close() }
                Button { text: "OK";     onClicked: panel.close() }
            }
        }
    }

    // Native file picker – QML does NOT read the file; C++ does.
    FileDialog {
        id: tokenChooser
        title: "Select Geowork token file"
        selectExisting: true
        onAccepted: {
            panel.pickedFileUrl = fileUrl
            if (GeoWork.setBearerTokenFromFile(fileUrl)) {
                GeoWork.validateToken()  // sets GeoWork.tokenStatus
                var nm = GeoWork.deviceName && GeoWork.deviceName.length ? GeoWork.deviceName : "BLUE001"
                GeoWork.checkActiveTaskAndFetchState(nm) // auto-fetch after token change
            }
        }
    }
}
