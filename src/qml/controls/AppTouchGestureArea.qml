import QtQuick

// Process a complete touch event before changing camera state. Separate drag
// and pinch handlers can finish in either order when the point count changes.
MultiPointTouchArea {
    id: root
    minimumTouchPoints: 1
    mouseEnabled: false

    property bool longPressEnabled: false
    readonly property real dragThreshold: Application.styleHints.startDragDistance
    property int pointCount: 0
    property string pointIds: ""
    property point startPosition: Qt.point(0, 0)
    property point previousPosition: Qt.point(0, 0)
    property real startDistance: 0
    property real previousDistance: 0
    property bool gestureActive: false
    property bool holdTriggered: false

    signal contactStarted()
    signal manipulationStarted(int points, point position)
    signal manipulationUpdated(int points, point previous, point position, real scale)
    signal manipulationFinished(bool canceled)
    signal longPressed(point position)

    function cancelGesture() {
        if (holdTimer)
            holdTimer.stop()
        pointCount = 0
        pointIds = ""
        holdTriggered = false
        if (gestureActive) {
            gestureActive = false
            manipulationFinished(true)
        }
    }

    function updateTouches(points) {
        const pressedPoints = []
        for (let i = 0; i < points.length; ++i) {
            if (points[i].pressed)
                pressedPoints.push(points[i])
        }
        pressedPoints.sort((a, b) => a.pointId - b.pointId)
        const count = pressedPoints.length
        const ids = pressedPoints.map(p => p.pointId).join(",")
        if (count > 2) {
            cancelGesture()
            // Remember unsupported contacts so a remaining finger is not
            // mistaken for a fresh press that can open the context menu.
            pointCount = count
            pointIds = ids
            return
        }
        if (count === 0) {
            holdTimer.stop()
            pointCount = 0
            pointIds = ""
            if (gestureActive) {
                gestureActive = false
                manipulationFinished(false)
            }
            holdTriggered = false
            return
        }

        const first = pressedPoints[0]
        const second = count === 2 ? pressedPoints[1] : first
        const position = Qt.point((first.x + second.x) / 2, (first.y + second.y) / 2)
        const distance = count === 2 ? Math.hypot(first.x - second.x, first.y - second.y) : 0
        if (ids !== pointIds) {
            const firstContact = pointCount === 0
            holdTimer.stop()
            if (gestureActive) {
                gestureActive = false
                manipulationFinished(true)
            }
            pointCount = count
            pointIds = ids
            startPosition = position
            previousPosition = position
            startDistance = distance
            previousDistance = distance
            // A remaining finger after a pinch must not open a context menu.
            holdTriggered = false
            contactStarted()
            if (firstContact && count === 1 && longPressEnabled)
                holdTimer.start()
            return
        }

        if (holdTriggered)
            return
        if (!gestureActive) {
            const travel = Math.hypot(position.x - startPosition.x, position.y - startPosition.y)
            if (travel < dragThreshold && Math.abs(distance - startDistance) < dragThreshold)
                return
            holdTimer.stop()
            gestureActive = true
            manipulationStarted(count, previousPosition)
        }
        // QML point properties expose live value references. Copy coordinates
        // before writing previousPosition, or both signal points become equal.
        const previous = Qt.point(previousPosition.x, previousPosition.y)
        const scale = count === 2 && previousDistance >= 1 && distance >= 1 ? distance / previousDistance : 1
        previousPosition = position
        previousDistance = distance
        manipulationUpdated(count, previous, position, scale)
    }

    onTouchUpdated: function (points) { updateTouches(points) }
    onCanceled: cancelGesture()
    onEnabledChanged: if (!enabled) cancelGesture()
    onVisibleChanged: if (!visible) cancelGesture()
    onGestureStarted: function (gesture) { gesture.grab() }

    Timer {
        id: holdTimer
        interval: Application.styleHints.mousePressAndHoldInterval
        onTriggered: {
            root.holdTriggered = true
            root.longPressed(root.previousPosition)
        }
    }
}
