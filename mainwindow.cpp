// Include necessary headers
#include "mainwindow.h"                 // MainWindow class header
#include "ui_mainwindow.h"              // Auto-generated UI header
#include <QSerialPortInfo>              // For serial port information
#include <QMessageBox>                 // For displaying message boxes
#include <QDateTime>                   // For date/time handling
#include <QDebug>                      // For debug output
#include <QStandardPaths>              // For accessing standard system paths

// MainWindow constructor
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)               // Initialize QMainWindow parent
    , ui(new Ui::MainWindow)            // Initialize UI
    , _serialPort(nullptr)              // Initialize serial port pointer to null
    , Pico_Port(nullptr)                 // Initialize Pico port pointer to null
    , csvRunning(false)                 // Initialize CSV recording flag to false
    , csvTimer(nullptr)                // Initialize CSV timer pointer to null
    , zeroTopLeft(0)                    // Initialize top left zero offset
    , zeroTopRight(0)                   // Initialize top right zero offset
    , zeroBotLeft(0)                    // Initialize bottom left zero offset
{
    ui->setupUi(this);                  // Set up the UI

    // Setup UI ranges for controls
    ui->framesPerSecond->setRange(0.00000001, 1000);        // Set FPS range (very small to 1000)
    ui->captureLengthSeconds->setRange(1, 3600);            // Set capture length range (1-3600 seconds)

    // Install event filters for numeric input controls
    ui->framesPerSecond->installEventFilter(this);          // Filter events for FPS control
    ui->captureLengthSeconds->installEventFilter(this);     // Filter events for capture length control

    // Refresh the list of available serial ports
    on_btnRefreshPorts_clicked();
}

// MainWindow destructor
MainWindow::~MainWindow()
{
    stopCsvRecording();                 // Ensure CSV recording is stopped

    // Clean up serial port resources
    if (_serialPort) {
        _serialPort->close();           // Close the serial port if open
        delete _serialPort;             // Delete the serial port object
    }

    // Clean up Pico port resources
    if (Pico_Port) {
        Pico_Port->close();             // Close the Pico port if open
        delete Pico_Port;               // Delete the Pico port object
    }

    delete ui;                         // Delete the UI object
}

// Event filter for handling specific key events
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Check if the event is for one of our numeric input controls and is a key press
    if ((watched == ui->framesPerSecond || watched == ui->captureLengthSeconds) &&
        event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);  // Cast to key event

        // Filter out Ctrl+8/9/0 combinations (potential special key combinations)
        if ((keyEvent->key() == Qt::Key_8 || keyEvent->key() == Qt::Key_9 ||
             keyEvent->key() == Qt::Key_0) &&
            (keyEvent->modifiers() & Qt::ControlModifier)) {
            return true;  // Block these key combinations
        }
    }
    return QMainWindow::eventFilter(watched, event);  // Pass other events to parent
}

// Key press event handler
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;  // Ignore auto-repeated key events

    // Handle your key press events here
    QMainWindow::keyPressEvent(event);  // Pass to parent class
}

// Key release event handler
void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) return;  // Ignore auto-repeated key events

    // Handle your key release events here
    QMainWindow::keyReleaseEvent(event);  // Pass to parent class
}

// Start button click handler
void MainWindow::on_btnStart_clicked()
{
    // Get current values from UI
    double fps = ui->framesPerSecond->value();      // Frames per second
    double duration = ui->captureLengthSeconds->value();  // Capture duration in seconds

    // Validate input values
    if (fps <= 0 || duration <= 0) {
        QMessageBox::warning(this, "Invalid Input",
                             "Frames per second and capture length must be greater than zero.");
        return;
    }

    // Calculate total frames needed (rounded to nearest integer)
    int totalFrames = qRound(fps * duration);

    startCsvRecording();  // Start CSV recording

    // Setup progress bar range and initial value
    ui->progressBar->setRange(0, totalFrames);  // Set range from 0 to total frames
    ui->progressBar->setValue(0);               // Start at 0

    // Clean up any existing timer
    if (csvTimer) {
        csvTimer->stop();            // Stop the timer if running
        csvTimer->deleteLater();     // Schedule for deletion
    }

    // Create new timer for frame capture
    csvTimer = new QTimer(this);
    int framesCaptured = 0;          // Counter for captured frames

    // Connect timer timeout signal to capture lambda function
    connect(csvTimer, &QTimer::timeout, this, [=]() mutable {
        // Check if we've captured all needed frames
        if (framesCaptured >= totalFrames) {
            csvTimer->stop();        // Stop the timer
            stopCsvRecording();      // Stop recording
            return;
        }

        // Capture data to CSV
        writeCsvData();

        // Send trigger to Pico if connected
        if (Pico_Port && Pico_Port->isOpen()) {
            Pico_Port->write("1");    // Send "1" as trigger
        }

        framesCaptured++;            // Increment frame counter
        ui->progressBar->setValue(framesCaptured);  // Update progress bar
    });

    // Calculate timer interval in milliseconds (minimum 1ms)
    int intervalMs = qMax(1, qRound(1000.0 / fps));
    csvTimer->start(intervalMs);     // Start the timer

    // Debug output of capture parameters
    qDebug() << "Starting capture with:"
             << "\nFPS:" << fps
             << "\nDuration:" << duration
             << "\nTotal frames:" << totalFrames
             << "\nInterval:" << intervalMs << "ms";
}

