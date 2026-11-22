pragma Singleton

import BedrockMap 1.0
import QtQuick

QtObject {
    function report(title, source, fallback) {
        let detail = ""
        if (source && source.lastError !== undefined)
            detail = String(source.lastError)
        else if (source && source.errorMessage !== undefined)
            detail = String(source.errorMessage)
        else if (source && source.error !== undefined)
            detail = String(source.error)
        const text = detail.length > 0 ? detail : String(fallback || "")
        App.notifications.error(title, "", text)
    }
}
