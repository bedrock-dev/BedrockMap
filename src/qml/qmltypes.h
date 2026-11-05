#ifndef BEDROCKMAP_QMLTYPES_H
#define BEDROCKMAP_QMLTYPES_H

#include <QtQml/qqmlregistration.h>

#include "controller/appcontroller.h"
#include "controller/blockvolumecontroller.h"
#include "controller/chunkeditorcontroller.h"
#include "controller/globaldatacontroller.h"
#include "controller/mapcontroller.h"
#include "controller/mapitemcontroller.h"
#include "controller/nbtdocumentcontroller.h"
#include "controller/regionimportcontroller.h"
#include "controller/runtimesettings.h"
#include "controller/sessioncontroller.h"
#include "controller/settingsviewmodel.h"
#include "controller/voxelscenecontroller.h"
#include "model/logmodel.h"
#include "model/nbttreemodel.h"
#include "model/notifications.h"
#include "model/worldlistmodel.h"
#include "render/terraindataprovider.h"

class QQmlEngine;
class QJSEngine;

// Keep QML exports in the GUI target; the core has no Qt Qml dependency.
struct NbtDocumentControllerQml {
    Q_GADGET
    QML_FOREIGN(NbtDocumentController)
    QML_NAMED_ELEMENT(NbtDocumentController)
};

struct NbtTreeModelQml {
    Q_GADGET
    QML_FOREIGN(NbtTreeModel)
    QML_NAMED_ELEMENT(NbtTreeModel)
};

struct RegionImportControllerQml {
    Q_GADGET
    QML_FOREIGN(RegionImportController)
    QML_NAMED_ELEMENT(RegionImportController)
    QML_UNCREATABLE("Owned by the map controller")
};

struct VoxelSceneControllerQml {
    Q_GADGET
    QML_FOREIGN(VoxelSceneController)
    QML_NAMED_ELEMENT(VoxelSceneController)
    QML_UNCREATABLE("Provided by a preview session")
};

struct BlockVolumeControllerQml {
    Q_GADGET
    QML_FOREIGN(BlockVolumeController)
    QML_NAMED_ELEMENT(BlockVolumeController)
    QML_UNCREATABLE("Provided by McstructureSession")
};

struct MapItemControllerQml {
    Q_GADGET
    QML_FOREIGN(MapItemController)
    QML_NAMED_ELEMENT(MapItemController)
    QML_UNCREATABLE("Provided by GlobalNbtSection")
};

struct ChunkEditorControllerQml {
    Q_GADGET
    QML_FOREIGN(ChunkEditorController)
    QML_NAMED_ELEMENT(ChunkEditorController)
    QML_UNCREATABLE("Provided by WorldSession")
};

struct GlobalDataControllerQml {
    Q_GADGET
    QML_FOREIGN(GlobalDataController)
    QML_NAMED_ELEMENT(GlobalDataController)
    QML_UNCREATABLE("Provided by WorldSession")
};

struct GlobalNbtSectionQml {
    Q_GADGET
    QML_FOREIGN(GlobalNbtSection)
    QML_NAMED_ELEMENT(GlobalNbtSection)
    QML_UNCREATABLE("Created by GlobalDataController")
};

struct MapControllerQml {
    Q_GADGET
    QML_FOREIGN(MapController)
    QML_ANONYMOUS
};

struct TerrainDataProviderQml {
    Q_GADGET
    QML_FOREIGN(TerrainDataProvider)
    QML_ANONYMOUS
};

struct WorkspaceSessionModelQml {
    Q_GADGET
    QML_FOREIGN(WorkspaceSessionModel)
    QML_ANONYMOUS
};

struct AppQml {
    Q_GADGET
    QML_FOREIGN(AppController)
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

   public:
    static AppController* create(QQmlEngine* engine, QJSEngine*);
};

struct SettingsQml {
    Q_GADGET
    QML_FOREIGN(SettingsViewModel)
    QML_ANONYMOUS
};

struct RuntimeSettingsQml {
    Q_GADGET
    QML_FOREIGN(RuntimeSettings)
    QML_UNCREATABLE("Provided by AppController")
};

struct WorldsQml {
    Q_GADGET
    QML_FOREIGN(WorldListModel)
    QML_ANONYMOUS
};

struct NotificationsQml {
    Q_GADGET
    QML_FOREIGN(NotificationCenter)
    QML_ANONYMOUS
};

struct LogsQml {
    Q_GADGET
    QML_FOREIGN(LogModel)
    QML_ANONYMOUS
};

#endif  // BEDROCKMAP_QMLTYPES_H
