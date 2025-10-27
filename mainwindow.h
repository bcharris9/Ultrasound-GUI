#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSerialPort>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QKeyEvent>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private slots:
    void on_btnStart_clicked();
    void on_btnStop_clicked();
    void on_btnOpenPort_clicked();
    void on_btnClosPort_clicked();
    void on_btnRefreshPorts_clicked();
    void on_btnZero_clicked();
    void on_PicoButton_clicked();
    void readData();

private:
    Ui::MainWindow *ui;

    // Serial port members
    QSerialPort *_serialPort;
    QSerialPort *Pico_Port;

    // CSV recording members
    QFile csvFile;
    QTextStream csvStream;
    bool csvRunning;
    double csvFramesPerSecond;
    int csvCaptureDuration;
    QTimer *csvTimer;

    // Sensor calibration values
    int zeroTopLeft;
    int zeroTopRight;
    int zeroBotLeft;

    // Helper functions
    void resetValues();
    void startCsvRecording();
    void stopCsvRecording();
    void writeCsvData();
    void handleCsvCapture();

};

#endif // MAINWINDOW_H
