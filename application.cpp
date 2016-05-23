#include "application.h"

Application::Application(int argc, char *argv[]): QApplication(argc, argv)
{
}

void Application::transitionToAllStreams()
{
    std::vector<QString> streamList;
    QMutexLocker locker(&mutex);
    streamList.reserve(streams.size());
    for(const Stream& stream: streams.values()) {
        if(!stream.title.isEmpty()) {
            streamList.push_back(stream.title);
        }
    }
    locker.unlock();

    std::sort(streamList.begin(), streamList.end());

    StreamView *view = new StreamView(streamList);
    mainWindow.setCentralWidget(view);
    connect(view, &StreamView::backButtonClicked, this, &Application::transitionToHome);
    connect(view, &StreamView::singleStreamClicked, this, &Application::transitionToSingleStream);
}

void Application::transitionToHome()
{
    HomeView *view = new HomeView();
    mainWindow.setCentralWidget(view);
    connect(view, &HomeView::aboutButtonClicked, this, &Application::transitionToAbout);
    connect(view, &HomeView::syncButtonClicked, this, &Application::transitionToSettings);
    connect(view, &HomeView::startButtonClicked, this, &Application::transitionToAllStreams);
}

void Application::transitionToAbout()
{
    AboutView *view = new AboutView();
    mainWindow.setCentralWidget(view);
    connect(view, &AboutView::backButtonClicked, this, &Application::transitionToHome);
}

void Application::transitionToSingleStream(const QString &streamName)
{
    qDebug() << streamName;
    std::vector<Invertebrate> invertebratesList;
    QMutexLocker locker(&mutex);
    Stream &stream = streams[streamName];
    invertebratesList.reserve(stream.invertebrateList.length());

    for(QString &invertebrateName: stream.invertebrateList) {
        Invertebrate invertebrate = invertebrates[invertebrateName];
        if(!invertebrate.name.isEmpty()) {
            invertebratesList.push_back(invertebrate);
        }
    }

    locker.unlock();
    std::sort(invertebratesList.begin(), invertebratesList.end());

    SingleStreamView *view = new SingleStreamView(invertebratesList, streamName);
    mainWindow.setCentralWidget(view);
    connect(view, &SingleStreamView::backButtonClicked, this, &Application::transitionToAllStreams);
    connect(view, &SingleStreamView::invertebrateDoubleClicked, this, &Application::transitionToInvertebrate);
}

void Application::transitionToInvertebrate(const QString &invertebrate, const QString &streamName)
{
    QMutexLocker locker(&mutex);
    InvertebrateView *view = new InvertebrateView(invertebrates.value(invertebrate), streamName);
    locker.unlock();

    mainWindow.setCentralWidget(view);
    connect(view, &InvertebrateView::backButtonClicked, this, &Application::transitionToSingleStream);
}

void Application::loadDataFromDisk()
{
    QDir directoryHelper;
    directoryHelper.mkpath(imagePath);
    bool needToSync = false;

    QString streamDataPath = QString("%1%2%3").arg(dataPath, directoryHelper.separator(), "stream.data");
    if(!directoryHelper.exists(streamDataPath)) {
        needToSync = true;
    } else {
        QFile dataFile(streamDataPath);
        if(!dataFile.open(QFile::ReadOnly)) {
#ifndef MOBILE_DEPLOYMENT
            qDebug() << "Unable to open local invertebrate data";
#endif
            return;
        } else {
            QDataStream loader(&dataFile);
            loader >> streams;
        }
    }

    QString invertebrateDataPath = QString("%1%2%3").arg(dataPath, directoryHelper.separator(), "invertebrate.data");
    if(!directoryHelper.exists(invertebrateDataPath) || needToSync) {
        needToSync = true;
    } else {
        QFile dataFile(invertebrateDataPath);
        if(!dataFile.open(QFile::ReadOnly)) {
#ifndef MOBILE_DEPLOYMENT
            qDebug() << "Unable to open local invertebrate data";
#endif
            return;
        }

        QDataStream loader(&dataFile);
        loader >> invertebrates;
    }

    if(needToSync) {
        startSync();
    }
}

void Application::saveDataToDisk()
{
    QDir directoryHelper;
    QString streamDataPath = QString("%1%2%3").arg(dataPath, directoryHelper.separator(), "stream.data");
    QFile streamDataFile(streamDataPath);
    if(!streamDataFile.open(QFile::WriteOnly)) {
#ifndef MOBILE_DEPLOYMENT
        qDebug() << "Unable to open local invertebrate data";
#endif
        return;
    }

    QDataStream streamSaver(&streamDataFile);
    streamSaver << streams;

    QString invertebrateDataPath = QString("%1%2%3").arg(dataPath, directoryHelper.separator(), "invertebrate.data");
    QFile invertebrateDataFile(invertebrateDataPath);
    if(!invertebrateDataFile.open(QFile::WriteOnly)) {
#ifndef MOBILE_DEPLOYMENT
        qDebug() << "Unable to open local invertebrate data";
#endif
        return;
    }

    QDataStream invertebrateSaver(&invertebrateDataFile);
    invertebrateSaver << invertebrates;
}

