#include "floatingtoolbar.h"

#include <qevent.h>
#include <qframe.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qpalette.h>
#include <qpoint.h>
#include <qrect.h>
#include <qsize.h>
#include <qwidget.h>

#include <QApplication>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>

namespace {
    constexpr int BTN_SIZE = 28;
    constexpr int ICON_SIZE = 22;
    constexpr int SEPARATOR_HEIGHT = 6;
    constexpr int CORNER_RADIUS = 4;
    constexpr int BG_ALPHA = 255;
}  // namespace

FloatingToolBar::FloatingToolBar(QWidget *parent) : QFrame(parent) {
    setAttribute(Qt::WA_TranslucentBackground, false);
    setCursor(Qt::ArrowCursor);

    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(2, 4, 2, 4);
    layout_->setSpacing(1);

    setupStyleSheet();

    if (parent) {
        parent->installEventFilter(this);
    }
}

void FloatingToolBar::setupStyleSheet() {
    auto bgColor = QApplication::palette().window().color();
    bgColor.setAlpha(BG_ALPHA);

    setStyleSheet(QString(R"(
        FloatingToolBar {
            background-color: rgba(%1, %2, %3, %4);
            border-radius: %5px;
        }
        FloatingToolBar QToolButton {
            border: none;
            border-radius: 4px;
            icon-size: %6px %6px;
        }
        FloatingToolBar QToolButton:hover {
            background-color: rgba(160, 160, 160, 100);
        }
        FloatingToolBar QToolButton:checked {
            background-color: rgba(160, 160, 160, 180);
        }
    )")
                      .arg(bgColor.red())
                      .arg(bgColor.green())
                      .arg(bgColor.blue())
                      .arg(BG_ALPHA)
                      .arg(CORNER_RADIUS)
                      .arg(ICON_SIZE));
}

void FloatingToolBar::setOrientation(Qt::Orientation orientation) {
    if (orientation == orientation_) return;
    orientation_ = orientation;

    // Recreate layout
    auto *oldLayout = layout_;
    if (orientation == Qt::Horizontal) {
        layout_ = new QHBoxLayout();
        layout_->setContentsMargins(4, 2, 4, 2);
        layout_->setSpacing(1);
    } else {
        layout_ = new QVBoxLayout();
        layout_->setContentsMargins(2, 4, 2, 4);
        layout_->setSpacing(1);
    }

    // Move all existing widgets to new layout
    while (oldLayout->count() > 0) {
        auto *item = oldLayout->takeAt(0);
        if (item->widget()) {
            layout_->addWidget(item->widget());
        }
        delete item;
    }
    delete oldLayout;
    setLayout(layout_);

    // Update separator styling
    setupStyleSheet();
    for (auto *sep : separators_) {
        if (orientation == Qt::Horizontal) {
            sep->setFixedHeight(BTN_SIZE);
            sep->setFixedWidth(SEPARATOR_HEIGHT);
            sep->setStyleSheet(QString(R"(
                background-color: rgba(128, 128, 128, 80);
                border-radius: 1px;
                max-width: 1px;
                margin: 4px %1px;
            )")
                                   .arg((SEPARATOR_HEIGHT - 1) / 2));
        } else {
            sep->setFixedWidth(BTN_SIZE);
            sep->setFixedHeight(SEPARATOR_HEIGHT);
            sep->setStyleSheet(QString(R"(
                background-color: rgba(128, 128, 128, 80);
                border-radius: 1px;
                max-height: 1px;
                margin: %1px 4px;
            )")
                                   .arg((SEPARATOR_HEIGHT - 1) / 2));
        }
    }
}

int FloatingToolBar::addGroup(const GroupConfig &group) {
    int groupIdx = groups_.size();
    int startIdx = buttons_.size();

    for (int i = 0; i < group.buttons.size(); i++) {
        const auto &cfg = group.buttons[i];
        auto *btn = new QToolButton(this);
        btn->setIcon(QIcon(cfg.iconPath));
        btn->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
        btn->setFixedSize(BTN_SIZE, BTN_SIZE);
        btn->setToolTip(cfg.tooltip);
        btn->setCheckable(cfg.checkable);
        btn->setAutoRaise(true);
        btn->setAutoExclusive(false);  // we handle exclusivity manually

        int flatIdx = buttons_.size();
        buttons_.append(btn);
        layout_->addWidget(btn);

        if (!cfg.checkable) {
            // Non-checkable: just emit on click
            connect(btn, &QToolButton::clicked, this, [this, groupIdx, i]() { emit buttonToggled(groupIdx, i, true); });
        } else if (group.mode == GroupConfig::Exclusive) {
            // Exclusive: Qt's checkable auto-toggles before clicked() fires,
            // so we manually correct the state in the clicked handler.
            connect(btn, &QToolButton::clicked, this, [this, groupIdx, flatIdx, i]() {
                auto *clickedBtn = buttons_[flatIdx];
                if (!clickedBtn->isCheckable()) return;

                // Uncheck all other buttons (Qt may have already toggled this one)
                const auto &gi = groups_[groupIdx];
                for (int j = 0; j < gi.buttonCount; j++) {
                    auto *other = buttons_[gi.startIdx + j];
                    if (other != clickedBtn) {
                        other->setChecked(false);
                    }
                }

                // Ensure this one is always checked (can't uncheck in exclusive)
                clickedBtn->setChecked(true);
                emit buttonToggled(groupIdx, i, true);
            });

            // First button checked by default
            if (i == 0) {
                btn->setChecked(true);
            }
        } else {
            // Toggle mode: independent on/off
            connect(btn, &QToolButton::toggled, this, [this, groupIdx, i](bool checked) { emit buttonToggled(groupIdx, i, checked); });
        }
    }

    groups_.append({startIdx, (int)group.buttons.size(), group.mode});

    // Force layout computation even if hidden, so adjustSize() works
    layout_->activate();
    adjustSize();

    return groupIdx;
}

