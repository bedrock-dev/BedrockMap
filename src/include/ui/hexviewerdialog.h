#ifndef BEDROCKMAP_HEXVIEWERDIALOG_H
#define BEDROCKMAP_HEXVIEWERDIALOG_H

#include <QByteArray>
#include <QDialog>


class QHexView;

// Reusable hex viewer/editor dialog. Use setReadOnly() to switch between
// pure viewing and editing; getData() returns the (possibly edited) bytes.
class HexViewerDialog : public QDialog {
    Q_OBJECT

   public:
    explicit HexViewerDialog(QWidget *parent = nullptr);

    void setData(const QByteArray &data);
    [[nodiscard]] QByteArray getData() const;

    void setReadOnly(bool readOnly);

   private:
    QHexView *hex_view_{nullptr};
};

#endif  // BEDROCKMAP_HEXVIEWERDIALOG_H
