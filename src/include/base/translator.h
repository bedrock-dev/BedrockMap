#ifndef BEDROCKMAP_TRANSLATOR_H
#define BEDROCKMAP_TRANSLATOR_H

#include <QMap>
#include <QString>
#include <QTranslator>
#include <memory>

class QCoreApplication;

/// Loads and installs the application's compiled translation catalogs.
class TranslatorManager final {
   public:
    TranslatorManager() = default;
    TranslatorManager(const TranslatorManager&) = delete;
    TranslatorManager& operator=(const TranslatorManager&) = delete;

    void initialize();
    void setupTranslation(QCoreApplication& application, const QString& language);

    /// Resolve the configured UI language. An empty (or "system") setting falls
    /// back to the system locale and publishes the detected value through
    /// setting::apply().
    [[nodiscard]] QString resolveLanguage() const;

    /// System locale fallback without touching the stored settings.
    [[nodiscard]] QString systemLanguage() const;

   private:
    QMap<QString, std::shared_ptr<QTranslator>> translations_;
};

#endif  // BEDROCKMAP_TRANSLATOR_H