void FloatingToolBar::addSeparator() {
    auto *sep = new QWidget(this);
    if (orientation_ == Qt::Horizontal) {
        sep->setFixedHeight(BTN_SIZE);
        sep->setFixedWidth(SEPARATOR_HEIGHT);
        sep->setStyleSheet(QString(R"(
            background-color: rgba(128, 128, 128, 80);
            border-radius: 1px;
            max-width: 1px;
            margin: 4px %1px;
        )")
                               .arg((SEPARATOR_HEIGHT - 1) / 2));
    } else {
        sep->setFixedWidth(BTN_SIZE);
        sep->setFixedHeight(SEPARATOR_HEIGHT);
        sep->setStyleSheet(QString(R"(
            background-color: rgba(128, 128, 128, 80);
            border-radius: 1px;
            max-height: 1px;
            margin: %1px 4px;
        )")
                               .arg((SEPARATOR_HEIGHT - 1) / 2));
    }
    layout_->addWidget(sep);
    separators_.append(sep);
}

void FloatingToolBar::clear() {
    for (auto *btn : buttons_) {
        layout_->removeWidget(btn);
        delete btn;
    }
    for (auto *sep : separators_) {
        layout_->removeWidget(sep);
        delete sep;
    }
    buttons_.clear();
    separators_.clear();
    groups_.clear();
}

QToolButton *FloatingToolBar::buttonAt(int groupIndex, int buttonIndex) const {
    if (groupIndex < 0 || groupIndex >= groups_.size()) return nullptr;
    const auto &g = groups_[groupIndex];
    int idx = g.startIdx + buttonIndex;
    if (buttonIndex < 0 || buttonIndex >= g.buttonCount || idx >= buttons_.size()) return nullptr;
    return buttons_[idx];
}

void FloatingToolBar::setButtonChecked(int groupIndex, int buttonIndex, bool checked) {
    auto *btn = buttonAt(groupIndex, buttonIndex);
    if (!btn) return;

    auto &gi = groups_[groupIndex];

    if (gi.mode == GroupConfig::Exclusive && checked) {
        // Uncheck others in group
        for (int j = 0; j < gi.buttonCount; j++) {
            auto *other = buttons_[gi.startIdx + j];
            if (other != btn) {
                other->setChecked(false);
            }
        }
    }
    btn->setChecked(checked);
}

bool FloatingToolBar::isButtonChecked(int groupIndex, int buttonIndex) const {
    auto *btn = buttonAt(groupIndex, buttonIndex);
    return btn && btn->isChecked();
}

void FloatingToolBar::setAnchor(Qt::Alignment anchor) {
    anchor_ = anchor;
    reposition();
}

void FloatingToolBar::setAnchorMargins(int margin) {
    margin_ = margin;
    reposition();
}

void FloatingToolBar::resizeEvent(QResizeEvent *event) {
    QFrame::resizeEvent(event);
    reposition();
}

void FloatingToolBar::showEvent(QShowEvent *event) {
    QFrame::showEvent(event);
    reposition();
}

bool FloatingToolBar::eventFilter(QObject *obj, QEvent *event) {
    if (obj == parent() && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        reposition();
    }
    return QFrame::eventFilter(obj, event);
}

void FloatingToolBar::reposition() {
    auto *p = parentWidget();
    if (!p) return;

    adjustSize();
    int x = 0, y = 0;
    int pw = p->width();
    int ph = p->height();
    int tw = width();
    int th = height();

    if (anchor_ & Qt::AlignLeft) {
        x = margin_;
    } else if (anchor_ & Qt::AlignRight) {
        x = pw - tw - margin_;
    } else if (anchor_ & Qt::AlignHCenter) {
        x = (pw - tw) / 2;
    }

    if (anchor_ & Qt::AlignTop) {
        y = margin_;
    } else if (anchor_ & Qt::AlignBottom) {
        y = ph - th - margin_;
    } else if (anchor_ & Qt::AlignVCenter) {
        y = (ph - th) / 2;
    }

    move(x, y);
}
