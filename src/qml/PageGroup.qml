import QtQuick

/// Child-page stack for one top-level page. The bottom entry is rootRoute; the
/// shell shows a back button while a child route is on top. Definitions may
/// include a title and a component:
/// { route: "logs", title: qsTr("Logs"), component: logsComponent }
QtObject {
    id: root

    property var definitions: []
    property string rootRoute: ""
    property string titleOverride: ""
    property var history: []

    readonly property string currentRoute: history.length > 0 ? history[history.length - 1] : ""
    readonly property var currentDefinition: routeDefinition(currentRoute)
    readonly property Component currentComponent: currentDefinition && currentDefinition.component
                                                  ? currentDefinition.component : null
    readonly property string currentTitle: titleOverride.length > 0 ? titleOverride : currentDefinition
                                                                       && currentDefinition.title
                                                                       ? currentDefinition.title : ""
    readonly property bool isRoot: currentRoute.length > 0 && currentRoute === rootRoute
    readonly property bool isChildOpen: currentRoute.length > 0 && currentRoute !== rootRoute
    readonly property bool canGoBack: history.length > 1

    Component.onCompleted: {
        if (history.length === 0 && rootRoute.length > 0)
            reset(rootRoute)
    }

    function routeDefinition(route) {
        for (let i = 0; i < definitions.length; ++i) {
            if (definitions[i].route === route)
                return definitions[i]
        }
        return null
    }

    function known(route) {
        if (routeDefinition(route))
            return true
        console.warn("Unknown page route:", route)
        return false
    }

    function reset(route) {
        if (!known(route))
            return false
        history = [route]
        return true
    }

    function push(route) {
        if (!known(route) || route === currentRoute)
            return false
        history = history.concat([route])
        return true
    }

    function replace(route) {
        if (!known(route))
            return false
        history = history.slice(0, -1).concat([route])
        return true
    }

    /// Shows a child route, replacing a sibling child instead of stacking on it.
    function open(route) {
        return isChildOpen ? replace(route) : push(route)
    }

    function pop() {
        if (!canGoBack)
            return false
        history = history.slice(0, -1)
        return true
    }
}
