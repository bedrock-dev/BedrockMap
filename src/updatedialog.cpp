#include "updatedialog.h"

#include <QDesktopServices>
#include <QTextCursor>
#include <QUrl>

#include "config.h"
#include "ui_updatedialog.h"

namespace {
    // Release notes are bilingual, separated by a "---" line: Chinese first,
    // then English. Show only the section matching the current UI language.
    QString selectReleaseNotes(const QString &notes) {
        const QStringList lines = notes.split('\n');
        int separator = -1;
        for (int i = 0; i < lines.size(); ++i) {
            if (lines[i].trimmed() == "---") {
                separator = i;
                break;
            }
        }
        if (separator < 0) return notes;  // not bilingual, show everything

        const bool isChinese = setting::current().LANGUAGE.startsWith("zh", Qt::CaseInsensitive);
        return (isChinese ? lines.mid(0, separator) : lines.mid(separator + 1)).join('\n').trimmed();
    }
}  // namespace

UpdateDialog::UpdateDialog(const QString &newVersion, const QString &releaseNotes, const QString &htmlUrl, QWidget *parent)
    : QDialog(parent), ui(new Ui::UpdateDialog), html_url_(htmlUrl) {
    ui->setupUi(this);
    setWindowFlag(Qt::MSWindowsFixedSizeDialogHint);
    setWindowTitle(tr("updateDialog.title") + " - " + QString::fromStdString(constant::SOFTWARE_NAME));

    const QString current = constant::SOFTWARE_VERSION.toString();
    const QString text = tr("updateDialog.newVersion").arg(newVersion, current);
    ui->version_label->setText(QString("<h3>%1</h3>").arg(text.toHtmlEscaped()));

    ui->notes_browser->setMarkdown(selectReleaseNotes(releaseNotes));
    ui->notes_browser->moveCursor(QTextCursor::Start);

    // "Don't check for updates" — persist the pending value without changing runtime state.
    ui->noUpdateCheckBox->setChecked(!setting::current().CHECK_UPDATE);
    connect(ui->noUpdateCheckBox, &QCheckBox::toggled, this, [](bool checked) {
        auto values = setting::current();
        values.CHECK_UPDATE = !checked;
        setting::save(values);
    });

    connect(ui->downloadBtn, &QPushButton::clicked, this, [this] {
        if (!html_url_.isEmpty()) QDesktopServices::openUrl(QUrl(html_url_));
        accept();
    });
    connect(ui->closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

UpdateDialog::~UpdateDialog() { delete ui; }
