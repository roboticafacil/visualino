#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "aboutdialog.h"
#include "filescore.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QMessageBox>
#include <QKeyEvent>
#include <QtSerialPort/QSerialPortInfo>
#include <QScrollBar>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QWebEnginePage>
#include <QEventLoop>
#include <QUrlQuery>
#include <QSignalMapper>
#include <QtWebChannel/QWebChannel>
#include <QUndoStack>
#include <QDomDocument>
#include <QClipboard>
#include <QListWidget>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{

    this->alert=false;
    signalMapper = new QSignalMapper(this);
    // Set environment
    settings = new SettingsStore(CONFIG_INI);
    ui->setupUi(this);
    setToolboxCategories();
    // Align last toolbar action to the right
    QWidget *empty = new QWidget(this);
    empty->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    ui->mainToolBar->insertWidget(ui->actionMonitor, empty);
    setSearchDocWidget();

    // Align last action to the right in the monitor toolbar
    QWidget *emptyMonitor = new QWidget(this);
    emptyMonitor->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    ui->monitorToolBar->insertWidget(ui->actionMonitor, emptyMonitor);
    // Hide monitor toolbar
    ui->monitorToolBar->setVisible(false);
    ui->licenseLabel->setText("Checking license...");
    ui->license->setVisible(false);


    // Hide graphs widget
    ui->graphsWidget->setVisible(false);


    // Set monospaced font in the monitor
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    ui->consoleText->setFont(fixedFont);


    setArduinoBoard();
    xmlFileName = "";
    this->myblocks=false; //The standard Facilino programming view
    serial = nullptr;
    //QWebSettings::globalSettings()->setAttribute(QWebSettings::LocalContentCanAccessRemoteUrls, true);
    //ui->webView->settings()->setAttribute(QWebSettings::LocalContentCanAccessRemoteUrls,true);
    float zoomScale = settings->zoomScale();
    ui->webView->setZoomFactor((double)zoomScale);

    initCategories();
    //actionLicense();

    // Hide messages
    actionCloseMessages();
    serialPortClose();

    actionInjectWebHelper();
    // Load blockly index

    loadBlockly();
    webHelper->setSourceChangeEnable(true);
    documentHistory.clear();
    documentHistory.setUndoLimit(0);  //No limit


    // Set timer to update list of available ports
    updateSerialPorts();
    QTimer *timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateSerialPorts()));
    timer->start(5000);
    licenseTimer = new QTimer(this);
    connect(licenseTimer, SIGNAL(timeout()), this, SLOT(checkLicense()));
    licenseTimer->start(1000);  //Initially waits 1sec for checking the license
    //checkLicense();

    //Hide MyBlocks toolbar
    this->switchToMyBlocks(false);

    ui->consoleText->document()->setMaximumBlockCount(100);
    //ui->messagesWidget->show();
    // Show/hide icon labels in the menu bar
    iconLabels();

    // Set process
    process = new QProcess();
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process,
            SIGNAL(started()),
            this,
            SLOT(onProcessStarted()));
    connect(process,
            SIGNAL(readyReadStandardOutput()),
            this,
            SLOT(onProcessOutputUpdated()));
    connect(process,
            SIGNAL(finished(int)),
            this,
            SLOT(onProcessFinished(int)));

    // Show opened file name in status bar
    connect(statusBar(),
            SIGNAL(messageChanged(QString)),
            this,
            SLOT(onStatusMessageChanged(QString)));
    ui->actionUndo->setEnabled(false);
    ui->actionRedo->setEnabled(false);
    connect(&documentHistory,SIGNAL(canUndoChanged(bool)),this,SLOT(onUndoChanged(bool)));
    connect(&documentHistory,SIGNAL(canRedoChanged(bool)),this,SLOT(onRedoChanged(bool)));
    // Filter events to capture backspace key
    ui->webView->installEventFilter(this);
    this->showMaximized();
}

MainWindow::~MainWindow() {
    delete webHelper;
    delete serial;
    delete settings;
    delete process;
    delete ui;
}

void MainWindow::arduinoExec(const QString &action) {
    QStringList arguments;

    // Check if temp path exists
    QDir dir(settings->tmpDirName());
    if (dir.exists() == false) {
        dir.mkdir(settings->tmpDirName());
    }

    // Check if tmp file exists
    QFile tmpFile(settings->tmpFileName());
    if (tmpFile.exists()) {
        tmpFile.remove();
    }
    tmpFile.open(QIODevice::WriteOnly);

    // Read code
    QString codeString = evaluateJavaScript("Blockly.Arduino.workspaceToCode();");

    // Write code to tmp file
    tmpFile.write(codeString.toLocal8Bit());
    tmpFile.close();

    // Verify code
    arguments << action;
    // Board parameter
    if (ui->boardBox->count() > 0) {
        arguments << "--board" << ui->boardBox->currentText();
    }
    // Port parameter
    if (ui->serialPortBox->count() > 0) {
        arguments << "--port" << ui->serialPortBox->currentText();
    }
    //arguments << "--pref editor.external=false ";
    arguments << settings->tmpFileName();
    process->start(settings->arduinoIdePath(), arguments);

    // Show messages
    ui->messagesWidget->show();
}

void MainWindow::actionAbout() {
    // Open About dialog
    AboutDialog aboutDialog(this);
    aboutDialog.exec();
}

void MainWindow::actionCode() {
    // Show/hide code
    QString jsLanguage = QString("toogleCode();");
	evaluateJavaScript(jsLanguage);
}

void MainWindow::actionDoc() {
    // Show/hide code
    QString jsLanguage = QString("toogleDoc();");
	evaluateJavaScript(jsLanguage);
}

void MainWindow::actionExamples() {
    // Check whether source was changed
    if (checkSourceChanged() == QMessageBox::Cancel) {
        return;
    }

    // Open an example from the examples folder
    actionOpenInclude(tr("Examples"), true, QDir::currentPath()+settings->examplesPath());
    // Void file name to prevent overwriting the original file by mistake
    setXmlFileName("");
}

void MainWindow::actionExportSketch() {
    // Export workspace as Arduino Sketch
    QString inoFileName;

    // Open file dialog
    QFileDialog fileDialog(this, tr("Save"));
    fileDialog.setFileMode(QFileDialog::AnyFile);
    fileDialog.setNameFilter(QString("Sketches %1").arg("(*.ino)"));
    fileDialog.setDefaultSuffix("ino");
    fileDialog.setLabelText(QFileDialog::Accept, tr("Export"));
    if (!fileDialog.exec()) return; // Return if cancelled
    QStringList selectedFiles = fileDialog.selectedFiles();
    // Return if no file to open
    if (selectedFiles.count() < 1) return;
    inoFileName = selectedFiles.at(0);

    int result = saveSketch(inoFileName);

    if (result == 0) {
        // Display error message
        QMessageBox msgBox(this);
        msgBox.setText(QString(tr("Couldn't open file to save content: %1.")).arg(inoFileName));
        msgBox.exec();
        return;
    }

    // Feedback
    QString message(tr("Done exporting: %1.").arg(inoFileName));
    statusBar()->showMessage(message, 2000);
}

void MainWindow::onSourceChanged() {
    if (!webHelper->isSourceChangeEnabled())
        return;
    if (sourceChanging) {
        sourceChanging = false;
        return;
    }
    QString code("Blockly.Xml.domToText(Blockly.Xml.workspaceToDom(Blockly.getMainWorkspace()));");
    QWebEnginePage *page = ui->webView->page();
    page->runJavaScript(code, [&](const QVariant var){
        QString xml = var.toString();
        if (documentHistory.count()>0)
        {
            if (QString::compare(lastDocument,xml)==0) return;
            //if (QString::compare(QString("<xml xmlns=\"http://www.w3.org/1999/xhtml\"></xml>"),xml)==0) return;
            if (documentHistory.index()==documentHistoryStep && (QString::compare(documentHistory.text(documentHistory.index()-1),xml)==0)){ return;}
        }
        QUndoCommand *command = new QUndoCommand(xml);
        documentHistory.push(command);
        lastDocument=xml;
        //ui->licenseLabel->setText(QString::number(documentHistory.index())+" "+QString::number(documentHistory.count()));
        //ui->textBrowser->append(QString::number(documentHistory.index())+" "+escapeCharacters(xml));
    });
}

void MainWindow::actionDocumentUndo() {
    if (documentHistory.canUndo()&&documentHistory.index()>1)
    {
        documentHistory.undo();
        documentHistoryStep=documentHistory.index();
        //ui->licenseLabel->setText(QString::number(documentHistory.index())+" "+QString::number(documentHistory.count()));
        //ui->textBrowser->append(QString::number(documentHistory.index())+" "+escapeCharacters(documentHistory.text(documentHistory.index()-1)));
        setXml(documentHistory.text(documentHistory.index()-1), true);
    }
    else
        ui->actionUndo->setEnabled(false);
}
void MainWindow::actionDocumentRedo() {
    if (documentHistory.canRedo()&&(documentHistory.index()<documentHistory.count()))
    {
        documentHistory.redo();
        documentHistoryStep=documentHistory.index();
        //ui->licenseLabel->setText(QString::number(documentHistory.index())+" "+QString::number(documentHistory.count()));
        //ui->textBrowser->append(QString::number(documentHistory.index())+" "+escapeCharacters(documentHistory.text(documentHistory.index()-1)));
        setXml(documentHistory.text(documentHistory.index()-1), true);
    }
    else
        ui->actionRedo->setEnabled(false);
}

