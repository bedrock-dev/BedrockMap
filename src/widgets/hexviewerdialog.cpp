#include "hexviewerdialog.h"

#include <QHexView/model/buffer/qmemorybuffer.h>
#include <QHexView/model/qhexdocument.h>
#include <QHexView/qhexview.h>

#include <QDialogButtonBox>
#include <QFont>
#include <QPushButton>
#include <QVBoxLayout>

HexViewerDialog::HexViewerDialog(QWidget* parent) : QDialog(parent) {
    resize(720, 480);

    auto* layout = new QVBoxLayout(this);
    hex_view_ = new QHexView(this);
    // match the NbtWidget look: mono first, CJK fallback
    QFont font;
    font.setFamilies({"JetBrains Mono", "Microsoft YaHei", "Microsoft YaHei UI"});
    hex_view_->setFont(font);
    layout->addWidget(hex_view_);

    // empty document until setData() is called
    hex_view_->setDocument(QHexDocument::fromMemory<QMemoryBuffer>(QByteArray(), this));

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    hex_view_->options();
    layout->addWidget(buttonBox);
}

void HexViewerDialog::setData(const QByteArray& data) {
    auto* doc = QHexDocument::fromMemory<QMemoryBuffer>(data, this);
    hex_view_->setDocument(doc);
}

QByteArray HexViewerDialog::getData() const {
    auto* doc = hex_view_->hexDocument();
    if (!doc) return {};
    return doc->read(0, static_cast<int>(doc->length()));
}

void HexViewerDialog::setReadOnly(bool readOnly) { hex_view_->setReadOnly(readOnly); }
