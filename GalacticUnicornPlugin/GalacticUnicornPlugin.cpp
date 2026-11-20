#include "GalacticUnicornPlugin.h"
#include "GUWidget.h"

#include <string>
#include <QImage>
#include <QPainter>
#include <QLinearGradient>

GalacticUnicornPlugin::~GalacticUnicornPlugin()
{
}

OpenRGBPluginInfo GalacticUnicornPlugin::GetPluginInfo()
{
    OpenRGBPluginInfo info;

    info.Name        = "Galactic Unicorn";
    info.Description = "Stream OpenRGB colours to a Pimoroni Galactic Unicorn over USB serial";
    info.Version     = "1.0.0";
    info.Commit      = "";
    info.URL         = "";
    info.Location    = OPENRGB_PLUGIN_LOCATION_TOP;
    info.Label       = "Galactic Unicorn";
    info.TabIconString = "Galactic Unicorn";

    QImage icon(64, 64, QImage::Format_ARGB32);
    icon.fill(Qt::transparent);
    QPainter p(&icon);
    QLinearGradient g(0, 0, 64, 0);
    g.setColorAt(0.0, Qt::red);
    g.setColorAt(0.33, Qt::yellow);
    g.setColorAt(0.66, Qt::green);
    g.setColorAt(1.0, Qt::blue);
    p.fillRect(4, 18, 56, 28, g);
    p.end();
    info.Icon = icon;

    return info;
}

unsigned int GalacticUnicornPlugin::GetPluginAPIVersion()
{
    return OPENRGB_PLUGIN_API_VERSION;
}

void GalacticUnicornPlugin::Load(OpenRGBPluginAPIInterface* resource_manager_ptr)
{
    resource_manager = resource_manager_ptr;

    /*-----------------------------------------------------*\
    | Create a 53x11 matrix device that shows up in the     |
    | OpenRGB device list. Other plugins/effects can drive  |
    | it; this plugin streams its colours to the Unicorn.   |
    \*-----------------------------------------------------*/
    const unsigned int W = 53, H = 11, N = W * H;

    RGBController_Setup setup{};
    setup.name        = "Galactic Unicorn";
    setup.vendor      = "Pimoroni";
    setup.description = "Pimoroni Galactic Unicorn (53x11 LED matrix) over Wi-Fi";
    setup.version     = "1.0";
    setup.serial      = "GalacticUnicorn";
    setup.location    = "OpenRGB Galactic Unicorn plugin";
    setup.type        = DEVICE_TYPE_ACCESSORY;
    setup.flags       = CONTROLLER_FLAG_VIRTUAL;

    mode direct;
    direct.name       = "Direct";
    direct.value      = 0;
    direct.flags      = MODE_FLAG_HAS_PER_LED_COLOR;
    direct.color_mode = MODE_COLORS_PER_LED;
    setup.modes.push_back(direct);
    setup.active_mode = 0;

    zone z;
    z.name       = "Display";
    z.type       = ZONE_TYPE_MATRIX;
    z.leds_count = N;
    z.leds_min   = N;
    z.leds_max   = N;
    z.matrix_map.Set(H, W, nullptr);
    for(unsigned int i = 0; i < N; i++)
    {
        z.matrix_map.map[i] = i;   // row-major, matches the wire format
    }
    setup.zones.push_back(z);

    for(unsigned int y = 0; y < H; y++)
    {
        for(unsigned int x = 0; x < W; x++)
        {
            led l;
            l.name  = "LED " + std::to_string(x) + "," + std::to_string(y);
            l.value = y * W + x;
            setup.leds.push_back(l);
        }
    }

    virtual_ctrl = resource_manager->CreateVirtualRGBController(&setup);
    resource_manager->RegisterVirtualRGBControllerInThread(virtual_ctrl);
}

QWidget* GalacticUnicornPlugin::GetWidget()
{
    if(!widget)
    {
        widget = new GUWidget(resource_manager);
        widget->setVirtualController(virtual_ctrl);
    }
    return widget;
}

QMenu* GalacticUnicornPlugin::GetTrayMenu()
{
    return nullptr;
}

void GalacticUnicornPlugin::Unload()
{
    if(widget)
    {
        widget->shutdown();
    }

    if(resource_manager && virtual_ctrl)
    {
        resource_manager->UnregisterVirtualRGBController(virtual_ctrl);
        resource_manager->DeleteVirtualRGBController(virtual_ctrl);
        virtual_ctrl = nullptr;
    }
}