void MainWindow::onUndoChanged(bool canUndo)
{
    ui->actionUndo->setEnabled(canUndo);
}

void MainWindow::onRedoChanged(bool canRedo)
{
    ui->actionRedo->setEnabled(canRedo);
}

void MainWindow::documentHistoryReset() {
    // Clear history of changes
    documentHistory.clear();
}

void MainWindow::actionGraph() {
    // Show/hide graph
    if (ui->consoleText->isVisible() == true) {
        // Show graph
        ui->consoleText->setVisible(false);
        ui->graphsWidget->setVisible(true);
        ui->actionGraph->setChecked(true);
    } else {
        // Hide graph
        ui->consoleText->setVisible(true);
        ui->graphsWidget->setVisible(false);
        ui->actionGraph->setChecked(false);
    }
}

void MainWindow::actionIconLabels() {
    // Change icon labels mode
    bool showIconLabels = settings->iconLabels();
    showIconLabels = !showIconLabels;
    // Update settings
    settings->setIconLabels(showIconLabels);
    // Update display
    iconLabels();
}

void MainWindow::actionInclude() {
    // Include blockly file to the current workspace
    actionOpenInclude(tr("Include file"), false);
}

void MainWindow::actionInsertLanguage() {
    // Set language in Roboblocks
    QString jsLanguage = QString("var roboblocksLanguage = '%1';").
            arg(settings->defaultLanguage());
	ui->webView->page()->runJavaScript(jsLanguage);
}

void MainWindow::actionLicense() {
    // Set license
    QString jsLanguage = QString("window.webHelper.license = '%1';").
            arg(settings->license());
    ui->webView->page()->runJavaScript(jsLanguage);
}

void MainWindow::actionMonitor() {
    // Open close monitor
    if (ui->widgetConsole->isVisible()) {
        serialPortClose();
        ui->actionMonitor->setChecked(false);
        // Show main toolbar, hide monitor toolbar
        ui->mainToolBar->setVisible(true);
        ui->monitorToolBar->setVisible(false);
    } else {
        serialPortOpen();
        ui->consoleEdit->setFocus();
        ui->actionMonitor->setChecked(true);
        // Hide main toolbar, show monitor toolbar
        ui->mainToolBar->setVisible(false);
        ui->monitorToolBar->setVisible(true);
    }
}

void MainWindow::actionMonitorSend() {
    // Send what's available in the console line edit
    if (serial == nullptr) return;

    QString data = ui->consoleEdit->text();
    if (data.isEmpty()) return; // Nothing to send

    // Send data
    qint64 result = serial->write(data.toLocal8Bit());
    if (result != -1) {
        // If data was sent successfully, clear line edit
        ui->consoleText->insertHtml("&rarr;&nbsp;");
        ui->consoleText->insertPlainText(data + "\n");
        ui->consoleEdit->clear();
    }
}

void MainWindow::actionMessages() {
    // Open/hide messages window
    if (ui->messagesWidget->isVisible()) {
        actionCloseMessages();
    } else {
        actionOpenMessages();
    }
}

void MainWindow::actionNew() {
    // Check whether source was changed
    if (checkSourceChanged() == QMessageBox::Cancel) {
        return;
    }
    // Unset file name
    setXmlFileName("");
    // Reset source change status
    webHelper->resetSourceChanged();
    // Disable save as
    ui->actionSave_as->setEnabled(false);
    // Clear workspace
    if (!this->myblocks)
      evaluateJavaScript("resetWorkspace();");
    else
      evaluateJavaScript("BlockFactory.showStarterBlock();");
    // Reset history
    documentHistoryReset();
}

void MainWindow::actionCloseMessages() {
    // Hide messages window
    ui->messagesWidget->hide();
    ui->actionMessages->setChecked(false);
}

void MainWindow::actionOpen() {
    // Check whether source was changed
    if (checkSourceChanged() == QMessageBox::Cancel) {
        return;
    }
    // Open file
    QString directory = QStandardPaths::locate(
                QStandardPaths::DocumentsLocation,
                "",QStandardPaths::LocateDirectory);
    actionOpenInclude(tr("Open file"), true, directory);

    // Reset source changed and history
    documentHistoryReset();
    webHelper->resetSourceChanged();
}

void MainWindow::actionOpenInclude(const QString &title,
                                   bool clear,
                                   const QString &directory) {
    // Open file dialog
    QString extension;
    if (!myblocks)
        extension = QString("bly");
    else
        extension = QString("bli");
    QFileDialog fileDialog(this, title);
    fileDialog.setFileMode(QFileDialog::AnyFile);
    fileDialog.setNameFilter(QString(tr("Blockly Files %1")).
                             arg("(*."+extension+")"));
    fileDialog.setDefaultSuffix(extension);
    if (!directory.isEmpty()) fileDialog.setDirectory(directory);
    if (!fileDialog.exec()) return; // Return if cancelled
    QStringList selectedFiles = fileDialog.selectedFiles();
    // Return if no file to open
    if (selectedFiles.count() < 1) return;
    QString xmlFileName = selectedFiles.at(0);

    // Open file
    openFileToWorkspace(xmlFileName, clear);
}

void MainWindow::openFileToWorkspace(const QString &xmlFileName, bool clear) {
    // Open file
    QFile xmlFile(xmlFileName);
    if (!xmlFile.open(QIODevice::ReadOnly)) {
        // Display error message
        QMessageBox msgBox(this);
        msgBox.setText(QString(tr("Couldn't open file to read content: %1.")
                               ).arg(xmlFileName));
        msgBox.exec();
        return;
    }
    // Read content
    QByteArray content = xmlFile.readAll();
    QString xml(content);
    xmlFile.close();
    // Set XML to Workspace
    setXml(xml, clear);
    // Set XML file name
    if (clear) {
        setXmlFileName(xmlFileName);
    }
}

void MainWindow::actionOpenMessages() {
    // Open messages
    ui->messagesWidget->show();
    ui->actionMessages->setChecked(true);
}

void MainWindow::actionQuit() {
    // Check whether source was changed
    if (checkSourceChanged() == QMessageBox::Cancel) {
        return;
    }
    // Quit
    close();
}

void MainWindow::actionUpload() {
    // Upload sketch
    arduinoExec("--upload");
}

void MainWindow::actionVerify() {
    // Build sketch
    arduinoExec("--verify");
}

void MainWindow::actionSaveAndSaveAs(bool askFileName,
                                     const QString &directory) {
    // Save XML file
    QString xmlFileName;
    QString extension="";
    if (!myblocks)
        extension=QString("bly");
    else
        extension=QString("bli");

    if (this->xmlFileName.isEmpty() || askFileName == true) {
        // Open file dialog
        QFileDialog fileDialog(this, tr("Save"));
        fileDialog.setFileMode(QFileDialog::AnyFile);
        fileDialog.setNameFilter(QString("Facilino Files %1").arg("(*."+extension+")"));
        fileDialog.setDefaultSuffix(extension);
        fileDialog.setLabelText(QFileDialog::Accept, tr("Save"));
        if (!directory.isEmpty()) fileDialog.setDirectory(directory);
        if (!fileDialog.exec()) return; // Return if cancelled
        QStringList selectedFiles = fileDialog.selectedFiles();
        // Return if no file to open
        if (selectedFiles.count() < 1) return;
        xmlFileName = selectedFiles.at(0);
    } else {
        xmlFileName = this->xmlFileName;
    }
    int result = saveXml(xmlFileName);

    if (result == 0) {
        // Display error message
        QMessageBox msgBox(this);
        msgBox.setText(QString(tr("Couldn't open file to save content: %1.")
                               ).arg(xmlFileName));
        msgBox.exec();
        return;
    }
    // Set file name
    setXmlFileName(xmlFileName);
    // Feedback
    statusBar()->showMessage(tr("Done saving."), 2000);
    // Reset status changed status
    webHelper->resetSourceChanged();
}

void MainWindow::actionSave() {
    // Save XML file
    QString directory = QStandardPaths::locate(
                QStandardPaths::DocumentsLocation,
                "",
                QStandardPaths::LocateDirectory);
    actionSaveAndSaveAs(false, directory);
}

void MainWindow::actionSaveAs() {
    // Save XML file with other name
    QString directory = QStandardPaths::locate(
                QStandardPaths::DocumentsLocation,
                "",
                QStandardPaths::LocateDirectory);
    actionSaveAndSaveAs(true, directory);
}

