#include "GUWidget.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QGridLayout>
#include <QSerialPortInfo>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>

#include <cstdint>
#include <cstring>
#include <vector>

#include "RGBControllerInterface.h"

// Wire protocol (must match the Pico firmware):
//   "GU53" + 583 * (R,G,B), row-major, 53 columns x 11 rows
static const int GU_W           = 53;
static const int GU_H           = 11;
static const int GU_LED_COUNT   = GU_W * GU_H;
static const int GU_FRAME_BYTES = 4 + GU_LED_COUNT * 3;

enum Mode
{
    MODE_VIRTUAL    = 0,   // stream the "Galactic Unicorn" device shown in OpenRGB's device list
    MODE_MIRROR_ONE = 1,
    MODE_MIRROR_ALL = 2,
    MODE_RAINBOW    = 3,
    MODE_SOLID      = 4
};

static inline void setPx(uint8_t* px, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t* p = px + (y * GU_W + x) * 3;
    p[0] = r;
    p[1] = g;
    p[2] = b;
}

// OpenRGB's RGBColor is 0x00BBGGRR
static inline void unpack(unsigned int c, uint8_t& r, uint8_t& g, uint8_t& b)
{
    r = (uint8_t)(c & 0xFF);
    g = (uint8_t)((c >> 8) & 0xFF);
    b = (uint8_t)((c >> 16) & 0xFF);
}

// Spread a linear list of LEDs across the 53 columns, repeated on all 11 rows.
template <class V>
static void fillLinear(const V& cols, uint8_t* px)
{
    size_t n = cols.size();
    if(n == 0) return;

    for(int x = 0; x < GU_W; x++)
    {
        size_t i = (size_t)x * n / GU_W;
        uint8_t r, g, b;
        unpack((unsigned int)cols[i], r, g, b);
        for(int y = 0; y < GU_H; y++)
        {
            setPx(px, x, y, r, g, b);
        }
    }
}

