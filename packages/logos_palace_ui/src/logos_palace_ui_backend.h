#pragma once

#include "logos_ui_plugin_context.h"
#include "rep_logos_palace_ui_source.h"

class LogosPalaceUiBackend : public LogosPalaceUiSimpleSource,
                             public LogosUiPluginContext
{
public:
    QString enterRoom(const QString& roomId) override;
    QString useSpot(const QString& spotId) override;

protected:
    void onContextReady() override;
};