void MainWindow::actionSettings() {
    // Open preferences dialog
    QString htmlIndex = settings->htmlIndex();
    QString defaultLanguage = settings->defaultLanguage();
    QString license = settings->license();
    // Supported list of languages
    QStringList languageList;
    languageList << "en-GB" << "es-ES" << "ca-ES" << "gl-ES" << "eu-ES" << "de-DE" << "fr-FR" << "it-IT" << "pt-PT" << "pl-PL" << "ru-RU" << "nb-NO" <<"da-DK" << "nl-NL";
    SettingsDialog settingsDialog(settings, languageList, this);
    int result = settingsDialog.exec();
    if (result && settingsDialog.changed()) {
        // Reload blockly page
        if (htmlIndex != settings->htmlIndex()
                || defaultLanguage != settings->defaultLanguage() || license!=settings->license()) {
            xmlLoadContent = getXml();
            loadBlockly();
            ui->licenseLabel->setText(tr("Checking license..."));
            ui->license->setVisible(false);
            licenseTimer->start(1000);
            QMessageBox msg;
            msg.setText(tr("Changes successfully applied!"));
            msg.exec();
            setXml(xmlLoadContent,true);
            }
        }
}

void MainWindow::actionZoomIn() {
    // Zoom in the web view
    ui->webView->zoomIn();
}

void MainWindow::actionZoomOut() {
    // Zoom out the web view
    ui->webView->zoomOut();
}

QString MainWindow::getXml() {
    // Get XML
    QString xml = evaluateJavaScript(QString("try{Blockly.Xml.domToText(Blockly.Xml.workspaceToDom(Blockly.getMainWorkspace()));}catch(e){alert('Fire!');}"));
    return xml;
}

QString MainWindow::getCode() {
    // Get code
    QString xml = evaluateJavaScript(QString("Blockly.Arduino.workspaceToCode();"));
    return xml;
}

void MainWindow::setXml(const QString &xml, bool clear) {
    // Set XML
    QString escapedXml(escapeCharacters(xml));
    QString js;
    if (!myblocks)
    {
        js = QString("var data = '%1'; "
                              "var xml = Blockly.Xml.textToDom(data);"
                              "Blockly.Xml.domToWorkspace(Blockly.getMainWorkspace(),xml);"
                               "").arg(escapedXml);
        if (clear) {
            js.prepend("Blockly.mainWorkspace.clear();");
        }
    }
    else
    {
        js = QString(        "var xml = Blockly.Xml.textToDom('%1');"
                             "Blockly.Xml.domToWorkspace(xml, BlockFactory.mainWorkspace);"
                             "BlockFactory.mainWorkspace.clearUndo();").arg(escapedXml.remove(QRegExp("[\\n\\t\\r]")));
        if (clear) {
            js.prepend("BlockFactory.mainWorkspace.clear();");
        }
    }

    evaluateJavaScript(js);
    //documentHistoryStep=-1;
}

bool MainWindow::listIsEqual(const QStringList &listOne,
                             const QStringList &listTwo) {
    // Compare two string lists. Return true if equal.
    if (listOne.count() != listTwo.count()) return false;
    for (int i = 0; i < listOne.count(); i++) {
        if (listOne[i] != listTwo[i]) return false;
    }
    return true;
}

void MainWindow::loadBlockly() {
    // Load blockly index
    connect(ui->webView->page(),
            SIGNAL(loadFinished(bool)),
            this,
            SLOT(actionInsertLanguage()));
    connect(ui->webView->page(),
            SIGNAL(loadFinished(bool)),
            this,
            SLOT(actionLicense()));
    connect(ui->webView->page(),
            SIGNAL(loadFinished(bool)),
            this,
            SLOT(updateToolboxCategories()));
    QUrl url = QUrl::fromLocalFile(settings->htmlIndex());
    QUrlQuery query(url);
    query.addQueryItem("language",settings->defaultLanguage());
    query.addQueryItem("processor",settings->arduinoBoardFacilino());
    url.setQuery(query);
    ui->webView->load(url);
    //checkLicense();  //First call to activate the license from the beginning
    // Reset history
    sourceChanging = false;
    documentHistoryStep = -1;
}

void MainWindow::setArduinoBoard() {
    // Select combo box value according to stored value in settings
    ui->boardBox->setCurrentText(settings->arduinoBoard());
}

void MainWindow::onBoardChanged() {
    // Board changed, update settings
    settings->setArduinoBoard(ui->boardBox->currentText());
    settings->setArduinoBoardFacilino(SettingsStore::index2board[ui->boardBox->currentIndex()]);
    loadBlockly();
}

void MainWindow::onLoadFinished(bool finished) {
    // Load content using xmlLoadContent variable
    // This is triggered by settings dialog
    if (!finished || xmlLoadContent.isEmpty()) return;
    setXml(xmlLoadContent);
    ui->webView->disconnect(SIGNAL(loadFinished(bool)));
    xmlLoadContent = "";
}

void MainWindow::onProcessFinished(int exitCode) {
    ui->textBrowser->append(tr("Finished."));
}

void MainWindow::onProcessOutputUpdated() {
    ui->textBrowser->append(QString(process->readAllStandardOutput()));
}

void MainWindow::onProcessStarted() {
    ui->textBrowser->clear();
    ui->textBrowser->append(tr("Building..."));
}

void MainWindow::onStatusMessageChanged(const QString &message) {
    // This was used to display the file name when no other message was shown
}

void MainWindow::setXmlFileName(const QString &fileName) {
    // Set file name and related widgets: Status bar message and Save as menu
    this->xmlFileName = fileName;
    if (fileName.isNull() || fileName.isEmpty()) {
        // Enable save as
        ui->actionSave_as->setEnabled(false);
        // Display file name in window title
        setWindowTitle("Facilino");
    } else {
        // Enable save as
        ui->actionSave_as->setEnabled(true);
        // Display file name in window title
        setWindowTitle("Facilino - " + this->xmlFileName);
    }
}

void MainWindow::serialPortClose() {
    // Close serial connection
    ui->webView->show();
    ui->widgetConsole->hide();
    ui->consoleText->clear();

    if (serial == nullptr) return;

    serial->close();
    serial->disconnect(serial, SIGNAL(readyRead()), this, SLOT(readSerial()));
}

void MainWindow::serialPortOpen() {
    // Open serial connection
    ui->webView->hide();
    ui->widgetConsole->show();

    // No available connections, nothing to do
    if (ui->serialPortBox->currentText() == "") return;

    if (serial == nullptr) {
        // Create serial connection
        serial = new QSerialPort(this);
    } else if (serial->isOpen()) {
        serial->close();
    }

    // Set default connection parameters
    serial->setPortName(ui->serialPortBox->currentText());
    serial->setBaudRate(QSerialPort::Baud9600);
    serial->setDataBits(QSerialPort::Data8);
    serial->setParity(QSerialPort::NoParity);
    serial->setStopBits(QSerialPort::OneStop);

    // Connect
    if (serial->open(QIODevice::ReadWrite)) {
        // Execute readSerial if data is available
        connect(serial, SIGNAL(readyRead()), this, SLOT(readSerial()));
    }
}

void MainWindow::iconLabels() {
    // Show/hide icon labels
    if (settings->iconLabels() == true) {
        ui->mainToolBar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        ui->actionShow_hide_icon_labels->setChecked(true);
    } else {
        ui->mainToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
        ui->actionShow_hide_icon_labels->setChecked(false);
    }
}

bool MainWindow::isCommaSeparatedNumbers(const QString data) {
    // Is data of format number,number,number?

    if (data.indexOf(",") < 0) return false; // Nope, at least two values needed

    // Separate values
    QStringList strings = dataString.split(",");
    bool allNumbers = true;
    // Check values
    foreach (QString s, strings) {
        bool ok;
        s.toLong(&ok);
        if (ok == false) {
            // This value is not an int
            allNumbers = false;
            break;
        }
    }

    return allNumbers;
}

void MainWindow::readSerial() {
    // Read serial port data and display it in the console
    QByteArray data = serial->readAll();

    // Read serial port data and display it in the console
    for (int i = 0; i < data.size(); i++) {
        int c = data.at(i);
        if (c > 13) {
            dataString.append(data.at(i));
        } else if (c == 13) {
            bool ok;
            bool display = false;
            ui->consoleText->insertPlainText(dataString + "\n");
            int value = dataString.toInt(&ok);

            // Check if values are all numbers
            QList<long> longList;

            if (isCommaSeparatedNumbers(dataString)) {
                // More than one value
                display = true;
                QStringList strings = dataString.split(",");
                foreach (QString s, strings) {
                    long value = s.toLong();
                    longList.append(value);
                }
            } else if (ok) {
                // One value
                display = true;
                longList.append(value);
            }

            // Add new graphs if needed
            if (display) {
                int diff = longList.count() - graphList.count();
                if (diff > 0 && graphList.count() <= 10) {
                    for (int n = 0; n < diff; n++) {
                        GraphWidget *gwidget = new GraphWidget();
                        gwidget->setSizePolicy(QSizePolicy::Expanding,
                                               QSizePolicy::Expanding);
                        ui->graphLayout->addWidget(gwidget);
                        graphList.append(gwidget);
                        // No more than 10 graphics
                        if (graphList.count() == 10) break;
                    }
                }

                // Display numbers
                for (int n = 0; n < graphList.count(); n++) {
                    GraphWidget *graphWidget = graphList.at(n);
                    long value = 0;
                    if (longList.count() > n) {
                        value = longList.at(n);
                    }
                    graphWidget->append(value);
                }
            }

            // Reset string
            dataString.clear();
        }
    }

    // Move scroll to the bottom
    QScrollBar *bar = ui->consoleText->verticalScrollBar();
    bar->setValue(bar->maximum());
}

