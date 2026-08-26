// qmllint disable unqualified
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    visible: false
    width: 880
    height: 760
    minimumWidth: 680
    minimumHeight: 620
    title: "Document Loupe"
    color: "#10161B"

    palette.window: "#10161B"
    palette.windowText: "#EAF0F2"
    palette.button: "#222C33"
    palette.buttonText: "#EAF0F2"
    palette.highlight: "#3AD1A4"

    HoverHandler {
        onHoveredChanged: controller.setPointerInside(hovered)
    }

    Shortcut { sequences: [StandardKey.ZoomIn]; onActivated: controller.zoomIn() }
    Shortcut { sequences: [StandardKey.ZoomOut]; onActivated: controller.zoomOut() }
    Shortcut { sequence: "Ctrl+Shift+Space"; onActivated: controller.selectArea() }
    Shortcut { sequence: "Meta+Shift+Space"; onActivated: controller.selectArea() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: controller.autoEnabled ? "●  Auto" : "○  Auto"
                highlighted: controller.autoEnabled
                onClicked: controller.autoEnabled = !controller.autoEnabled
            }
            Button {
                text: "⌖  Select Area"
                highlighted: true
                onClicked: controller.selectArea()
            }
            Button {
                text: "Adjust Area"
                enabled: controller.hasSource
                onClicked: controller.adjustArea()
            }
            Button {
                text: controller.pinned ? "●  Pinned" : "Pin"
                onClicked: controller.togglePin()
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: Math.min(statusText.implicitWidth + 20,
                                                Math.max(90, window.width - 565))
                Layout.maximumWidth: 240
                Layout.preferredHeight: 30
                radius: 15
                color: "#18232A"
                Text {
                    id: statusText
                    anchors.centerIn: parent
                    width: parent.width - 20
                    text: controller.captureStatus
                    color: "#9EB0B8"
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignHCenter
                }
            }
            ToolButton {
                text: "⚙"
                Accessible.name: "Settings"
                ToolTip.visible: hovered
                ToolTip.text: "Settings"
                onClicked: settings.open()
            }
            ToolButton {
                text: "×"
                Accessible.name: "Quit Document Loupe"
                font.pixelSize: 22
                ToolTip.visible: hovered
                ToolTip.text: "Quit Document Loupe"
                onClicked: Qt.quit()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 290

            Loupe {
                anchors.fill: parent
                imageSource: controller.viewMode === "document" ? controller.documentUrl : controller.sourceUrl
                imageSize: controller.sourceSize
                zoom: controller.zoom
                smoothImage: controller.viewMode !== "pixels"
                showOverlay: controller.activeOcrDisplay === "overlay"
                overlayItems: controller.overlayItems
                onWheelZoom: delta => controller.zoomByWheel(delta)
                onPinchStarted: controller.beginPinchZoom()
                onPinchZoom: scale => controller.zoomByPinch(scale)
            }

            Column {
                visible: !controller.hasSource
                anchors.centerIn: parent
                spacing: 8
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "POINT AT SOMETHING SMALL"
                    color: "#EAF0F2"
                    font.pixelSize: 18
                    font.bold: true
                    font.letterSpacing: 1.2
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Move the pointer over text, or choose Select Area"
                    color: "#81919A"
                    font.pixelSize: 14
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Select Area"
                    onClicked: controller.selectArea()
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            ButtonGroup { id: viewGroup }
            ToolButton {
                text: "Pixels"
                checkable: true
                checked: controller.viewMode === "pixels"
                ButtonGroup.group: viewGroup
                onClicked: controller.viewMode = "pixels"
            }
            ToolButton {
                text: "Document"
                checkable: true
                checked: controller.viewMode === "document"
                ButtonGroup.group: viewGroup
                onClicked: controller.viewMode = "document"
            }
            ToolButton {
                text: "AI Restore"
                checkable: true
                enabled: false
                ToolTip.visible: hovered
                ToolTip.text: "No verified restoration model is installed"
            }
            Item { Layout.fillWidth: true }
            ToolButton { text: "−"; onClicked: controller.zoomOut() }
            Rectangle {
                Layout.preferredWidth: 88
                Layout.preferredHeight: 32
                radius: 8
                color: "#1B252C"
                Text {
                    anchors.centerIn: parent
                    text: controller.zoomLabel
                    color: "#EAF0F2"
                    font.family: "SF Mono"
                    font.bold: true
                }
            }
            ToolButton { text: "+"; onClicked: controller.zoomIn() }
            ComboBox {
                Layout.preferredWidth: 128
                model: ["OCR: Auto", "Reader", "Overlay"]
                currentIndex: controller.ocrDisplay === "reader" ? 1 : controller.ocrDisplay === "overlay" ? 2 : 0
                onActivated: controller.ocrDisplay = currentIndex === 1 ? "reader" : currentIndex === 2 ? "overlay" : "auto"
            }
        }

        Reader {
            visible: controller.activeOcrDisplay === "reader"
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            text: controller.ocrText
            status: controller.ocrStatus
            confidence: controller.confidence
            alternative: controller.alternative
            textSize: controller.textSize
        }

        Pane {
            Layout.fillWidth: true
            padding: 4

            background: Rectangle {
                color: "#182129"
                border.color: "#2B3740"
                radius: 10
            }

            contentItem: RowLayout {
                spacing: 4
                Button {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: "Copy Text"
                    enabled: controller.ocrText.length > 0
                    onClicked: controller.copyText()
                }
                Button {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: "Copy Number"
                    visible: controller.numericLike
                    onClicked: controller.copyNumber()
                }
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    visible: controller.activeOcrDisplay === "overlay"
                    text: controller.ocrStatus
                    color: "#8FA1A9"
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignHCenter
                }
                Button {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: "Copy Image"
                    enabled: controller.hasSource
                    onClicked: controller.copyImage()
                }
            }
        }
    }

    Rectangle {
        visible: controller.captureStatus.indexOf("permission") >= 0 ||
                 controller.captureStatus.indexOf("Permission") >= 0
        anchors.fill: parent
        color: "#EE10161B"
        z: 50
        Column {
            anchors.centerIn: parent
            width: Math.min(480, parent.width - 60)
            spacing: 16
            Text {
                width: parent.width
                text: "Allow Screen Recording"
                color: "white"
                font.pixelSize: 26
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }
            Text {
                width: parent.width
                text: controller.captureStatus
                color: "#B9C4C8"
                font.pixelSize: 15
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Open System Settings"
                onClicked: controller.openScreenRecordingSettings()
            }
        }
    }

    ManualSelector { backend: controller }
    Settings {
        id: settings
        width: Math.max(1, Math.min(300, window.width - 32))
        height: Math.max(1, Math.min(540, window.height - 72))
        x: Math.max(8, window.width - width - 20)
        y: Math.min(56, Math.max(8, window.height - height - 8))
    }
}
