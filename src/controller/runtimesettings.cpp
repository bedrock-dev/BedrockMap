#include "controller/runtimesettings.h"

#include "base/config.h"

RuntimeSettings::RuntimeSettings(QObject* parent) : QObject(parent), values_(setting::current()) {}