QStringList MainWindow::portList() {
    // Return list of serial ports
    QStringList serialPorts;

    // Get list
    foreach (const QSerialPortInfo &info, QSerialPortInfo::availablePorts()) {
        // Add to the list
        QString portName = info.portName();
#ifdef Q_OS_LINUX
        portName.insert(0, "/dev/");
#endif
#ifdef Q_OS_OSX
        portName.insert(0, "/dev/tty.");
#endif
        serialPorts.append(portName);
    }

    return serialPorts;
}

int MainWindow::saveXml(const QString &xmlFilePath) {
    // Save XML file

    // Get XML
    QVariant xml = getXml();
    // Save XML to file
    QFile xmlFile(xmlFilePath);
    if (!xmlFile.open(QIODevice::WriteOnly)) {
        return 0;
    }
    if (xmlFile.write(xml.toByteArray()) == -1) {
        return 0;
    }
    xmlFile.close();

    // Set file name
    if (this->xmlFileName.isEmpty()) {
        this->xmlFileName = xmlFileName;
    }

    return 1;
}

int MainWindow::saveSketch(const QString &inoFilePath) {
    // Save sketch

    // Get code
    QVariant code = getCode();

    // Save code
    QFile inoFile(inoFilePath);
    if (!inoFile.open(QIODevice::WriteOnly)) {
        return 0;
    }
    if (inoFile.write(code.toByteArray()) == -1) {
        return 0;
    }
    inoFile.close();

    return 1;
}

void MainWindow::unhide() {
    this->show();
}

void MainWindow::updateSerialPorts() {
    // Update the list of available serial ports in the combo box
    QStringList ports = portList();
    if (!listIsEqual(serialPortList, ports)) {
        QString currentPort = ui->serialPortBox->currentText();
        ui->serialPortBox->clear();
        ui->serialPortBox->insertItems(0, ports);
        if (ports.indexOf(currentPort) != -1) {
            ui->serialPortBox->setCurrentText(currentPort);
        }
        serialPortList = ports;
    }
}

void MainWindow::checkLicense() {
    if (!myblocks)
    {
        if (QString::compare(settings->license(),QString(""))!=0)
        {
            QString jsLanguage = QString("checkLicense('%1');").arg(settings->license());
            QString activeLicense= evaluateJavaScript(jsLanguage);
            if ((QString::compare(activeLicense,settings->license())==0))
            {
                ui->licenseLabel->setText(tr("License Active"));
                ui->license->setVisible(true);
            }
            else
            {
                ui->licenseLabel->setText(tr("Demo version"));
                ui->license->setVisible(false);
            }
        }
        else
        {
            ui->licenseLabel->setText(tr("Demo version"));
            ui->license->setVisible(false);
        }
        if (licenseTimer->interval()<3600000)
        {
            licenseTimer->start(3600000);
        }
    }
}

QString MainWindow::evaluateJavaScript(const QString code) {
    // Execute JavaScript code and return
    QEventLoop loop;
    QVariant returnValue = "";
    QWebEnginePage *page = ui->webView->page();
    page->runJavaScript(code, [&](const QVariant var){returnValue = var;
        loop.quit();
    });
    loop.exec();
    return returnValue.toString();
}															 
QString MainWindow::escapeCharacters(const QString& string)
{
    QString rValue = QString(string);
    // Assign \\ to backSlash
    QString backSlash = QString(QChar(0x5c)).append(QChar(0x5c));
    /* Replace \ with \\ */
    rValue = rValue.replace('\\', backSlash);
    // Assing \" to quote.
    QString quote = QString(QChar(0x5c)).append(QChar(0x22));
    // Replace " with \"
    rValue = rValue.replace('"', quote);
    return rValue;
}

void MainWindow::actionInjectWebHelper() {
    // Inject the webHelper object in the webview. This is used in index.html,
    // where a call is made back to webHelper.sourceChanged() function.
    webHelper = new JsWebHelpers();
    webHelper->setLicense(settings->license());
    channel = new QWebChannel(ui->webView->page());
    ui->webView->page()->setWebChannel(channel);
    connect(webHelper, SIGNAL(changed()), this, SLOT(onSourceChanged()));
    channel->registerObject(QString("webHelper"), webHelper);
}

int MainWindow::checkSourceChanged() {
    // Check whether source has changed
    if (webHelper->sourceChanges() > 1) {
        // Does the user want to save the changes?
        QMessageBox msgBox(this);
        msgBox.setText(QString(tr("There could be unsaved changes that could be "
                                  "lost. Do you want to save them before "
                                  "continuing?")));
        msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard |
                                  QMessageBox::Cancel);
        int result = msgBox.exec();
        if (result == QMessageBox::Save) {
            // Yes, save changes
            actionSave();
        }
        return result;
    }
    return -1;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Check whether source was changed
    if (checkSourceChanged() == QMessageBox::Cancel) {
        event->ignore();
    } else {
        // Save zoom state
        settings->setZoomScale(ui->webView->zoomFactor());
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    if (obj == ui->webView) {
        if (event->type() == QEvent::KeyPress) {
            // Backspace filter to prevent a blank page.
            // Based on this code: http://ynonperek.com/qt-event-filters.html
            //
            // Specially in Mac OS X, the backspace key generates a page back event. In
            // order to disable that action, this event filter captures the key presses
            // to capture Backspace. Then, if there is a text edit field in focus, then
            // let the event to flow, but if not, it ignores it.
            QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Backspace) {
                // Is the active element a text field?
                //QWebFrame *frame = ui->webView->page()->mainFrame();
                //QVariant code = frame->evaluateJavaScript(
                //    "document.activeElement.type");
				QVariant code = evaluateJavaScript("document.activeElement.type");
                QString type = code.toString();
                if (type == "text") {
                    // Text field: pass the event to the widget
                    return false;
                } else {
                    // No text field: ignore the event
                    return true;
                }
            } else {
                return false;
            }
        }
        // Pass the event to the widget
        return false;
    } else {
        // Pass the event on to the parent class
        return QMainWindow::eventFilter(obj, event);
    }
}

void MainWindow::on_actionMy_Blocks_triggered()
{
    QMessageBox::StandardButton reply;
    if (!this->myblocks)
    {
        reply = QMessageBox::question(this, tr("My Blocks"), tr("Are you sure you want to change to My Blocks? All changes will be lost!"),
                                QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            docDockWidget->hide();
            this->switchToMyBlocks(true);
            QUrl url = QUrl::fromLocalFile(settings->htmlIndexMyBlocks());
            ui->webView->load(url);
        }
        else
            ui->actionMy_Blocks->setChecked(false);
    }
    else
    {
        reply = QMessageBox::question(this, tr("My Blocks"), tr("Are you sure you want to exit from My Blocks? All changes will be lost!"),
                                    QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            this->switchToMyBlocks(false);
            QUrl url = QUrl::fromLocalFile(settings->htmlIndex());
            QUrlQuery query(url);
            query.addQueryItem("language",settings->defaultLanguage());
            query.addQueryItem("processor","arduino-nano");
            url.setQuery(query);
            ui->webView->load(url);
        }
        else
            ui->actionMy_Blocks->setChecked(true);

    }
}

void MainWindow::on_actionMonitor_triggered()
{
    docDockWidget->hide();
}

void MainWindow::on_actionMy_Blocks_2_triggered()
{
    this->on_actionMy_Blocks_triggered();
}

void MainWindow::mapBlock(const QString &block)
{
    if (checkSourceChanged() == QMessageBox::Cancel) {
        return;
    }
    setXml(blocksXml[block],true);
    setXmlFileName(block);
}

void MainWindow::switchToMyBlocks(bool var)
{
    ui->actionMy_Blocks->setChecked(var);
    ui->mainToolBar->setVisible(!var);
    ui->myBlocksToolBar->setVisible(var);
    ui->menu_FileMyBlocks->menuAction()->setVisible(var);
    ui->menuLibrary->menuAction()->setVisible(var);
    ui->menu_File->menuAction()->setVisible(!var);
    ui->menu_Edit->menuAction()->setVisible(!var);
    ui->menuView->menuAction()->setVisible(!var);
    ui->license->setVisible(!var);
    ui->licenseLabel->setVisible(!var);
    ui->boardBox->setEnabled(!var);
    ui->serialPortBox->setEnabled(!var);
    this->myblocks=var;
    if (var)
    {
        updateDOMLibraryFromXML();
        updateLibraryMenu();
    }
    else
    {
        licenseTimer->start(1000);
    }
    webHelper->resetSourceChanged();
}

void MainWindow::updateDOMLibraryFromXML()
{
    QDir dir;
    QFile xmlFile(dir.currentPath()+"/html/library.xml");
    if (!xmlFile.open(QIODevice::ReadOnly | QIODevice::Text) || !doc.setContent(&xmlFile))
        return;
    xmlFile.close();
}

/*void MainWindow::on_actionblockMenuSelected_triggered()
{
}*/

