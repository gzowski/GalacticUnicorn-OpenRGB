#pragma once

#include <QObject>
#include <QtPlugin>

#include "OpenRGBPluginInterface.h"

class GUWidget;
class RGBControllerInterface;

class GalacticUnicornPlugin : public QObject, public OpenRGBPluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpenRGBPluginInterface_IID FILE "plugin.json")
    Q_INTERFACES(OpenRGBPluginInterface)

public:
    ~GalacticUnicornPlugin() override;

    OpenRGBPluginInfo GetPluginInfo() override;
    unsigned int      GetPluginAPIVersion() override;

    void     Load(OpenRGBPluginAPIInterface* plugin_api_ptr) override;
    QWidget* GetWidget() override;
    QMenu*   GetTrayMenu() override;
    void     Unload() override;
    void     OnProfileAboutToLoad() override {}
    void     OnProfileLoad(nlohmann::json) override {}
    nlohmann::json OnProfileSave() override { return nlohmann::json(); }
    unsigned char* OnSDKCommand(unsigned int, unsigned char*, unsigned int*) override { return nullptr; }
    void     ProfileManagerUpdated(unsigned int) override {}
    void     ResourceManagerUpdated(unsigned int) override {}
    void     SettingsManagerUpdated(unsigned int) override {}

private:
    OpenRGBPluginAPIInterface* resource_manager = nullptr;
    GUWidget*                 widget           = nullptr;
    RGBControllerInterface*   virtual_ctrl     = nullptr;
};