GUWidget::GUWidget(OpenRGBPluginAPIInterface* rm_ptr, QWidget* parent)
    : QWidget(parent),
      rm(rm_ptr),
      solidColour(255, 255, 255),
      wantConnected(false),
      rainbowPhase(0)
{
    serial           = new QSerialPort(this);
    frameTimer       = new QTimer(this);
    reconnectTimer   = new QTimer(this);
    deviceWatchTimer = new QTimer(this);

    QGridLayout* grid = new QGridLayout();
    int row = 0;

    portCombo = new QComboBox();
    portRefreshButton = new QPushButton("Rescan");
    grid->addWidget(new QLabel("USB serial port:"), row, 0);
    grid->addWidget(portCombo, row, 1);
    grid->addWidget(portRefreshButton, row++, 2);

    fpsSpin = new QSpinBox();
    fpsSpin->setRange(1, 60);
    fpsSpin->setValue(40);
    grid->addWidget(new QLabel("Frame rate (FPS):"), row, 0);
    grid->addWidget(fpsSpin, row++, 1);

    brightSlider = new QSlider(Qt::Horizontal);
    brightSlider->setRange(5, 100);
    brightSlider->setValue(100);
    brightLabel = new QLabel("100%");
    grid->addWidget(new QLabel("Brightness:"), row, 0);
    grid->addWidget(brightSlider, row, 1);
    grid->addWidget(brightLabel, row++, 2);

    modeCombo = new QComboBox();
    modeCombo->addItem("Galactic Unicorn device (OpenRGB device list)");
    modeCombo->addItem("Mirror one device");
    modeCombo->addItem("Mirror all devices");
    modeCombo->addItem("Rainbow (test pattern)");
    modeCombo->addItem("Solid colour");
    grid->addWidget(new QLabel("Source:"), row, 0);
    grid->addWidget(modeCombo, row++, 1, 1, 2);

    deviceCombo   = new QComboBox();
    refreshButton = new QPushButton("Refresh");
    grid->addWidget(new QLabel("Device:"), row, 0);
    grid->addWidget(deviceCombo, row, 1);
    grid->addWidget(refreshButton, row++, 2);

    colourButton = new QPushButton("Pick colour...");
    grid->addWidget(new QLabel("Solid colour:"), row, 0);
    grid->addWidget(colourButton, row++, 1);

    autoReconnect = new QCheckBox("Auto-reconnect");
    autoReconnect->setChecked(true);
    grid->addWidget(autoReconnect, row++, 1);

    connectButton = new QPushButton("Connect");
    grid->addWidget(connectButton, row++, 1);

    statusLabel = new QLabel();
    grid->addWidget(statusLabel, row++, 0, 1, 3);

    grid->setRowStretch(row, 1);
    grid->setColumnStretch(1, 1);

    QWidget* holder = new QWidget();
    holder->setLayout(grid);
    holder->setMaximumWidth(520);

    QGridLayout* outer = new QGridLayout(this);
    outer->addWidget(holder, 0, 0, Qt::AlignTop | Qt::AlignLeft);

    // Load saved settings
    QSettings s("OpenRGBGalacticUnicorn", "Plugin");
    refreshPorts();
    int savedPort = portCombo->findData(s.value("serialport", "").toString());
    if(savedPort >= 0) portCombo->setCurrentIndex(savedPort);
    fpsSpin->setValue(s.value("fps", 40).toInt());
    brightSlider->setValue(s.value("brightness", 100).toInt());
    modeCombo->setCurrentIndex(s.value("source", MODE_VIRTUAL).toInt());
    solidColour = QColor(s.value("solid", "#ffffff").toString());
    autoReconnect->setChecked(s.value("autoreconnect", true).toBool());
    brightLabel->setText(QString("%1%").arg(brightSlider->value()));

    refreshDevices();
    int savedIdx = deviceCombo->findText(s.value("device", "").toString());
    if(savedIdx >= 0) deviceCombo->setCurrentIndex(savedIdx);

    updateEnabledStates();
    setStatus("Disconnected", "#cc4444");

    connect(connectButton, &QPushButton::clicked, this, &GUWidget::onConnectClicked);
    connect(refreshButton, &QPushButton::clicked, this, &GUWidget::refreshDevices);
    connect(colourButton,  &QPushButton::clicked, this, &GUWidget::pickColour);
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){ updateEnabledStates(); saveSettings(); });
    connect(deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){ saveSettings(); });
    connect(brightSlider, &QSlider::valueChanged, this, [this](int v){ brightLabel->setText(QString("%1%").arg(v)); saveSettings(); });
    connect(fpsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v){ frameTimer->setInterval(1000 / v); saveSettings(); });
    connect(portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int){ saveSettings(); });
    connect(portRefreshButton, &QPushButton::clicked, this, &GUWidget::refreshPorts);
    connect(autoReconnect, &QCheckBox::toggled, this, [this](bool){ saveSettings(); });

    connect(serial, &QSerialPort::readyRead,       this, &GUWidget::onSerialReadyRead);
    connect(serial, &QSerialPort::errorOccurred,   this, &GUWidget::onSerialError);

    frameTimer->setTimerType(Qt::PreciseTimer);
    frameTimer->setInterval(1000 / fpsSpin->value());
    connect(frameTimer, &QTimer::timeout, this, &GUWidget::onFrameTick);

    reconnectTimer->setInterval(3000);
    connect(reconnectTimer, &QTimer::timeout, this, &GUWidget::onReconnectTick);

    deviceWatchTimer->setInterval(3000);
    connect(deviceWatchTimer, &QTimer::timeout, this, &GUWidget::onDeviceWatchTick);
    deviceWatchTimer->start();
}

GUWidget::~GUWidget()
{
    shutdown();
}

void GUWidget::setVirtualController(RGBControllerInterface* vc)
{
    virtualCtrl = vc;
}

