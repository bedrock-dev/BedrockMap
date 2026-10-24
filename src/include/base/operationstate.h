#ifndef BEDROCKMAP_OPERATIONSTATE_H
#define BEDROCKMAP_OPERATIONSTATE_H

#include <QObject>

/// Common lifecycle state for asynchronous application operations.
enum class OperationState { Idle, Running, Succeeded, Failed, Cancelled };
Q_DECLARE_METATYPE(OperationState)

#endif