void Application::startSync()
{
    // Don't allow multiple syncs to start concurrently
    if(!isSyncingNow) {
        bool syncIsRequired = streams.count() == 0;

        // Let the user choose if they want to sync data on the first run/if data is empty
        if(syncIsRequired) {
            QMessageBox msgBox;
            msgBox.setText("Welcome new user!");
            msgBox.setInformativeText("Thank you for installing this app. In order for it to be useful it needs to sync data. This takes up less than 5 megabytes of space.");
            msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);

            if(msgBox.exec() == QMessageBox::Cancel) {
                return;
            }
        }

        // Start the sync process
        isSyncingNow = true;
        syncer = new WebDataSynchronizer();
        syncer->setData(&mutex, &invertebrates, &streams);

        connect(syncer, &WebDataSynchronizer::finished, [&](WebDataSynchonizerExitStatus status) {
            if(status == WebDataSynchonizerExitStatus::SUCCEEDED) {
                saveDataToDisk();
            } else {
                mainWindow.statusBar()->showMessage("Sync did not complete. Stored data has not been changed.", 10000);
            }

           stopSync();
        });

        if(!settings.isNull()) {
            connect(syncer, &WebDataSynchronizer::finished, [&](){
                settings->toggleSyncButtonText(SyncStatus::READY_TO_SYNC);
            });
        }

        connect(this, &Application::aboutToQuit, syncer, &WebDataSynchronizer::stop);
        connect(syncer, &WebDataSynchronizer::statusUpdateMessage, this, &Application::syncMessage);

        QThreadPool::globalInstance()->start(syncer);

        // only show the message about syncing running if the user clicked the sync button
        if(!syncIsRequired) {
            QMessageBox msgBox;
            msgBox.setText("Data syncing has begun!");
            msgBox.setInformativeText("Sync has begun. Items will be updated as they are completed. If you wish to stop, press cancel.");
            msgBox.setStandardButtons(QMessageBox::Ok|QMessageBox::Cancel);

            if(msgBox.exec() == QMessageBox::Cancel) {
                stopSync();
            }
        }
    } else {
        // The user requested a data sync when one was already running. They might want to quit.
        QMessageBox msgBox;
        msgBox.setText("Data is already syncing. Cancel?");
        msgBox.setStandardButtons(QMessageBox::Yes|QMessageBox::No);
        if(msgBox.exec() == QMessageBox::Yes) {
            // If we get here then syncer shouldn't be null, but it's possible so: check
            stopSync();
        }
    }
}

void Application::transitionToSettings()
{
    settings = new SettingsView(isSyncingNow);
    mainWindow.setCentralWidget(settings);
    connect(settings, &SettingsView::backButtonClicked, this, &Application::transitionToHome);
    connect(settings, &SettingsView::syncButtonClicked, this, &Application::startSync);

    if(!syncer.isNull()) {
        connect(syncer, &WebDataSynchronizer::finished, [&](){
            settings->toggleSyncButtonText(SyncStatus::READY_TO_SYNC);
        });
    }
}

Application::~Application() {
}

#ifdef ADD_FS_WATCHER

void Application::reloadStyles()
{
    QFile styles("/Users/morganrodgers/Desktop/MacroinvertebratesV3/styles/app.css");
    if(styles.open(QFile::ReadOnly)) {
        setStyleSheet("/* /");
        QString loadedStyles = styles.readAll();
        qDebug() << loadedStyles;
        setStyleSheet(loadedStyles);
    }
}

#endif

void Application::performSetUp()
{
    setOrganizationDomain("epscor.uvm.edu");
    setOrganizationName("EPSCOR");
    setApplicationName("Macroinvertebrates");

    setStyle("fusion");
    QFile file(":/styles/app.css");
    file.open(QFile::ReadOnly);
    setStyleSheet(file.readAll());
    file.close();

    HomeView *view = new HomeView();
    connect(view, &HomeView::aboutButtonClicked, this, &Application::transitionToAbout);
    connect(view, &HomeView::syncButtonClicked, this, &Application::transitionToSettings);
    connect(view, &HomeView::startButtonClicked, this, &Application::transitionToAllStreams);
    mainWindow.setCentralWidget(view);
    mainWindow.show();

    dataPath = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    imagePath = QString("%1%2%3").arg(dataPath, QDir::separator(), "images");

#ifdef ADD_FS_WATCHER
    watcher.addPath("/Users/morganrodgers/Desktop/MacroinvertebratesV3/styles/app.css");
    connect(&watcher, &QFileSystemWatcher::fileChanged, this, &Application::reloadStyles);
#endif

    QSettings settings;
    SyncOptions option = (SyncOptions)settings.value("syncingPreference").toInt();
    loadDataFromDisk();
    if(option != SyncOptions::MANUAL_ONLY) {
        if(option == SyncOptions::ON_STARTUP) {
            startSync();
        }

//        // ELSE TEST FOR WIRELESS
//        QNetworkConfigurationManager ncm;
//        auto nc = ncm.allConfigurations();

//        for (auto &x : nc) {
//            qDebug() << x.name();
//            qDebug() << x.bearerTypeName();
//            qDebug() << x.bearerTypeFamily();
//            qDebug() << x.state();
//            qDebug() << x.state().testFlag(QNetworkConfiguration::Active);
//            qDebug() << x.isValid();

//            if(x.isValid() && x.state().testFlag(QNetworkConfiguration::Active)) {
//                mainWindow->statusBar()->showMessage("We're actively connected to some kind of internets", 10000);
//                qDebug() << "Should be showing";
//                QMessageBox box;
//                box.setText(x.name() + " " + x.bearerTypeName() + " " + x.bearerTypeFamily());
//                box.exec();
//            }
//        }
    }
}

void Application::syncMessage(const QString &message)
{
    mainWindow.statusBar()->showMessage(message, 10000);
}

void Application::stopSync()
{
    if(syncer != nullptr) {
        syncer->syncingShouldContinue = false;
    }

    isSyncingNow = false;
}
