#ifndef BEDROCKMAP_APPCONTEXT_H
#define BEDROCKMAP_APPCONTEXT_H

#include <memory>

class BinaryClipboard;
class FontManager;
class QCoreApplication;
class ResourceManager;
class TranslatorManager;

/// Owns application-wide services shared by every session.
class AppContext final {
   public:
    AppContext();
    ~AppContext();

    AppContext(const AppContext&) = delete;
    AppContext& operator=(const AppContext&) = delete;

    /// Initialize process-wide services after settings have been loaded.
    void initialize(QCoreApplication& application);

    [[nodiscard]] ResourceManager& resources();
    [[nodiscard]] FontManager& fonts();
    [[nodiscard]] BinaryClipboard& binaryClipboard();
    [[nodiscard]] TranslatorManager& translator();

   private:
    std::unique_ptr<ResourceManager> resources_;
    std::unique_ptr<FontManager> fonts_;
    std::unique_ptr<BinaryClipboard> binary_clipboard_;
    std::unique_ptr<TranslatorManager> translator_;
};

#endif  // BEDROCKMAP_APPCONTEXT_H
