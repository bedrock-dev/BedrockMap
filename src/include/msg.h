//
// Created by xhy on 2023/8/6.
//

#ifndef BEDROCKMAP_MSG_H
#define BEDROCKMAP_MSG_H

#include <qchar.h>
#include <qcontainerfwd.h>
#include <qobject.h>

#include <QMessageBox>

bool WARN(const QString &msg);

bool INFO(const QString &msg);

namespace msg {

    // file
    inline QString SAEVE_AS() { return QObject::tr("msg.saveAs"); }

    // level
    inline QString LEVEL_NOT_OPEN() { return QObject::tr("msg.levelNotOpen"); }
    inline QString OPEN_LEVEL_FAILED() { return QObject::tr("msg.openLevelFailed"); }
    inline QString OPEN_LEVEL_SUCC() { return QObject::tr("msg.openLevelSucc"); }
    inline QString LOAD_GLOBAL_DATA_FAILED() { return QObject::tr("msg.loadGlobalDataFailed"); }

    // misc
    inline QString SET_SCALE_LEVEL() { return QObject::tr("msg.setScaleLevel"); }

    // dialog titles
    inline QString WARNING_TITLE() { return QObject::tr("msg.warning"); }
    inline QString INFO_TITLE() { return QObject::tr("msg.info"); }

    // data
    inline QString SAVE_DATA_SUCC() { return QObject::tr("msg.saveDataSucc"); }
    inline QString SAVE_DATA_FAILED() { return QObject::tr("msg.saveDataFailed"); }
    inline QString CANNOT_OPEN_FILE() { return QObject::tr("msg.cannotOpenFile"); }
    inline QString NBT_DATA_CORRUPTED() { return QObject::tr("msg.nbtDataCorrupted"); }
    inline QString INIT_FAILED() { return QObject::tr("msg.initFailed"); }
    inline QString CANNOT_DELETE_ROOT() { return QObject::tr("msg.cannotDeleteRoot"); }
    inline QString NBT_PARSE_FAILED() { return QObject::tr("msg.nbtParseFailed"); }
    inline QString EMPTY_NBT_DATA() { return QObject::tr("msg.emptyNbtData"); }
    inline QString CONFIRM_CLEAR_ALL() { return QObject::tr("msg.confirmClearAll"); }

    // operations
    inline QString NOTHING_TO_SAVE() { return QObject::tr("msg.nothingToSave"); }
    inline QString LEVEL_SAVED() { return QObject::tr("msg.levelSaved"); }
    inline QString IMPORT_FAILED() { return QObject::tr("msg.importFailed"); }
    inline QString INVALID_CHUNK_FORMAT() { return QObject::tr("msg.invalidChunkFormat"); }
    inline QString UNSAVED_CHANGES() { return QObject::tr("msg.unsavedChanges"); }
    inline QString UNSAVED_CHANGES_PROMPT() { return QObject::tr("msg.unsavedChangesPrompt"); }
    inline QString EXPORT_SUCC() { return QObject::tr("msg.exportSucc"); }
    inline QString EXPORT_COMPLETE() { return QObject::tr("msg.exportComplete"); }
    inline QString NO_CHUNK_FOUND() { return QObject::tr("msg.noChunkFound"); }

    // nbt editor
    inline QString CREATE_NODE_FAILED(const QString &err) { return QObject::tr("msg.createNodeFailed") + err; }
    inline QString MODIFY_NODE_FAILED(const QString &err) { return QObject::tr("msg.modifyNodeFailed") + err; }

    // image
    inline QString IMAGE_ASPECT_MISMATCH() { return QObject::tr("msg.imageAspectMismatch"); }

    // welcome
    inline QString PLEASE_WAIT() { return QObject::tr("msg.pleaseWait"); }

    // nbt tags
    inline QString TAG_NAME_EMPTY() { return QObject::tr("msg.tagNameEmpty"); }
    inline QString TAG_TYPE_INVALID() { return QObject::tr("msg.tagTypeInvalid"); }
    inline QString TAG_VALUE_INVALID() { return QObject::tr("msg.tagValueInvalid"); }
    inline QString TAG_UNKNOWN_TYPE() { return QObject::tr("msg.tagUnknownType"); }
    inline QString TAG_VALUE_EMPTY() { return QObject::tr("msg.tagValueEmpty"); }

    // paste / import
    inline QString PASTE_NO_DATA() { return QObject::tr("msg.pasteNoData"); }
    inline QString PASTE_DATA_EMPTY() { return QObject::tr("msg.pasteDataEmpty"); }
    inline QString PASTE_DATA_INVALID() { return QObject::tr("msg.pasteDataInvalid"); }

    // file dialogs
    inline QString SELECT_LEVEL_DIR() { return QObject::tr("msg.selectLevelDir"); }
    inline QString ALL_FILES() { return QObject::tr("msg.allFiles"); }
    inline QString BCHKS_FILES() { return QObject::tr("msg.bchksFiles"); }

    // coordinates
    inline QString INVALID_COORDINATE() { return QObject::tr("msg.invalidCoordinate"); }

}  // namespace msg

#endif  // BEDROCKMAP_MSG_H
