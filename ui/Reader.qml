import QtQuick

Rectangle {
    id: root
    property string text: ""
    property string status: ""
    property string confidence: ""
    property string alternative: ""
    property string textSize: "huge"
    color: "#F4F0E8"
    radius: 14

    Column {
        anchors.fill: parent
        anchors.margins: 22
        spacing: 8

        Text {
            width: parent.width
            text: root.text.length ? root.text : root.status
            color: root.text.length ? "#14171B" : "#727981"
            font.family: "SF Mono"
            font.pixelSize: root.text.length
                ? (root.textSize === "large" ? 27 : root.textSize === "maximum" ? 46 : 34)
                : 17
            font.bold: root.text.length
            font.letterSpacing: root.text.length ? 0.4 : 0
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            visible: root.confidence.length > 0
            width: parent.width
            text: root.confidence
            color: root.confidence.startsWith("Low") ? "#A34D26" : "#657069"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            visible: root.alternative.length > 0
            width: parent.width
            text: root.alternative
            color: "#A34D26"
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
