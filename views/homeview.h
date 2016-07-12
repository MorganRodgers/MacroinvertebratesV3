#ifndef HOMEVIEW_H
#define HOMEVIEW_H

#include <QDesktopWidget>
#include <QWidget>
#include <QFile>
#include <QPixmap>
#include <QSettings>
#include <QByteArray>
#include <QBuffer>

namespace Ui {
class HomeView;
}

class HomeView : public QWidget
{
    Q_OBJECT
    QPixmap epscor_logo;

#ifndef ADD_FS_WATCHER
  void resizeEvent(QResizeEvent *event);
#endif
public:
    explicit HomeView(QWidget *parent = 0);
    ~HomeView();
    void on_backButton_pressed();
signals:
    void syncButtonClicked();
    void startButtonClicked();
    void aboutButtonClicked();
    void backButtonPressed();
private slots:
    void on_syncButton_pressed();
    void on_startButton_pressed();
    void on_aboutButton_pressed();
private:
    Ui::HomeView *ui;
};

#endif // HOMEVIEW_H
