#pragma once

#include <QWidget>
#include <QColor>
#include <QByteArray>
#include <QElapsedTimer>
#include <QSerialPort>

#include "OpenRGBPluginInterface.h"

class QTimer;
class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QCheckBox;
class QSlider;
class QLabel;

class GUWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GUWidget(OpenRGBPluginAPIInterface* rm, QWidget* parent = nullptr);
    ~GUWidget() override;

    void shutdown();
    void setVirtualController(RGBControllerInterface* vc);

private slots:
    void onConnectClicked();
    void onSerialReadyRead();
    void onSerialError(QSerialPort::SerialPortError err);
    void refreshPorts();
    void onFrameTick();
    void onReconnectTick();
    void onDeviceWatchTick();
    void refreshDevices();
    void pickColour();
    void updateEnabledStates();
    void saveSettings();

private:
    void buildFrame(QByteArray& frame);
    void setStatus(const QString& text, const QString& colour);
    void startConnection();

    OpenRGBPluginAPIInterface* rm;
    RGBControllerInterface*    virtualCtrl = nullptr;

    QSerialPort* serial;
    QElapsedTimer ackClock;
    bool         deviceReady = false;   // Unicorn script is running and answering
    int          inflight    = 0;       // frames sent but not yet acknowledged
    QTimer*     frameTimer;
    QTimer*     reconnectTimer;
    QTimer*     deviceWatchTimer;

    QComboBox*   portCombo;
    QPushButton* portRefreshButton;
    QSpinBox*    fpsSpin;
    QSlider*     brightSlider;
    QLabel*      brightLabel;
    QComboBox*   modeCombo;
    QComboBox*   deviceCombo;
    QPushButton* refreshButton;
    QPushButton* colourButton;
    QCheckBox*   autoReconnect;
    QPushButton* connectButton;
    QLabel*      statusLabel;

    QColor solidColour;
    bool   wantConnected;
    int    rainbowPhase;
};
