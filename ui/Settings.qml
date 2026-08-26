// qmllint disable unqualified
import QtQuick
import QtQuick.Controls

Popup {
    id: root
    width: 300
    height: 540
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: "#182027"
        border.color: "#34414A"
        radius: 14
    }

    contentItem: ScrollView {
        id: settingsScroll
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Column {
        width: settingsScroll.availableWidth
        spacing: 16
        Row {
            width: parent.width
            spacing: 8
            Text {
                text: "Settings"
                color: "white"
                font.pixelSize: 22
                font.bold: true
                width: Math.max(1, parent.width - closeButton.width - parent.spacing)
                anchors.verticalCenter: parent.verticalCenter
            }
            ToolButton {
                id: closeButton
                text: "×"
                width: 36
                height: 36
                font.pixelSize: 22
                Accessible.name: "Close settings"
                onClicked: root.close()
            }
        }
        Text { text: "CAPTURE"; color: "#83A094"; font.pixelSize: 10; font.letterSpacing: 1.4 }
        CheckBox {
            text: "Automatic capture"
            checked: controller.autoEnabled
            onToggled: controller.autoEnabled = checked
        }
        Row {
            spacing: 8
            Text { text: "Area"; color: "#C7D0D4"; width: 80; anchors.verticalCenter: parent.verticalCenter }
            ComboBox {
                width: 160
                model: ["Tight", "Normal", "Wide"]
                currentIndex: controller.captureArea === "tight" ? 0 : controller.captureArea === "wide" ? 2 : 1
                onActivated: controller.captureArea = currentIndex === 0 ? "tight" : currentIndex === 2 ? "wide" : "normal"
            }
        }
        Row {
            spacing: 8
            Text { text: "Response"; color: "#C7D0D4"; width: 80; anchors.verticalCenter: parent.verticalCenter }
            ComboBox {
                width: 160
                model: ["Responsive", "Normal", "Calm"]
                currentIndex: controller.pointerResponse === "responsive" ? 0 : controller.pointerResponse === "calm" ? 2 : 1
                onActivated: controller.pointerResponse = currentIndex === 0 ? "responsive" : currentIndex === 2 ? "calm" : "normal"
            }
        }
        Text { text: "OCR DISPLAY"; color: "#83A094"; font.pixelSize: 10; font.letterSpacing: 1.4 }
        ComboBox {
            width: parent.width
            model: ["Auto", "Reader", "Overlay"]
            currentIndex: controller.ocrDisplay === "reader" ? 1 : controller.ocrDisplay === "overlay" ? 2 : 0
            onActivated: controller.ocrDisplay = currentIndex === 1 ? "reader" : currentIndex === 2 ? "overlay" : "auto"
        }
        Row {
            spacing: 8
            Text { text: "Text size"; color: "#C7D0D4"; width: 80; anchors.verticalCenter: parent.verticalCenter }
            ComboBox {
                width: 160
                model: ["Large", "Huge", "Maximum"]
                currentIndex: controller.textSize === "large" ? 0 : controller.textSize === "maximum" ? 2 : 1
                onActivated: controller.textSize = currentIndex === 0 ? "large" : currentIndex === 2 ? "maximum" : "huge"
            }
        }
        Row {
            spacing: 8
            Text { text: "Recognition"; color: "#C7D0D4"; width: 80; anchors.verticalCenter: parent.verticalCenter }
            ComboBox {
                width: 160
                model: ["Fast", "Balanced", "Accurate"]
                currentIndex: controller.recognition === "fast" ? 0 : controller.recognition === "accurate" ? 2 : 1
                onActivated: controller.recognition = currentIndex === 0 ? "fast" : currentIndex === 2 ? "accurate" : "balanced"
            }
        }
        Text { text: "DEFAULTS"; color: "#83A094"; font.pixelSize: 10; font.letterSpacing: 1.4 }
        Text {
            text: "Document enhancement: Automatic\nDefault zoom: 16×"
            color: "#C7D0D4"
            font.pixelSize: 13
            lineHeight: 1.35
        }
        Text {
            text: "Processing stays on this device."
            color: "#68E0B7"
            font.pixelSize: 12
        }
        }
    }
}