void MainWindow::on_actionNew_triggered()
{
}

void MainWindow::on_actionNew_4_triggered()
{
    actionNew();
}

void MainWindow::on_actionOpen_3_triggered()
{
    actionOpen();
}

void MainWindow::on_actionSave_3_triggered()
{
    actionSave();
}

void MainWindow::on_actionQuit_2_triggered()
{
    actionQuit();
}

void MainWindow::on_actionSave_as_2_triggered()
{
    actionSaveAs();
}

void MainWindow::on_actionadd_triggered()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("My Blocks"), tr("Are you sure you want to add this block to the library?"),
                            QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        QString xml = getXml();
        QDomDocument new_block_doc;
        if (new_block_doc.setContent(xml))
        {
            QDomNodeList new_block = new_block_doc.firstChild().childNodes();
            QDomNodeList items = new_block.item(0).childNodes();
            QString new_block_name;
            for (int j=0;j<items.size();j++)
            {
                QString attr_name =items.item(j).attributes().namedItem("name").nodeValue();
                if (QString::compare(attr_name,"NAME")==0)
                {
                    new_block_name = items.item(j).toElement().text();
                    break;
                }
            }
            if (blocksXml.contains(new_block_name))
            {
                QMessageBox::StandardButton reply2;
                reply2 = QMessageBox::question(this, tr("My Blocks"), tr("This block already exists in the library. Do you want to update it?"),
                                        QMessageBox::Yes|QMessageBox::No);
                if (reply2 == QMessageBox::Yes)
                {
                    updateDOMBlock(new_block_name,new_block_doc);
                    updateXMLLibraryFromDOM();
                    updateMyBlocks();
                    updateMyBlocksDoc(new_block_name);
                }
                return;
            }
            addDOMBlock(new_block_name,new_block_doc);
            updateXMLLibraryFromDOM();
            updateMyBlocks();
            updateMyBlocksDoc(new_block_name);
            updateLibraryMenu();
            setXmlFileName(new_block_name);
        }
    }
}

void MainWindow::addDOMBlock(QString new_block_name, QDomDocument new_block_doc)
{
    QDomDocument block_library_item;
    //I don't know other ways for removing the xml tag
    //QString block_str = QString("<block_library_item>%1</block_library_item>").arg(new_block_doc.toString().replace("<xml xmlns=\"http://www.w3.org/1999/xhtml\">","").replace("</xml>",""));
    QString block_str = QString("<block_library_item>%1</block_library_item>").arg(DOMNodeListToString(new_block_doc.firstChild().childNodes()));
    //ui->textBrowser->append(block_str);
    block_library_item.setContent(block_str);
    block_library_item.firstChild().toElement().setAttribute("name",new_block_name);
    //ui->textBrowser->append(block_library_item.toString());
    doc.firstChild().appendChild(block_library_item);
    //ui->textBrowser->append(doc.toString());
    blocksXml[new_block_name]=block_str;
}

QString MainWindow::DOMNodeToString(QDomNode node)
{
    QString str;
    QTextStream textStream(&str);
    node.save(textStream,QDomNode::CDATASectionNode);
    return textStream.readAll();
}

QString MainWindow::DOMNodeListToString(QDomNodeList nodeList)
{
    QString str;
    for (int i=0;i<nodeList.size();i++)
    {
        str+=DOMNodeToString(nodeList.item(i));
    }
    return str;
}

void MainWindow::updateLibraryMenu()
{
    QMap<QString,QStringList> blocks;
    blocksXml.clear();
    QDomNodeList block_library_items = doc.firstChild().childNodes();
    for (int i=0;i<block_library_items.size();i++)
    {
        QDomNode block_node = block_library_items.item(i);
        QString str = DOMNodeToString(block_node);
        QDomNode block = block_node.childNodes().item(0);  //Get the factory_base node
        QString block_name = block_node.attributes().namedItem("name").nodeValue();
        QDomNodeList items = block.childNodes();
        for (int j=0;j<items.size();j++)
        {
            QString attr_name = items.item(j).attributes().namedItem("name").nodeValue();
            if (QString::compare(attr_name,"CATEGORY")==0)
            {
                QString category_name = items.item(j).toElement().text();
                blocks[categories[category_name]].push_back(block_name);
                blocksXml[block_name].push_back(str);
                //ui->textBrowser->append(block_name+" "+escapeCharacters(textStream.readAll()));
                break;
            }
        }
    }
    //First disconnect all possible actions
    foreach(const QAction *action, ui->menuLibrary->actions())
        signalMapper->disconnect(action,SIGNAL(triggered()));
    ui->menuLibrary->clear();
    foreach (QStringList category, blocks) {
        QMenu * menu = new QMenu(blocks.key(category));
        int count=1;
        foreach (QString block, category) {
            //menu->addMenu(block);
            QAction * action = new QAction(block,menu);
            connect(action, SIGNAL(triggered()), signalMapper, SLOT(map()));
            signalMapper->setMapping(action,block);
            menu->addAction(action);
            count++;
        }
        connect(signalMapper, SIGNAL(mapped(const QString &)), this, SLOT(mapBlock()));
        ui->menuLibrary->addMenu(menu);
    }
    setXmlFileName("");
}

void MainWindow::updateXMLLibraryFromDOM()
{
    QDir dir;
    QFile xmlFile(dir.currentPath()+"/html/library.xml");
    if (!xmlFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream xmlStream(&xmlFile );
    xmlStream << doc.toString();
    xmlFile.close();
    //Now we update again the DOM object (some of the DOM nodes, when created or updated might not be QDomElements and attributes method does not properly work)
    updateDOMLibraryFromXML();
}

void MainWindow::updateDOMBlock(QString block_name, QDomDocument block_name_doc)
{
    QDomDocument block_library_item;
    //I don't know other ways for removing the xml tag
    //QString block_str = update_block_name.toString().replace("<xml xmlns=\"http://www.w3.org/1999/xhtml\">","").replace("</xml>","");
    QString block_str = QString("<block_library_item>%1</block_library_item>").arg(DOMNodeListToString(block_name_doc.firstChild().childNodes()));
    block_library_item.setContent(block_str);
    block_library_item.firstChild().toElement().setAttribute("name",block_name);
    QDomNodeList all_block_library_items = doc.firstChild().childNodes();
    QDomNode old_library_item;
    for (int i=0;i<all_block_library_items.size();i++)
    {
        if (QString::compare(all_block_library_items.item(i).attributes().namedItem("name").nodeValue(),block_name)==0)
        {
            old_library_item=all_block_library_items.item(i);
            break;
        }
    }
    doc.firstChild().replaceChild(block_library_item,old_library_item);
    blocksXml[block_name]=block_str;
}

void MainWindow::on_actionupdate_triggered()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("My Blocks"), tr("Are you sure you want to update this block from the library?"),
                            QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        QString xml = getXml();
        QDomDocument update_block_doc;
        if (update_block_doc.setContent(xml))
        {
            QDomNodeList update_block = update_block_doc.firstChild().childNodes();
            QDomNodeList items = update_block.item(0).childNodes();
            QString update_block_name;
            for (int j=0;j<items.size();j++)
            {
                QString attr_name =items.item(j).attributes().namedItem("name").nodeValue();
                if (QString::compare(attr_name,"NAME")==0)
                {
                    update_block_name = items.item(j).toElement().text();
                    break;
                }
            }
            if (!blocksXml.contains(update_block_name))
            {
                QMessageBox::StandardButton reply2;
                reply2 = QMessageBox::question(this, tr("My Blocks"), tr("This block does not exist in the library. Do you want to add it?"),
                                        QMessageBox::Yes|QMessageBox::No);
                if (reply2 == QMessageBox::Yes)
                {
                    addDOMBlock(update_block_name,update_block_doc);
                    updateXMLLibraryFromDOM();
                    updateMyBlocks();
                    updateMyBlocksDoc(update_block_name);
                    updateLibraryMenu();
                }
                return;
            }
            updateDOMBlock(update_block_name,update_block_doc);
            updateXMLLibraryFromDOM();
            updateMyBlocks();
            updateMyBlocksDoc(update_block_name);
        }
    }
}

void MainWindow::on_actiondelete_triggered()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("My Blocks"), tr("Are you sure you want to delete this block from the library?"),
                            QMessageBox::Yes|QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        QString xml = getXml();
        QDomDocument delete_block_doc;
        if (delete_block_doc.setContent(xml))
        {
            QDomNodeList delete_block = delete_block_doc.firstChild().childNodes();
            QDomNodeList items = delete_block.item(0).childNodes();
            QString delete_block_name;
            for (int j=0;j<items.size();j++)
            {
                QString attr_name =items.item(j).attributes().namedItem("name").nodeValue();
                if (QString::compare(attr_name,"NAME")==0)
                {
                    delete_block_name = items.item(j).toElement().text();
                    break;
                }
            }
            if (!blocksXml.contains(delete_block_name))
            {
                QMessageBox msg;
                msg.setText(tr("This block does not exist in the library and can't be deleted."));
                msg.exec();
                return;
            }
            deleteDOMBlock(delete_block_name);
            updateXMLLibraryFromDOM();
            updateMyBlocks();
            deleteMyBlocksDoc(delete_block_name);
        }
    }
}

