#ifndef TABPAGEWIDGET_H
#define TABPAGEWIDGET_H

#include <QWidget>

// Base class for pages hosted as tabs in LevelTabWidget.
// It centralizes the dirty / commit contract so the tab widget can prompt for
// unsaved changes uniformly. New page types (e.g. future file formats) only
// need to inherit from this and implement isDirty() / commit().
class TabPageWidget : public QWidget {
    Q_OBJECT

   public:
    explicit TabPageWidget(QWidget *parent = nullptr);
    ~TabPageWidget() override;

    /// Whether the page holds unsaved in-memory changes.
    virtual bool isDirty() const { return false; }

    /// Persist unsaved changes. Returns false if the page could not be saved.
    virtual bool commit() { return true; }
};

#endif  // TABPAGEWIDGET_H