// Stop button click handler
void MainWindow::on_btnStop_clicked()
{
    if (csvTimer) {
        csvTimer->stop();            // Stop the capture timer if running
    }
    stopCsvRecording();              // Stop CSV recording
}

// Open port button click handler
void MainWindow::on_btnOpenPort_clicked()
{
    resetValues();  // Reset sensor values

    // Clean up existing serial port if any
    if (_serialPort) {
        _serialPort->close();        // Close the port if open
        delete _serialPort;          // Delete the port object
    }

    // Create and configure new serial port
    _serialPort = new QSerialPort(this);
    _serialPort->setPortName(ui->HC06Ports->currentText());  // Set port name from UI
    _serialPort->setBaudRate(QSerialPort::Baud9600);          // Set baud rate
    _serialPort->setDataBits(QSerialPort::Data8);              // 8 data bits
    _serialPort->setParity(QSerialPort::NoParity);             // No parity
    _serialPort->setStopBits(QSerialPort::OneStop);            // 1 stop bit
    _serialPort->setFlowControl(QSerialPort::NoFlowControl);   // No flow control

    // Try to open the port in read-only mode
    if (_serialPort->open(QIODevice::ReadOnly)) {
        // Connect readyRead signal to our readData slot
        connect(_serialPort, &QSerialPort::readyRead, this, &MainWindow::readData);
        QMessageBox::information(this, "Success", "Port opened successfully");
        ui->HC06Button->setStyleSheet("background-color: green");
    }

    else {
        // Show error if port opening failed
        QMessageBox::critical(this, "Error", "Failed to open port: " + _serialPort->errorString());
        delete _serialPort;
        _serialPort = nullptr;
        ui->HC06Button->setStyleSheet("background-color: red");
    }
}

// Pico button click handler
void MainWindow::on_PicoButton_clicked()
{
    // Clean up existing Pico port if any
    if (Pico_Port) {
        Pico_Port->close();          // Close the port if open
        delete Pico_Port;            // Delete the port object
        ui->PicoButton->setStyleSheet("background-color: red");
    }

    // Create and configure new Pico serial port
    Pico_Port = new QSerialPort(this);
    Pico_Port->setPortName(ui->PicoPorts->currentText());  // Set port name from UI
    Pico_Port->setBaudRate(QSerialPort::Baud115200);           // Higher baud rate for Pico
    Pico_Port->setDataBits(QSerialPort::Data8);                // 8 data bits
    Pico_Port->setParity(QSerialPort::NoParity);               // No parity
    Pico_Port->setStopBits(QSerialPort::OneStop);              // 1 stop bit
    Pico_Port->setFlowControl(QSerialPort::NoFlowControl);     // No flow control

    // Try to open the port in write-only mode
    if (Pico_Port->open(QIODevice::WriteOnly)) {
        QMessageBox::information(this, "Success", "Pico Port opened successfully");
        ui->PicoButton->setStyleSheet("background-color: green");
    }
    else {
        // Show error if port opening failed
        QMessageBox::critical(this, "Error", "Failed to open Pico port: " + Pico_Port->errorString());
        delete Pico_Port;
        Pico_Port = nullptr;
        ui->PicoButton->setStyleSheet("background-color: red");
    }
}