void MainWindow::deleteDOMBlock(QString block_name)
{
    QDomNodeList all_block_library_items = doc.firstChild().childNodes();
    QDomNode old_library_item;
    for (int i=0;i<all_block_library_items.size();i++)
    {
        if (QString::compare(all_block_library_items.item(i).attributes().namedItem("name").nodeValue(),block_name)==0)
        {
            old_library_item=all_block_library_items.item(i);
            break;
        }
    }
    doc.firstChild().removeChild(old_library_item);
    if (blocksXml.contains(block_name))
        blocksXml.erase(blocksXml.find(block_name));
    updateXMLLibraryFromDOM();
    updateLibraryMenu();
    webHelper->resetSourceChanged();
    actionNew();
}

void MainWindow::updateMyBlocks()
{
    QDir dir;
    QFile blockFile(dir.currentPath()+"/html/my_blocks.js");
    if (blockFile.open(QIODevice::WriteOnly))
    {
        QTextStream stream(&blockFile);
        QString tools_code = "var tools = new BlockExporterTools();";
        evaluateJavaScript(tools_code);
        foreach (QString block_type, blocksXml.keys())
        {
            QString code = QString("var tools = new BlockExporterTools();"
                                   "var xml = Blockly.Xml.textToDom('%1');"
                                   "var rootBlock = tools.getRootBlockFromXml_(xml);"
                                   "FactoryUtils.getBlockDefinition('%2', rootBlock,'JavaScript', tools.hiddenWorkspace)").arg(blocksXml[block_type].remove(QRegExp("[\n\t\r]")),block_type);
            QString code1 = QString("var tempBlock = FactoryUtils.getDefinedBlock('%1', tools.hiddenWorkspace);"
                               "FactoryUtils.getGeneratorStub(tempBlock, 'Arduino')").arg(block_type);
            QString block_definiton_code = evaluateJavaScript(code);
            QString block_arduino_code = evaluateJavaScript(code1);
            stream << block_definiton_code << endl << block_arduino_code << endl;
        }
        blockFile.close();
    }
}

void MainWindow::updateMyBlocksDoc(QString block_type)
{
    QDir dir;
    QFile blockFile(dir.currentPath()+QString("/html/doc/en-GB/%1.html").arg(block_type));

    if (blockFile.open(QIODevice::WriteOnly))
    {
        QTextStream stream(&blockFile);
        QString tools_code = "var tools = new BlockExporterTools();";
        evaluateJavaScript(tools_code);
        QString code = QString("var tools = new BlockExporterTools();"
                               "var xml = Blockly.Xml.textToDom('%1');"
                               "var rootBlock = tools.getRootBlockFromXml_(xml);"
                               "FactoryUtils.getBlockDefinition('%2', rootBlock,'JavaScript', tools.hiddenWorkspace)").arg(blocksXml[block_type].remove(QRegExp("[\n\t\r]")),block_type);
        QString code1 = QString("FactoryUtils.getDoc('%1')").arg(block_type);
        QString block_definiton_code = evaluateJavaScript(code);
        QString block_doc_code = evaluateJavaScript(code1);
        stream << block_doc_code << endl;
        blockFile.close();
    }
}

void MainWindow::deleteMyBlocksDoc(QString block_type)
{
    QDir dir;
    QFile blockFile(dir.currentPath()+QString("/html/doc/en-GB/%1.html").arg(block_type));
    if (blockFile.exists())
        QFile::remove(dir.currentPath()+QString("/html/doc/en-GB/%1.html").arg(block_type));
}

void MainWindow::initCategories()
{
    categories.clear();
    categories["BLOCKS"].push_back("My Blocks");
    categories["PROCEDURES"].push_back("Functions");
    categories["CONTROL"].push_back("Control");
    categories["LOGIC"].push_back("Logic");
    categories["MATH"].push_back("Math");
    categories["VARIABLES"].push_back("Variables");
    categories["TEXT"].push_back("Text");
    categories["COMMUNICATION"].push_back("Communication");
    categories["DISTANCE"].push_back("Distance");
    categories["SCREEN"].push_back("Screen");
    categories["LIGHT"].push_back("Light");
    categories["SOUND"].push_back("SOUND");
    categories["MOVEMENT"].push_back("Movement");
    categories["AMBIENT"].push_back("Ambient");
    categories["HTML"].push_back("HTML");
    categories["ADVANCED"].push_back("Basic I/O");
}

void MainWindow::on_actionCopy_triggered()
{
    QClipboard *clipboard = QApplication::clipboard();
    QString codeString = evaluateJavaScript("Blockly.Arduino.workspaceToCode();");
    clipboard->setText(codeString);
}

void MainWindow::toogleCategories(QString data)
{
    if (!toolboxCategories.contains(data))
    {
        toolboxCategories << data;
        settings->setToolboxCategories(toolboxCategories);
    }
    else
    {
        toolboxCategories.removeOne(data);
        settings->setToolboxCategories(toolboxCategories);
    }
    toolboxCategories.removeDuplicates();
    updateToolboxCategories();
}

void MainWindow::on_actionInterrupts_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_INTERRUPTS");
}

void MainWindow::on_actionState_Machine_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_STATEMACHINE");
}

void MainWindow::on_actionArrays_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_ARRAYS");
}

void MainWindow::on_actionCurve_triggered()
{
    toogleCategories("LANG_CATEGORY_CURVE");
}

void MainWindow::on_actionButton_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_BUTTON");
}

void MainWindow::on_actionBus_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_BUS");
}

void MainWindow::on_actionOther_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_OTHER");
}

void MainWindow::on_actionLCD_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_LCD");
}

void MainWindow::on_actionLED_Matrix_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_MAX7219");
}

void MainWindow::on_actionRGB_LEDs_triggered()
{
    toogleCategories("LANG_SUBCATERGORY_WS2812");
}

void MainWindow::on_actionOLED_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_OLED");
}

void MainWindow::on_actionBluetooth_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_BLUETOOTH");
}

void MainWindow::on_actionWiFI_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_WIFI");
}

void MainWindow::on_actionIoT_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_IOT");
}

void MainWindow::on_actionBuzzer_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_BUZZER");
}

void MainWindow::on_actionVoice_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_VOICE");
}

void MainWindow::on_actionMic_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_MIC");
}

void MainWindow::on_actionMusic_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_MUSIC");
}

void MainWindow::on_actionColour_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_COLOR");
}

void MainWindow::on_actionDistance_triggered()
{
    toogleCategories("LANG_CATEGORY_DISTANCE");
}

void MainWindow::on_actionInfrared_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_INFRARED");
}

void MainWindow::on_actionMotors_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_MOTORS");
}

void MainWindow::on_actionRobot_base_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_ROBOTBASE");
}

void MainWindow::on_actionRobot_accessories_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_ROBOTACC");
}

void MainWindow::on_actionRobot_walk_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_WALK");
}

void MainWindow::on_actionController_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_SYSTEM_CONTROL");
}

void MainWindow::on_actionFiltering_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_SYSTEM_FILTER");
}

void MainWindow::on_actionTemperature_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_TEMPERATURE");
}

void MainWindow::on_actionHumidity_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_HUMIDITY");
}

void MainWindow::on_actionRain_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_RAIN");
}

void MainWindow::on_actionGas_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_GAS");
}

void MainWindow::on_actionMiscellaneous_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_MISC");
}

void MainWindow::on_actionHTML_triggered()
{
    toogleCategories("LANG_SUBCATEGORY_HTML");
}

void MainWindow::on_actionUser_Interface_triggered()
{
    toogleCategories("LANG_SUBCATERGORY_ESPUI");
}

void MainWindow::on_actionDeprecated_triggered()
{
    toogleCategories("LANG_CATEGORY_DEPRECATED");
}

void MainWindow::on_treeWidget_itemChanged(QTreeWidgetItem *item, int column)
{
    if (!ignoreQTreeWidgetItemEvents)
    {
        if (item->checkState(column))
        {
            QString data =item->data(0,Qt::ItemDataRole::UserRole).toString();
            if (!toolboxCategories.contains(data))
            {
                toolboxCategories << data;
                settings->setToolboxCategories(toolboxCategories);
            }
        }
        else
        {
            QString data =item->data(0,Qt::ItemDataRole::UserRole).toString();
            if (toolboxCategories.contains(data))
            {
                toolboxCategories.removeOne(data);
                settings->setToolboxCategories(toolboxCategories);
            }
        }
        toolboxCategories.removeDuplicates();
        updateToolboxCategories();
    }
}

void MainWindow::updateToolboxCategories()
{
    QString jsLanguage = QString("window.toolbox = ['%1','%2']; Blockly.getMainWorkspace().updateToolbox(Blockly.updateToolboxXml(getToolboxNames(window.toolbox)));").arg(SettingsStore::allCommonToolboxes.split(",").join("','")).arg(toolboxCategories.join("','"));
    ui->webView->page()->runJavaScript(jsLanguage);
}

void MainWindow::on_actionView_triggered()
{
    ui->dockWidget->show();
    updateToolboxCategories();
}

