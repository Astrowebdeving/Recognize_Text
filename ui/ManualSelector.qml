import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: selector
    required property var backend
    visible: backend.selectionActive
    x: backend.desktopGeometry.x
    y: backend.desktopGeometry.y
    width: backend.desktopGeometry.width
    height: backend.desktopGeometry.height
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    modality: Qt.ApplicationModal
    title: backend.adjusting ? "Adjust capture area" : "Select capture area"

    property point dragStart: Qt.point(0, 0)
    property rect selectedRect: Qt.rect(0, 0, 0, 0)

    onVisibleChanged: {
        if (visible && backend.initialSelection.width > 0) {
            selectedRect = Qt.rect(backend.initialSelection.x - x,
                                   backend.initialSelection.y - y,
                                   backend.initialSelection.width,
                                   backend.initialSelection.height)
        } else if (visible) {
            selectedRect = Qt.rect(0, 0, 0, 0)
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: selector.backend.cancelSelection()
    }

    Rectangle { anchors.fill: parent; color: "#66060A0D" }

    Button {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 16
        width: 44
        height: 44
        z: 10
        text: "×"
        font.pixelSize: 24
        ToolTip.visible: hovered
        ToolTip.text: "Cancel selection (Escape)"
        onClicked: selector.backend.cancelSelection()
    }

    Canvas {
        id: shade
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (selector.selectedRect.width > 0) {
                ctx.strokeStyle = "#68E0B7"
                ctx.lineWidth = 2
                ctx.strokeRect(selector.selectedRect.x, selector.selectedRect.y,
                               selector.selectedRect.width, selector.selectedRect.height)
            }
        }
        Connections {
            target: selector
            function onSelectedRectChanged() { shade.requestPaint() }
        }
    }

    Rectangle {
        visible: selector.selectedRect.width > 20
        x: selector.selectedRect.x
        y: Math.max(8, selector.selectedRect.y - 36)
        width: hint.implicitWidth + 18
        height: 28
        radius: 8
        color: "#E611181D"
        Text {
            id: hint
            anchors.centerIn: parent
            text: Math.round(selector.selectedRect.width) + " × " + Math.round(selector.selectedRect.height)
            color: "white"
            font.pixelSize: 12
        }
    }

    MouseArea {
        anchors.fill: parent
        z: 1
        cursorShape: Qt.CrossCursor
        onPressed: mouse => {
            selector.dragStart = Qt.point(mouse.x, mouse.y)
            selector.selectedRect = Qt.rect(mouse.x, mouse.y, 0, 0)
        }
        onPositionChanged: mouse => {
            if (!pressed) return
            selector.selectedRect = Qt.rect(Math.min(selector.dragStart.x, mouse.x),
                                             Math.min(selector.dragStart.y, mouse.y),
                                             Math.abs(mouse.x - selector.dragStart.x),
                                             Math.abs(mouse.y - selector.dragStart.y))
        }
        onReleased: {
            const r = selector.selectedRect
            selector.backend.completeSelection(Qt.rect(r.x + selector.x, r.y + selector.y,
                                                       r.width, r.height))
        }
    }
}