void GUWidget::shutdown()
{
    virtualCtrl = nullptr;
    wantConnected = false;
    frameTimer->stop();
    reconnectTimer->stop();
    deviceWatchTimer->stop();
    if(serial->isOpen())
    {
        serial->close();
    }
}

void GUWidget::saveSettings()
{
    QSettings s("OpenRGBGalacticUnicorn", "Plugin");
    s.setValue("serialport", portCombo->currentData().toString());
    s.setValue("fps", fpsSpin->value());
    s.setValue("brightness", brightSlider->value());
    s.setValue("source", modeCombo->currentIndex());
    s.setValue("solid", solidColour.name());
    s.setValue("device", deviceCombo->currentText());
    s.setValue("autoreconnect", autoReconnect->isChecked());
}

void GUWidget::setStatus(const QString& text, const QString& colour)
{
    statusLabel->setText(QString("<b style='color:%1'>%2</b>").arg(colour, text.toHtmlEscaped()));
}

void GUWidget::updateEnabledStates()
{
    int m = modeCombo->currentIndex();
    deviceCombo->setEnabled(m == MODE_MIRROR_ONE);
    refreshButton->setEnabled(m == MODE_MIRROR_ONE);
    colourButton->setEnabled(m == MODE_SOLID);
}

void GUWidget::pickColour()
{
    QColor c = QColorDialog::getColor(solidColour, this, "Solid colour");
    if(c.isValid())
    {
        solidColour = c;
        saveSettings();
    }
}

void GUWidget::refreshDevices()
{
    QString previous = deviceCombo->currentText();
    deviceCombo->blockSignals(true);
    deviceCombo->clear();

    if(rm)
    {
        std::vector<RGBControllerInterface*> ctrls = rm->GetRGBControllers();
        for(size_t i = 0; i < ctrls.size(); i++)
        {
            deviceCombo->addItem(QString::fromStdString(ctrls[i]->GetName()), (int)i);
        }
    }

    int idx = deviceCombo->findText(previous);
    if(idx >= 0) deviceCombo->setCurrentIndex(idx);
    deviceCombo->blockSignals(false);
}

void GUWidget::onDeviceWatchTick()
{
    if(!rm) return;
    if((size_t)deviceCombo->count() != rm->GetRGBControllers().size())
    {
        refreshDevices();
    }
}

static const quint16 PICO_VID = 0x2E8A;   // Raspberry Pi
static const char     GU_ACK    = (char)0xA5;  // Pico -> host: ready / frame done

void GUWidget::refreshPorts()
{
    QString previous = portCombo->currentData().toString();
    portCombo->blockSignals(true);
    portCombo->clear();
    portCombo->addItem("Auto (Raspberry Pi Pico, VID 2E8A)", "");
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for(const QSerialPortInfo& p : ports)
    {
        QString label = p.portName();
        if(p.hasVendorIdentifier())
        {
            label += QString(" (%1:%2)").arg(p.vendorIdentifier(), 4, 16, QChar('0'))
                                         .arg(p.hasProductIdentifier() ? p.productIdentifier() : 0, 4, 16, QChar('0'));
        }
        portCombo->addItem(label, p.portName());
    }
    int idx = portCombo->findData(previous);
    portCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    portCombo->blockSignals(false);
}

