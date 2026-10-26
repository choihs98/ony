#include <QApplication>
#include <QIcon>
#include <QTimer>
#include "MainWindow.h"
#include "Privacy.h"
#include "Style.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("ONY");
    app.setOrganizationName("Ony"); // 설정 저장 위치 (기존 설정 유지를 위해 이름 그대로)
#ifdef ONY_HAS_LOGO
    app.setWindowIcon(QIcon(":/ony.png"));
#endif
    app.setStyle(Style::createStyle());
    Style::installMotion();
    MainWindow w;
    w.resize(1600, 900);
    w.show();
    QTimer::singleShot(300, &w, [&w] { Privacy::askIfNeeded(&w); });
    return app.exec();
}