// Serial port data ready read handler
void MainWindow::readData()
{
    // Check if port exists and is open
    if (!_serialPort || !_serialPort->isOpen()) return;

    static QByteArray buffer;  // Static buffer to accumulate data
    buffer.append(_serialPort->readAll());  // Append new data to buffer

    // Convert buffer to string and split by commas
    QString data = QString::fromUtf8(buffer).trimmed();
    QStringList items = data.split(",");

    // Check if we have exactly 3 values (expected sensor data)
    if (items.size() == 3) {
        bool ok1, ok2, ok3;
        // Convert strings to integers
        int botLeft = items[0].toInt(&ok1);
        int topLeft = items[1].toInt(&ok2);
        int topRight = items[2].toInt(&ok3);

        // If all conversions succeeded, update displays with zero-adjusted values
        if (ok1 && ok2 && ok3) {
            ui->botLeftNum->display(botLeft - zeroBotLeft);
            ui->topLeftNum->display(topLeft - zeroTopLeft);
            ui->topRightNum->display(topRight - zeroTopRight);
        }
    }

    buffer.clear();  // Clear buffer for next read
}

// Zero button click handler
void MainWindow::on_btnZero_clicked()
{
    // Adjust zero offsets based on current displayed values
    zeroBotLeft += ui->botLeftNum->value();
    zeroTopLeft += ui->topLeftNum->value();
    zeroTopRight += ui->topRightNum->value();
}

// Refresh ports button click handler
void MainWindow::on_btnRefreshPorts_clicked()
{

    // If HC06 is not connected refresh the port
    if (!_serialPort) {

        // Clear existing port lists
        ui->HC06Ports->clear();

        // Add all available serial ports to combo
        foreach (const QSerialPortInfo &port, QSerialPortInfo::availablePorts()) {
            ui->HC06Ports->addItem(port.portName());

        }
    }

    // If Pico is not connected refresh the port
    if (!Pico_Port) {

        // Clear existing port list
        ui->PicoPorts->clear();

        // Add all available serial ports to combo
        foreach (const QSerialPortInfo &port, QSerialPortInfo::availablePorts()) {
            ui->PicoPorts->addItem(port.portName());
        }
    }
}

// Close port button click handler
void MainWindow::on_btnClosPort_clicked()
{
    if (_serialPort) {
        _serialPort->close();        // Close the port if open
        delete _serialPort;        // Delete the port object
        _serialPort = nullptr;      // Clear the pointer
        resetValues();              // Reset sensor values
        ui->HC06Button->setStyleSheet("background-color: red");
    }

    if (Pico_Port) {
        Pico_Port->close();        // Close the port if open
        delete Pico_Port;        // Delete the port object
        Pico_Port = nullptr;      // Clear the pointer
        ui->PicoButton->setStyleSheet("background-color: red");
    }


}

// Reset sensor values and zero offsets
void MainWindow::resetValues()
{
    zeroTopLeft = 0;                // Reset top left zero offset
    zeroTopRight = 0;               // Reset top right zero offset
    zeroBotLeft = 0;                // Reset bottom left zero offset

    // Reset displayed values to zero
    ui->botLeftNum->display(0);
    ui->topLeftNum->display(0);
    ui->topRightNum->display(0);
}

// Start CSV recording function
void MainWindow::startCsvRecording()
{
    // Create filename with timestamp on desktop
    QString fileName = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation) +
                       "/sensor_data_" + QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".csv";

    csvFile.setFileName(fileName);
    // Try to open file for writing
    if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", "Failed to create CSV file: " + csvFile.errorString());
        return;
    }

    // Set up text stream and write CSV header
    csvStream.setDevice(&csvFile);
    csvStream << "Timestamp,Top Left,Top Right,Bottom Left,Top Left w/o Zero,Top Right w/o Zero,Bottom Left w/o Zero\n";
    csvRunning = true;  // Set recording flag
}

// Stop CSV recording function
void MainWindow::stopCsvRecording()
{
    csvRunning = false;  // Clear recording flag
    if (csvFile.isOpen()) {
        csvFile.close();  // Close the file if open
    }
}

// Write data to CSV function
void MainWindow::writeCsvData()
{
    // Check if we should record and file is open
    if (!csvRunning || !csvFile.isOpen()) return;

    // Write current timestamp and sensor values (both zero-adjusted and raw)
    csvStream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") << ","
              << ui->topLeftNum->value() << ","
              << ui->topRightNum->value() << ","
              << ui->botLeftNum->value() << ","
              << ui->topLeftNum->value()+zeroTopLeft << ","
              << ui->topRightNum->value()+zeroTopRight << ","
              << ui->botLeftNum->value()+zeroBotLeft << "\n";

    csvStream.flush();  // Ensure data is written to file
}

