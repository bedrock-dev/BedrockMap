//
// Created by xhy on 2023/8/6.
//
#include "msg.h"

#include <qchar.h>

#include <QObject>
#include <QString>

bool WARN(const QString &msg) {
    QMessageBox::warning(nullptr, msg::WARNING_TITLE(), msg, QMessageBox::Yes, QMessageBox::Yes);
    return false;
}

bool INFO(const QString &msg) {
    QMessageBox::information(nullptr, msg::INFO_TITLE(), msg, QMessageBox::Yes, QMessageBox::Yes);
    return true;
}

bool CHECK_CONDITION(bool c, const QString &msg) {
    if (!c) WARN(msg);
    return c;
}

void CHECK_RESULT(bool c, const QString &succ, const QString &fail) {
    if (c) {
        INFO(succ);
    } else {
        WARN(fail);
    }
}

void CHECK_DATA_SAVE(bool c) { CHECK_RESULT(c, QObject::tr("msg.saveDataSucc"), QObject::tr("msg.saveDataFailed")); }
