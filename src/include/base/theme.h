#ifndef BEDROCKMAP_THEME_H
#define BEDROCKMAP_THEME_H

#include <QStyleHints>
#include <QString>

/// Apply the saved UI theme mode to Qt's platform palette hints. "system" (and
/// any unknown value) resets the hint so the platform chooses the effective
/// light/dark scheme.
inline void applyThemeHints(QStyleHints* hints, const QString& colorTheme) {
    if (!hints) return;
    if (colorTheme == QStringLiteral("dark")) {
        hints->setColorScheme(Qt::ColorScheme::Dark);
    } else if (colorTheme == QStringLiteral("light")) {
        hints->setColorScheme(Qt::ColorScheme::Light);
    } else {
        hints->setColorScheme(Qt::ColorScheme::Unknown);
    }
}

#endif  // BEDROCKMAP_THEME_H
