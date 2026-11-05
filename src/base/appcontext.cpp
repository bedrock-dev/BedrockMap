#include "base/appcontext.h"

#include "base/binaryclipboard.h"
#include "base/resourcemanager.h"
#include "base/translator.h"

AppContext::AppContext()
    : resources_(std::make_unique<ResourceManager>()),
      binary_clipboard_(BinaryClipboard::create()),
      translator_(std::make_unique<TranslatorManager>()) {}

AppContext::~AppContext() = default;

ResourceManager& AppContext::resources() { return *resources_; }

BinaryClipboard& AppContext::binaryClipboard() { return *binary_clipboard_; }

TranslatorManager& AppContext::translator() { return *translator_; }
