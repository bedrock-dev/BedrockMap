//
// Created by xhy on 2023/8/6.
//

#ifndef BEDROCKMAP_MSG_H
#define BEDROCKMAP_MSG_H

#include <QMessageBox>

void WARN(const QString &msg);

void INFO(const QString &msg);

bool CHECK_CONDITION(bool c, const QString &msg);

void CHECK_RESULT(bool c, const QString &succ, const QString &fail);

void CHECK_DATA_SAVE(bool c);

#endif  // BEDROCKMAP_MSG_H