void MainWindow::addQTreeWidgetItemToParent(QTreeWidgetItem *parent, QString name, QString data)
{
    QTreeWidgetItem *item = new QTreeWidgetItem(parent);
    item->setText(0,name);
    if (toolboxCategories.contains(data))
    {
        item->setCheckState(0,Qt::CheckState::Checked);
    }
    else
    {
        item->setCheckState(0,Qt::CheckState::Unchecked);
    }
        item->setData(0,Qt::ItemDataRole::UserRole,QVariant(QString(data)));
}

void MainWindow::setToolboxCategories()
{
    ui->dockWidget->hide();
    toolboxCategories = settings->toolboxCategories();
    //toolboxCategories = SettingsStore::allAdditionalToolboxes.split(',');
    ignoreQTreeWidgetItemEvents=true;
    ui->treeWidget->setColumnCount(1);
    QTreeWidgetItem *controlItem = new QTreeWidgetItem(ui->treeWidget);
    controlItem->setText(0,tr("Control"));
    addQTreeWidgetItemToParent(controlItem,tr("Interrupts"),"LANG_SUBCATEGORY_INTERRUPTS");
    ui->actionInterrupts->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_INTERRUPTS"));
    addQTreeWidgetItemToParent(controlItem,tr("State Machine"),"LANG_SUBCATEGORY_STATEMACHINE");
    ui->actionState_Machine->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_STATEMACHINE"));
    QTreeWidgetItem *mathItem = new QTreeWidgetItem(ui->treeWidget);
    mathItem->setText(0,tr("Math"));
    addQTreeWidgetItemToParent(mathItem,tr("Arrays"),"LANG_SUBCATEGORY_ARRAYS");
    ui->actionArrays->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_ARRAYS"));
    addQTreeWidgetItemToParent(mathItem,tr("Curve"),"LANG_CATEGORY_CURVE");
    ui->actionCurve->setChecked(toolboxCategories.contains("LANG_CATEGORY_CURVE"));
    QTreeWidgetItem *variablesItem = new QTreeWidgetItem(ui->treeWidget);
    variablesItem->setText(0,tr("Variables"));
    addQTreeWidgetItemToParent(variablesItem,tr("EEPROM"),"LANG_SUBCATEGORY_EEPROM");
    ui->actionEEPROM->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_EEPROM"));
    QTreeWidgetItem *basicIOItem = new QTreeWidgetItem(ui->treeWidget);
    basicIOItem->setText(0,tr("Basic I/O"));
    addQTreeWidgetItemToParent(basicIOItem,tr("Button"),"LANG_SUBCATEGORY_BUTTON");
    ui->actionButton->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_BUTTON"));
    addQTreeWidgetItemToParent(basicIOItem,tr("Bus"),"LANG_SUBCATEGORY_BUS");
    ui->actionBus->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_BUS"));
    addQTreeWidgetItemToParent(basicIOItem,tr("Other"),"LANG_SUBCATEGORY_OTHER");
    ui->actionOther->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_OTHER"));
    QTreeWidgetItem *screenItem = new QTreeWidgetItem(ui->treeWidget);
    screenItem->setText(0,tr("Screen"));
    addQTreeWidgetItemToParent(screenItem,tr("LCD"),"LANG_SUBCATEGORY_LCD");
    ui->actionLCD->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_LCD"));
    addQTreeWidgetItemToParent(screenItem,tr("LED Matrix"),"LANG_SUBCATEGORY_MAX7219");
    ui->actionLED_Matrix->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_MAX7219"));
    addQTreeWidgetItemToParent(screenItem,tr("RGB LEDs"),"LANG_SUBCATERGORY_WS2812");
    ui->actionRGB_LEDs->setChecked(toolboxCategories.contains("LANG_SUBCATERGORY_WS2812"));
    addQTreeWidgetItemToParent(screenItem,tr("OLED"),"LANG_SUBCATEGORY_OLED");
    ui->actionOLED->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_OLED"));
    QTreeWidgetItem *communicationItem = new QTreeWidgetItem(ui->treeWidget);
    communicationItem->setText(0,tr("Communication"));
    addQTreeWidgetItemToParent(communicationItem,tr("Bluetooth"),"LANG_SUBCATEGORY_BLUETOOTH");
    ui->actionBluetooth->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_BLUETOOTH"));
    addQTreeWidgetItemToParent(communicationItem,tr("WiFi"),"LANG_SUBCATEGORY_WIFI");
    ui->actionWiFI->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_WIFI"));
    addQTreeWidgetItemToParent(communicationItem,tr("IoT"),"LANG_SUBCATEGORY_IOT");
    ui->actionIoT->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_IOT"));
    QTreeWidgetItem *soundItem = new QTreeWidgetItem(ui->treeWidget);
    soundItem->setText(0,tr("Sound"));
    addQTreeWidgetItemToParent(soundItem,tr("Buzzer"),"LANG_SUBCATEGORY_BUZZER");
    ui->actionBuzzer->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_BUZZER"));
    addQTreeWidgetItemToParent(soundItem,tr("Voice"),"LANG_SUBCATEGORY_VOICE");
    ui->actionVoice->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_VOICE"));
    addQTreeWidgetItemToParent(soundItem,tr("Mic"),"LANG_SUBCATEGORY_MIC");
    ui->actionMic->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_MIC"));
    addQTreeWidgetItemToParent(soundItem,tr("Music"),"LANG_SUBCATEGORY_MUSIC");
    ui->actionMusic->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_MUSIC"));
    QTreeWidgetItem *distanceItem = new QTreeWidgetItem(ui->treeWidget);
    distanceItem->setText(0,tr("Distance"));
    if (toolboxCategories.contains("LANG_CATEGORY_DISTANCE"))
    {
        distanceItem->setCheckState(0,Qt::CheckState::Checked);
    }
    else
    {
        distanceItem->setCheckState(0,Qt::CheckState::Unchecked);
    }
    distanceItem->setData(0,Qt::ItemDataRole::UserRole,QVariant(QString("LANG_CATEGORY_DISTANCE")));
    ui->actionDistance->setChecked(toolboxCategories.contains("LANG_CATEGORY_DISTANCE"));
    QTreeWidgetItem *lightItem = new QTreeWidgetItem(ui->treeWidget);
    lightItem->setText(0,tr("Light"));
    addQTreeWidgetItemToParent(lightItem,tr("Infrared"),"LANG_SUBCATEGORY_INFRARED");
    ui->actionInfrared->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_INFRARED"));
    addQTreeWidgetItemToParent(lightItem,tr("Colour"),"LANG_SUBCATEGORY_COLOR");
    ui->actionColour->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_COLOR"));
    QTreeWidgetItem *movementItem = new QTreeWidgetItem(ui->treeWidget);
    movementItem->setText(0,tr("Movement"));
    addQTreeWidgetItemToParent(movementItem,tr("Motors"),"LANG_SUBCATEGORY_MOTORS");
    ui->actionMotors->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_MOTORS"));
    addQTreeWidgetItemToParent(movementItem,tr("Robot base"),"LANG_SUBCATEGORY_ROBOTBASE");
    ui->actionRobot_base->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_ROBOTBASE"));
    addQTreeWidgetItemToParent(movementItem,tr("Robot accessories"),"LANG_SUBCATEGORY_ROBOTACC");
    ui->actionRobot_accessories->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_ROBOTACC"));
    addQTreeWidgetItemToParent(movementItem,tr("Robot walk"),"LANG_SUBCATEGORY_WALK");
    ui->actionRobot_walk->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_WALK"));
    QTreeWidgetItem *systemItem = new QTreeWidgetItem(ui->treeWidget);
    systemItem->setText(0,tr("System"));
    addQTreeWidgetItemToParent(systemItem,tr("Controller"),"LANG_SUBCATEGORY_SYSTEM_CONTROL");
    ui->actionController->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_SYSTEM_CONTROL"));
    addQTreeWidgetItemToParent(systemItem,tr("Filtering"),"LANG_SUBCATEGORY_SYSTEM_FILTER");
    ui->actionFiltering->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_SYSTEM_FILTER"));
    QTreeWidgetItem *environmentItem = new QTreeWidgetItem(ui->treeWidget);
    environmentItem->setText(0,tr("Environment"));
    addQTreeWidgetItemToParent(environmentItem,tr("Temperature"),"LANG_SUBCATEGORY_TEMPERATURE");
    ui->actionTemperature->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_TEMPERATURE"));
    addQTreeWidgetItemToParent(environmentItem,tr("Humidity"),"LANG_SUBCATEGORY_HUMIDITY");
    ui->actionHumidity->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_HUMIDITY"));
    addQTreeWidgetItemToParent(environmentItem,tr("Rain"),"LANG_SUBCATEGORY_RAIN");
    ui->actionRain->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_RAIN"));
    addQTreeWidgetItemToParent(environmentItem,tr("Gas"),"LANG_SUBCATEGORY_GAS");
    ui->actionGas->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_GAS"));
    addQTreeWidgetItemToParent(environmentItem,tr("Miscellaneous"),"LANG_SUBCATEGORY_MISC");
    ui->actionMiscellaneous->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_MISC"));
    QTreeWidgetItem *webInterfaceItem = new QTreeWidgetItem(ui->treeWidget);
    webInterfaceItem->setText(0,tr("Web Interface"));
    addQTreeWidgetItemToParent(webInterfaceItem,tr("HTML"),"LANG_SUBCATEGORY_HTML");
    ui->actionHTML->setChecked(toolboxCategories.contains("LANG_SUBCATEGORY_HTML"));
    addQTreeWidgetItemToParent(webInterfaceItem,tr("User Interface"),"LANG_SUBCATERGORY_ESPUI");
    ui->actionUser_Interface->setChecked(toolboxCategories.contains("LANG_SUBCATERGORY_ESPUI"));
    QTreeWidgetItem *deprecatedItem = new QTreeWidgetItem(ui->treeWidget);
    deprecatedItem->setText(0,tr("Deprecated"));
    if (toolboxCategories.contains("LANG_CATEGORY_DEPRECATED"))
    {
        deprecatedItem->setCheckState(0,Qt::CheckState::Checked);
    }
    else
    {
        deprecatedItem->setCheckState(0,Qt::CheckState::Unchecked);
    }
    deprecatedItem->setData(0,Qt::ItemDataRole::UserRole,QVariant(QString("LANG_CATEGORY_DEPRECATED")));
    ui->actionDeprecated->setChecked(toolboxCategories.contains("LANG_CATEGORY_DEPRECATED"));
    ignoreQTreeWidgetItemEvents=false;
}

