#include "welcometab.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {
    constexpr int CONTENT_INDENT = 4;

    QLabel *makeLink(const QString &text, QWidget *parent) {
        auto *label = new QLabel(text, parent);
        label->setTextFormat(Qt::RichText);
        label->setCursor(Qt::PointingHandCursor);
        return label;
    }

    QLabel *makeSectionTitle(const QString &text, QWidget *parent) {
        auto *label = new QLabel(text, parent);
        auto font = label->font();
        font.setBold(true);
        font.setPointSize(font.pointSize() + 2);
        label->setFont(font);
        return label;
    }

    QVBoxLayout *makeContentLayout(QWidget *parent) {
        auto *layout = new QVBoxLayout();
        layout->setContentsMargins(CONTENT_INDENT, 0, 0, 0);
        layout->setSpacing(2);
        return layout;
    }

    QLabel *makeRecentItem(const QString &path, QWidget *parent) {
        auto *label = new QLabel(QString("<a href=\"%1\">%2</a>").arg(path, path), parent);
        label->setTextFormat(Qt::RichText);
        label->setCursor(Qt::PointingHandCursor);
        return label;
    }
}  // namespace

WelcomeTab::WelcomeTab(QWidget *parent) : QWidget(parent) { setupUI(); }

void WelcomeTab::setupUI() {
    auto *root = new QVBoxLayout(this);
    root->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    root->setContentsMargins(40, 40, 40, 40);

    // --- title ---
    auto *title = new QLabel(tr("msg.welcome.text"), this);
    title->setTextFormat(Qt::RichText);
    title->setAlignment(Qt::AlignLeft);
    auto titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 4);
    title->setFont(titleFont);
    root->addWidget(title);
    root->addSpacing(10);

    // --- basic actions ---
    root->addSpacing(10);
    root->addWidget(makeSectionTitle(tr("msg.welcome.start"), this));

    auto *startContent = makeContentLayout(this);
    auto *newLink = makeLink(QString("<a href=\"new\">%1</a>").arg(tr("msg.welcome.newLevel")), this);
    connect(newLink, &QLabel::linkActivated, this, [this](const QString &) { emit newLevelRequested(); });
    startContent->addWidget(newLink);

    auto *openLink = makeLink(QString("<a href=\"open\">%1</a>").arg(tr("msg.welcome.openLevel")), this);
    connect(openLink, &QLabel::linkActivated, this, [this](const QString &) { emit openLevelRequested(); });
    startContent->addWidget(openLink);

    auto *nbtLink = makeLink(QString("<a href=\"nbt\">%1</a>").arg(tr("msg.welcome.nbtEditor")), this);
    connect(nbtLink, &QLabel::linkActivated, this, [this](const QString &) { emit openNbtEditorRequested(); });
    startContent->addWidget(nbtLink);
    root->addLayout(startContent);

    // --- recent levels ---
    root->addSpacing(10);
    root->addWidget(makeSectionTitle(tr("msg.welcome.recentLevels"), this));
    recent_layout_ = makeContentLayout(this);
    root->addLayout(recent_layout_);

    // --- links ---
    root->addSpacing(10);
    root->addWidget(makeSectionTitle(tr("msg.welcome.links"), this));

    auto *linksContent = makeContentLayout(this);
    auto *tutorialLink = makeLink(QString("<a href=\"tutorial\">%1</a>").arg(tr("msg.welcome.tutorial")), this);
    linksContent->addWidget(tutorialLink);

    auto *githubLink = makeLink(QString("<a href=\"github\">%1</a>").arg(tr("msg.welcome.github")), this);
    linksContent->addWidget(githubLink);
    root->addLayout(linksContent);

    setLayout(root);
}

void WelcomeTab::setRecentPaths(const QStringList &paths) {
    recent_paths_ = paths;
    rebuildRecentList();
}

void WelcomeTab::rebuildRecentList() {
    // clear existing items
    while (recent_layout_->count() > 0) {
        auto *item = recent_layout_->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (recent_paths_.isEmpty()) {
        auto *empty = new QLabel(tr("msg.welcome.noRecent"), this);
        empty->setStyleSheet("QLabel { color: gray; font-size: 12px; }");
        recent_layout_->addWidget(empty);
        return;
    }

    for (const auto &path : recent_paths_) {
        auto *link = makeRecentItem(path, this);
        connect(link, &QLabel::linkActivated, this, [this, path](const QString &) { emit openRecentLevel(path); });
        recent_layout_->addWidget(link);
    }
}
