#include "render/resourcemanager.h"

#include <QColor>
#include <QCoreApplication>
#include <QDirIterator>
#include <QLocale>
#include <QMap>
#include <QString>

#include "base/config.h"
#include "loguru/loguru.hpp"

namespace {
    QMap<QString, QImage*> actor_img_pool;
    QImage* unknown_img{nullptr};

    QImage* addBorder(const QImage& img, int bw) {
        auto* masked = new QImage(img.width() + bw * 2, img.height() + bw * 2, QImage::Format_RGBA8888);
        masked->fill(Qt::transparent);  // fill with transparent color
        auto conv = [bw](int x, int z, QImage* mask, const QImage* origin) {
            int cx = x - bw;
            int cz = z - bw;
            if (cx >= 0 && cx < origin->width() && cz >= 0 && cz < origin->height() && origin->pixelColor(cx, cz).alpha() != 0) {
                mask->setPixelColor(x, z, origin->pixelColor(cx, cz).rgba());
            } else {
                // Add the outline around non-transparent source pixels.
                for (int i = -bw; i <= bw; ++i) {
                    for (int j = -bw; j <= bw; ++j) {
                        if (cx + i >= 0 && cx + i < origin->width() && cz + j >= 0 && cz + j < origin->height() &&
                            origin->pixelColor(cx + i, cz + j).alpha() != 0) {
                            mask->setPixelColor(x, z, QColor(setting::current().actorBorderColor));
                            return;
                        }
                    }
                }
            }
        };
        for (int i = 0; i < masked->width(); i++) {
            for (int j = 0; j < masked->height(); j++) {
                conv(i, j, masked, &img);
            }
        }

        return masked;
    }

}  // namespace

void initResources() {
    QDirIterator it(":/res/icons/entity", QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QImage image(it.next());
        const QString key = it.fileName().replace(".png", "");
        actor_img_pool[key] = addBorder(image, setting::current().actorBorderWidth);
    }
    unknown_img = new QImage(":/res/icons/what.png");
}

QImage* ActorImage(const QString& key) {
    const auto it = actor_img_pool.constFind(key);
    return it == actor_img_pool.cend() ? unknown_img : it.value();
}

std::vector<QImage*> MarkerIcons() {
    std::vector<QImage*> result;
    result.reserve(actor_img_pool.size() + 1);
    for (auto it = actor_img_pool.cbegin(); it != actor_img_pool.cend(); ++it) result.push_back(it.value());
    if (unknown_img) result.push_back(unknown_img);
    return result;
}

void TranslatorMgr::init() {
    // Translations are compiled into the application resource system. Keep the
    // language list explicit so this also works in mobile sandboxes.
    for (const auto& langName : {QStringLiteral("zh_CN"), QStringLiteral("en")}) {
        auto translator = std::make_shared<QTranslator>();
        const auto resourcePath = QStringLiteral(":/i18n/") + langName + QStringLiteral(".qm");
        if (translator->load(resourcePath)) {
            LOG_F(INFO, "Load translation resource %s succeed", resourcePath.toStdString().c_str());
            translations().insert(langName, translator);
        } else {
            LOG_F(WARNING, "Load translation resource %s failed", resourcePath.toStdString().c_str());
        }
    }
}

QString TranslatorMgr::systemLanguage() {
    return QLocale::system().language() == QLocale::Chinese ? QStringLiteral("zh_CN") : QStringLiteral("en");
}

QString TranslatorMgr::resolveLanguage() {
    auto settings = setting::current();
    if (!settings.language.isEmpty() && settings.language != QStringLiteral("system")) return settings.language;
    settings.language = systemLanguage();
    setting::apply(settings);
    return settings.language;
}

void TranslatorMgr::setupTranslation(QCoreApplication& a, const QString& langName) {
    // remove the current translator
    for (auto& t : translations()) {
        a.removeTranslator(t.get());
    }
    // add new
    auto it = translations().find(langName);
    if (it != translations().end()) {
        a.installTranslator(it.value().get());
        LOG_F(INFO, "Install translation for language %s succeed", langName.toStdString().c_str());
    } else {
        LOG_F(WARNING, "No translation found for language %s", langName.toStdString().c_str());
    }
}

QMap<QString, std::shared_ptr<QTranslator>>& TranslatorMgr::translations() {
    static QMap<QString, std::shared_ptr<QTranslator>> translations_;
    return translations_;
}
