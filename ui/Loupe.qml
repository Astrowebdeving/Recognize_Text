pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root
    required property url imageSource
    required property size imageSize
    required property real zoom
    required property bool smoothImage
    property bool showOverlay: false
    property var overlayItems: []
    signal wheelZoom(real delta)
    signal pinchStarted()
    signal pinchZoom(real scale)

    Rectangle {
        anchors.fill: parent
        color: "#0B0E12"
        radius: 14
        clip: true

        Flickable {
            id: viewport
            anchors.fill: parent
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: Math.max(width, sourceImage.width)
            contentHeight: Math.max(height, sourceImage.height)

            Image {
                id: sourceImage
                source: root.imageSource
                cache: false
                asynchronous: false
                smooth: root.smoothImage
                mipmap: root.smoothImage
                width: Math.max(1, root.imageSize.width * root.zoom)
                height: Math.max(1, root.imageSize.height * root.zoom)
                x: Math.max(0, (viewport.width - width) / 2)
                y: Math.max(0, (viewport.height - height) / 2)

                Repeater {
                    model: root.showOverlay ? root.overlayItems : []
                    delegate: Item {
                        id: overlayDelegate
                        required property var modelData
                        x: modelData.x * sourceImage.width
                        y: modelData.y * sourceImage.height
                        width: Math.max(24 / root.zoom, modelData.width * sourceImage.width)
                        height: Math.max(16 / root.zoom, modelData.height * sourceImage.height)

                        Rectangle {
                            anchors.fill: parent
                            color: overlayDelegate.modelData.low ? "#55FFB454" : "#4424D7A6"
                            border.color: overlayDelegate.modelData.low ? "#FFB454" : "#55E6BE"
                            border.width: Math.max(1 / root.zoom, 0.1)
                            radius: 2 / root.zoom
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.bottom: parent.top
                            text: overlayDelegate.modelData.text
                            color: "white"
                            style: Text.Outline
                            styleColor: "#111820"
                            font.pixelSize: Math.max(11 / root.zoom, 1)
                        }
                    }
                }
            }

            WheelHandler {
                target: null
                onWheel: event => {
                    root.wheelZoom(event.angleDelta.y)
                    event.accepted = true
                }
            }

            PinchHandler {
                target: null
                minimumPointCount: 2
                maximumPointCount: 2
                onActiveChanged: {
                    if (active)
                        root.pinchStarted()
                }
                onScaleChanged: {
                    if (active)
                        root.pinchZoom(activeScale)
                }
            }
        }

        Rectangle {
            visible: root.zoom >= 32
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            width: gridLabel.implicitWidth + 18
            height: 28
            radius: 14
            color: "#CC111820"
            Text {
                id: gridLabel
                anchors.centerIn: parent
                text: "PIXEL LEVEL"
                color: "#8BE4C7"
                font.pixelSize: 10
                font.bold: true
                font.letterSpacing: 1.2
            }
        }
    }
}
