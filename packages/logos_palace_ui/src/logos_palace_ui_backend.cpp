#include "logos_palace_ui_backend.h"

#include "logos_sdk.h"

void LogosPalaceUiBackend::onContextReady()
{
    setRoomTitle(modules().palace_core.roomTitle());
    setSyncHealth(modules().palace_core.syncHealth());
}

QString LogosPalaceUiBackend::enterRoom(const QString& roomId)
{
    if (!isContextReady())
        return QStringLiteral("rejected=core-not-ready");
    const QString result = modules().palace_core.enterRoom(roomId);
    if (result.startsWith(QStringLiteral("ok;"))) {
        setRoomTitle(modules().palace_core.roomTitle());
        setSyncHealth(modules().palace_core.syncHealth());
    }
    return result;
}

QString LogosPalaceUiBackend::useSpot(const QString& spotId)
{
    if (!isContextReady())
        return QStringLiteral("rejected=core-not-ready");
    const QString result = modules().palace_core.useSpot(spotId);
    if (result.startsWith(QStringLiteral("ok;"))) {
        setRoomTitle(modules().palace_core.roomTitle());
        setSyncHealth(modules().palace_core.syncHealth());
    }
    return result;
}
