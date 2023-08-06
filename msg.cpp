//
// Created by xhy on 2023/8/6.
//
#include "msg.h"

void WARN(const QString &msg) { QMessageBox::warning(nullptr, "警告", msg, QMessageBox::Yes, QMessageBox::Yes); }

void INFO(const QString &msg) { QMessageBox::warning(nullptr, "信息", msg, QMessageBox::Yes, QMessageBox::Yes); }