void MainWindow::setSearchDocWidget()
{
    docDockWidget = new QDockWidget(tr("Search on documentation and examples"),this);
    docDockWidget->setAllowedAreas(Qt::RightDockWidgetArea);
    docDockWidget->setFixedWidth(450);
    docDockWidget->setFeatures(QDockWidget::DockWidgetClosable);
    QWidget* docMultiWidgetV = new QWidget();
    QWidget* docMultiWidgetH = new QWidget();
    docSearchButton = new QPushButton(tr("Search"),this);
    docLineEdit = new QLineEdit();
    docList = new QListWidget(docDockWidget);
    exampleList = new QListWidget(docDockWidget);
    QHBoxLayout *docLayoutH = new QHBoxLayout();
    docLayoutH->addWidget(docLineEdit);
    docLayoutH->addWidget(docSearchButton);
    docMultiWidgetH->setLayout(docLayoutH);
    QVBoxLayout *docLayoutV = new QVBoxLayout();
    docLayoutV->addWidget(docMultiWidgetH);
    docLayoutV->addWidget(new QLabel(tr("Documentation")));
    docLayoutV->addWidget(docList);
    docLayoutV->addWidget(new QLabel(tr("Examples")));
    docLayoutV->addWidget(exampleList);
    docMultiWidgetV->setLayout(docLayoutV);
    docDockWidget->hide();
    docDockWidget->setWidget(docMultiWidgetV);
    addDockWidget(Qt::RightDockWidgetArea, docDockWidget);
    connect(docSearchButton,SIGNAL(released()),this,SLOT(searchDoc()));
    connect(docList, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(onDocListItemClicked(QListWidgetItem*)));
    connect(exampleList, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(onExampleListItemClicked(QListWidgetItem*)));
}

void MainWindow::parseXML(const QDomElement& root, const QString& baseName, QStringList& v)
{
  // Extract node value, if any
  if (!baseName.isEmpty() && !root.firstChild().nodeValue().isEmpty()) { // the first child is the node text
    v.push_back(baseName + "=" + root.firstChild().nodeValue());
  }

  // Parse children elements
  for (auto element = root.firstChildElement(); !element.isNull(); element = element.nextSiblingElement()) {
    parseXML(element, baseName + "." + element.tagName(), v);
  }
}

QString MainWindow::noAcute(QString str)
{
    str=str.replace("á","a").replace("é","e").replace("í","i").replace("ó","o").replace("ú","u").replace("è","e").replace("à","a").replace("ò","o");
    return str;
}

void MainWindow::searchDoc()
{
    //SearchDoc
    QString path = settings->docPath()+settings->defaultLanguage()+"/";
    QString search_line = docLineEdit->text();
    if (search_line.length()>0)
    {
        docSearchButton->setEnabled(false);
        docSearchButton->setDisabled(true);
        docLineEdit->setEnabled(false);
        docLineEdit->setDisabled(true);
        docList->setEnabled(false);
        docList->setDisabled(true);
        exampleList->setEnabled(false);
        exampleList->setDisabled(true);
        docList->clear();
        exampleList->clear();
        search_line.remove(",");
        search_line.remove(".");
        search_line=noAcute(search_line);
        QStringList search_words = search_line.split(" ");
        QDir directoryDoc(path);
        if (!directoryDoc.exists())
        {
            path = settings->docPath()+"en-GB/";
            directoryDoc.setPath(path);
        }
        QStringList files = directoryDoc.entryList(QStringList() << "*.html" << "*.HTML",QDir::Files);
        QTextBrowser *doc = new QTextBrowser();
        QList<FileScore*> filesScored;
        filesScored.clear();
        foreach(QString filename, files) {
            doc->setSource(QUrl(QUrl::fromLocalFile(path+filename)));
            QString text = doc->toPlainText();
            text=noAcute(text);
            QString title=text.split(" \n").at(0);
            float score=0;
            foreach (QString word,search_words)
            {
                if (text.contains(" "+word,Qt::CaseSensitivity::CaseInsensitive))  //Complete words
                {
                    score++;
                }
            }
            if (score>0)
            {
                filesScored.append(new FileScore(filename,title,score));
            }
        }
        qSort(filesScored.begin(),filesScored.end(),FileScore::greaterThan);
        foreach (FileScore* file, filesScored)
        {
            QListWidgetItem* item = new QListWidgetItem(file->getTitle());
            item->setData(Qt::ItemDataRole::UserRole,QVariant(file->getFilename()));
            docList->addItem(item);
        }
        delete doc;
        path=settings->examplesPath();
        QDir directoryExamples(path);
        if (!directoryExamples.exists())
        {
            path = settings->examplesPath()+"en-GB/";
            directoryExamples.setPath(path);
        }
        QString keywords_tag=".keywords_"+settings->defaultLanguage()+"=";
        files = directoryExamples.entryList(QStringList() << "*.bly",QDir::Files);
        filesScored.clear();
        foreach(QString filename, files) {
              QStringList v;
              QDomDocument xml("xml");
              QFile file(path+filename);
              QString example_xml="";
              if (file.open(QIODevice::ReadOnly | QIODevice::Text)){
                      QTextStream stream(&file);
                      while (!stream.atEnd()){
                          example_xml+= stream.readLine();
                      }
              }
              xml.setContent(example_xml);
              parseXML(xml.documentElement(), "",v); // root has no base name, as indicated in expected output
              QString keywords;
              foreach (QString tag,v)
              {
                  if (tag.contains(keywords_tag))
                  {
                      keywords=tag.replace(keywords_tag,"",Qt::CaseSensitivity::CaseInsensitive);
                      break;
                  }
              }
              /*if (keywords.isEmpty())
              {
                  QMessageBox box;
                  box.setText(filename+" is empty!");
                  box.exec();
              }*/
              keywords=noAcute(keywords);
              float score=0;
              foreach (QString word,search_words)
              {
                  if (keywords.contains(word,Qt::CaseSensitivity::CaseInsensitive))  //Complete words
                  {
                      score++;
                  }
              }
              if (score>0)
              {
                  score+=score/(float)(keywords.split(",").length());
                  filesScored.append(new FileScore(filename,filename,score));
              }
        }
        qSort(filesScored.begin(),filesScored.end(),FileScore::greaterThan);
        foreach (FileScore* file, filesScored)
        {
            QListWidgetItem* item = new QListWidgetItem(file->getTitle());
            item->setData(Qt::ItemDataRole::UserRole,QVariant(file->getFilename()));
            exampleList->addItem(item);
        }
        docSearchButton->setEnabled(true);
        docSearchButton->setDisabled(false);
        docLineEdit->setEnabled(true);
        docLineEdit->setDisabled(false);
        docList->setEnabled(true);
        docList->setDisabled(false);
        exampleList->setEnabled(true);
        exampleList->setDisabled(false);
    }
}

void MainWindow::on_actionSearchDocumentation_triggered()
{
    if (!docDockWidget->isVisible())
    {
        docLineEdit->setText("");
        docList->clear();
        exampleList->clear();
        docDockWidget->show();
    }
}

void MainWindow::onDocListItemClicked(QListWidgetItem* item)
{
    QString jsLanguage = QString("showDoc('%1');").arg(item->data(Qt::ItemDataRole::UserRole).toString());
    ui->webView->page()->runJavaScript(jsLanguage);
}

void MainWindow::onExampleListItemClicked(QListWidgetItem* item)
{
    QString jsLanguage = QString("openExample('%1');").arg(item->data(Qt::ItemDataRole::UserRole).toString());
    ui->webView->page()->runJavaScript(jsLanguage);
}

void MainWindow::on_actionEEPROM_triggered()
{
    ui->dockWidget->show();
    updateToolboxCategories();
}