void GUWidget::startConnection()
{
    QString wanted = portCombo->currentData().toString();
    QSerialPortInfo found;
    bool ok = false;

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    for(const QSerialPortInfo& p : ports)
    {
        if(wanted.isEmpty() ? (p.hasVendorIdentifier() && p.vendorIdentifier() == PICO_VID)
                            : (p.portName() == wanted))
        {
            found = p;
            ok = true;
            break;
        }
    }

    if(!ok)
    {
        setStatus(wanted.isEmpty() ? "No Pico (VID 2E8A) found - is it plugged in?"
                                   : "Port " + wanted + " not found", "#cc8800");
        return;
    }

    if(serial->isOpen()) serial->close();
    deviceReady = false;
    inflight    = 0;

    serial->setPort(found);
    serial->setBaudRate(QSerialPort::Baud115200);   // ignored by USB CDC
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);
    serial->setFlowControl(QSerialPort::NoFlowControl);

    if(!serial->open(QIODevice::ReadWrite))
    {
        setStatus("Cannot open " + found.portName() + ": " + serial->errorString(), "#cc4444");
        return;
    }
    serial->setDataTerminalReady(true);
    serial->setRequestToSend(true);
    serial->clear();

    ackClock.start();
    setStatus("Opened " + found.portName() + " - waiting for the Unicorn script (run openrgb.py on it)", "#cc8800");
    frameTimer->start();
}

void GUWidget::onConnectClicked()
{
    if(wantConnected)
    {
        wantConnected = false;
        frameTimer->stop();
        reconnectTimer->stop();
        if(serial->isOpen()) serial->close();
        connectButton->setText("Connect");
        setStatus("Disconnected", "#cc4444");
    }
    else
    {
        wantConnected = true;
        connectButton->setText("Disconnect");
        saveSettings();
        reconnectTimer->start();
        startConnection();
    }
}

void GUWidget::onSerialReadyRead()
{
    QByteArray data = serial->readAll();
    // Anything other than our marker byte is REPL chatter - ignore it.
    int acks = data.count(GU_ACK);
    if(acks > 0)
    {
        inflight = qMax(0, inflight - acks);
        ackClock.restart();
        if(!deviceReady)
        {
            deviceReady = true;
            setStatus("Connected - streaming", "#33aa55");
        }
    }
}

void GUWidget::onSerialError(QSerialPort::SerialPortError err)
{
    if(err == QSerialPort::NoError) return;

    if(err == QSerialPort::ResourceError || err == QSerialPort::DeviceNotFoundError ||
       err == QSerialPort::PermissionError)
    {
        frameTimer->stop();
        if(serial->isOpen()) serial->close();
        deviceReady = false;
        if(wantConnected)
        {
            setStatus("Serial error: " + serial->errorString() +
                      (err == QSerialPort::PermissionError ? " (add your user to the 'uucp'/'dialout' group)" : ""),
                      "#cc4444");
        }
    }
}

void GUWidget::onReconnectTick()
{
    if(!wantConnected || !autoReconnect->isChecked()) return;
    if(!serial->isOpen())
    {
        startConnection();
    }
}

void GUWidget::onFrameTick()
{
    if(!serial->isOpen()) return;

    // The Unicorn answers with an ack byte after rendering each frame (and
    // every ~0.5s while idle). No ack for 2s => script not running / stalled.
    if(ackClock.elapsed() > 2000)
    {
        if(deviceReady)
        {
            deviceReady = false;
            setStatus("Opened " + serial->portName() + " - Unicorn not answering (run openrgb.py on it)", "#cc8800");
        }
        inflight = 0;
    }

    // Keep up to 2 frames in flight so USB latency is hidden, without
    // letting a backlog (lag) build up.
    if(!deviceReady || inflight >= 2) return;

    QByteArray frame;
    buildFrame(frame);
    serial->write(frame);
    inflight++;
}

