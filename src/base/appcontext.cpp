#include "base/appcontext.h"

#include <QCoreApplication>

#include "base/binaryclipboard.h"
#include "base/config.h"
#include "base/resourcemanager.h"
#include "base/translator.h"

AppContext::AppContext()
    : resources_(std::make_unique<ResourceManager>()),
      fonts_(std::make_unique<FontManager>()),
      binary_clipboard_(BinaryClipboard::create()),
      translator_(std::make_unique<TranslatorManager>()) {}

AppContext::~AppContext() = default;

void AppContext::initialize(QCoreApplication& application) {
    resources_->initialize();
    fonts_->initialize(setting::current());
    translator_->initialize(application);
}

ResourceManager& AppContext::resources() { return *resources_; }

FontManager& AppContext::fonts() { return *fonts_; }

BinaryClipboard& AppContext::binaryClipboard() { return *binary_clipboard_; }

TranslatorManager& AppContext::translator() { return *translator_; }
