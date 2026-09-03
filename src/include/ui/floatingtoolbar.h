#ifndef BEDROCKMAP_FLOATINGTOOLBAR_H
#define BEDROCKMAP_FLOATINGTOOLBAR_H

#include <qframe.h>
#include <qnamespace.h>
#include <qobjectdefs.h>
#include <qtmetamacros.h>

#include <QHBoxLayout>
#include <QToolButton>
#include <QVector>

class FloatingToolBar : public QFrame {
    Q_OBJECT

   public:
    struct ButtonConfig {
        QString iconPath;
        QString tooltip;
        bool checkable = true;
    };

    struct GroupConfig {
        enum Mode { Exclusive, Toggle };
        Mode mode = Exclusive;
        QVector<ButtonConfig> buttons;
    };

    explicit FloatingToolBar(QWidget *parent = nullptr);

    /// Add a group of buttons. Returns the group index.
    int addGroup(const GroupConfig &group);
    void addSeparator();
    void clear();

    int groupCount() const { return groups_.size(); }
    QToolButton *buttonAt(int groupIndex, int buttonIndex) const;

    void setButtonChecked(int groupIndex, int buttonIndex, bool checked);
    bool isButtonChecked(int groupIndex, int buttonIndex) const;

    void setOrientation(Qt::Orientation orientation);
    Qt::Orientation orientation() const { return orientation_; }

    void setAnchor(Qt::Alignment anchor);
    Qt::Alignment anchor() const { return anchor_; }

    void setAnchorMargins(int margin);
    int anchorMargins() const { return margin_; }

   signals:
    /// Emitted when a button's checked state changes.
    /// For Exclusive groups, only the newly checked button emits with checked=true
    /// (the previously checked one does not emit).
    void buttonToggled(int groupIndex, int buttonIndex, bool checked);

   protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

   private:
    void setupStyleSheet();
    void refreshButtonStyleProperties();
    void reposition();

    struct GroupInfo {
        int startIdx;     // index into buttons_
        int buttonCount;  // buttons in this group
        GroupConfig::Mode mode;
    };

    QVector<GroupInfo> groups_;
    QVector<QToolButton *> buttons_;
    QVector<QWidget *> separators_;
    QBoxLayout *layout_;
    Qt::Orientation orientation_{Qt::Vertical};
    Qt::Alignment anchor_{Qt::AlignLeft | Qt::AlignVCenter};
    int margin_{8};
};

#endif  // BEDROCKMAP_FLOATINGTOOLBAR_H