void GUWidget::buildFrame(QByteArray& frame)
{
    frame.resize(GU_FRAME_BYTES);
    char* out = frame.data();
    memcpy(out, "GU53", 4);
    uint8_t* px = reinterpret_cast<uint8_t*>(out + 4);
    memset(px, 0, GU_LED_COUNT * 3);

    switch(modeCombo->currentIndex())
    {
    case MODE_VIRTUAL:
    {
        // The virtual device is a 53x11 matrix zone whose LED index is y*53+x,
        // identical to the wire format, so colours map 1:1.
        if(!virtualCtrl) break;
        RGBColor*    vcol = virtualCtrl->GetColorsPointer();
        unsigned int n    = virtualCtrl->GetLEDCount();
        if(!vcol) break;
        if(n > (unsigned int)GU_LED_COUNT) n = GU_LED_COUNT;
        for(unsigned int i = 0; i < n; i++)
        {
            unpack((unsigned int)vcol[i], px[i * 3 + 0], px[i * 3 + 1], px[i * 3 + 2]);
        }
        break;
    }

    case MODE_MIRROR_ONE:
    {
        if(!rm) break;
        std::vector<RGBControllerInterface*> ctrls = rm->GetRGBControllers();
        int idx = deviceCombo->currentData().toInt();
        if(deviceCombo->count() == 0 || idx < 0 || idx >= (int)ctrls.size()) break;

        RGBControllerInterface* rc = ctrls[idx];
        unsigned int  ledCount = rc->GetLEDCount();
        RGBColor*     colors   = rc->GetColorsPointer();
        if(!colors) break;

        // Prefer a matrix zone (keyboards, panels) and scale it to 53x11.
        for(unsigned int zi = 0; zi < rc->GetZoneCount(); zi++)
        {
            const unsigned int* mmap = rc->GetZoneMatrixMapData(zi);
            if(rc->GetZoneType(zi) == ZONE_TYPE_MATRIX && mmap)
            {
                unsigned int H = rc->GetZoneMatrixMapHeight(zi);
                unsigned int W = rc->GetZoneMatrixMapWidth(zi);
                unsigned int startIdx = rc->GetZoneStartIndex(zi);
                if(W == 0 || H == 0) continue;

                for(int y = 0; y < GU_H; y++)
                {
                    for(int x = 0; x < GU_W; x++)
                    {
                        unsigned int sx = (unsigned int)x * W / GU_W;
                        unsigned int sy = (unsigned int)y * H / GU_H;
                        unsigned int v  = mmap[sy * W + sx];
                        if(v == 0xFFFFFFFF) continue;

                        size_t ci = (size_t)startIdx + v;
                        if(ci < ledCount)
                        {
                            uint8_t r, g, b;
                            unpack((unsigned int)colors[ci], r, g, b);
                            setPx(px, x, y, r, g, b);
                        }
                    }
                }
                goto brightness;
            }
        }

        // Otherwise treat the device as one long strip.
        fillLinear(std::vector<unsigned int>(colors, colors + ledCount), px);
        break;
    }

    case MODE_MIRROR_ALL:
    {
        if(!rm) break;
        std::vector<unsigned int> all;
        std::vector<RGBControllerInterface*> ctrls = rm->GetRGBControllers();
        for(RGBControllerInterface* rc : ctrls)
        {
            RGBColor* cp = rc->GetColorsPointer();
            for(unsigned int i = 0; cp && i < rc->GetLEDCount(); i++)
            {
                all.push_back((unsigned int)cp[i]);
            }
        }
        fillLinear(all, px);
        break;
    }

    case MODE_RAINBOW:
    {
        for(int x = 0; x < GU_W; x++)
        {
            QColor c = QColor::fromHsv(((x * 360 / GU_W) + rainbowPhase) % 360, 255, 255);
            for(int y = 0; y < GU_H; y++)
            {
                setPx(px, x, y, (uint8_t)c.red(), (uint8_t)c.green(), (uint8_t)c.blue());
            }
        }
        rainbowPhase = (rainbowPhase + 4) % 360;
        break;
    }

    case MODE_SOLID:
    {
        for(int i = 0; i < GU_LED_COUNT; i++)
        {
            px[i * 3 + 0] = (uint8_t)solidColour.red();
            px[i * 3 + 1] = (uint8_t)solidColour.green();
            px[i * 3 + 2] = (uint8_t)solidColour.blue();
        }
        break;
    }
    }

brightness:
    int b = brightSlider->value();
    if(b < 100)
    {
        for(int i = 0; i < GU_LED_COUNT * 3; i++)
        {
            px[i] = (uint8_t)(px[i] * b / 100);
        }
    }
}
