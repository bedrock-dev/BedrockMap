#ifndef BEDROCKMAP_PLEASEWAITDIALOG_H
#define BEDROCKMAP_PLEASEWAITDIALOG_H

#include <QDialog>

class QLabel;

/// Process-wide non-modal "Please wait" window shown while a background task runs.
class PleaseWaitDialog : public QDialog {
   public:
    static PleaseWaitDialog& instance();

    void showBusy();
    void hideBusy();

   private:
    PleaseWaitDialog();
    QLabel* label_{nullptr};
};

/// Shows the shared wait dialog for the lifetime of a synchronous operation.
class PleaseWaitScope {
   public:
    PleaseWaitScope();
    ~PleaseWaitScope();

    PleaseWaitScope(const PleaseWaitScope&) = delete;
    PleaseWaitScope& operator=(const PleaseWaitScope&) = delete;
};

#endif  // BEDROCKMAP_PLEASEWAITDIALOG_H
