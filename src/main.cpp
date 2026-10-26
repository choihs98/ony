#include <QApplication>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("Ony");
    app.setStyle("Fusion");
    MainWindow w;
    w.resize(1600, 900);
    w.show();
    return app.exec();
}
