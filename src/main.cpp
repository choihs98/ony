#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QIcon>
#include <QPainter>
#include <QSettings>
#include <QSplashScreen>
#include <QTimer>
#include <QVariantAnimation>
#include "MainWindow.h"
#include "Privacy.h"
#include "Style.h"

namespace {
// 이벤트를 처리하면서 ms 동안 기다린다 (화면이 멈춰 보이지 않게)
void pump(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// 값을 from→to 로 부드럽게 바꾼다 (이징 포함)
void animate(int ms, const std::function<void(double)>& step) {
    QVariantAnimation a;
    a.setDuration(ms);
    a.setStartValue(0.0);
    a.setEndValue(1.0);
    a.setEasingCurve(QEasingCurve::InOutCubic);
    QObject::connect(&a, &QVariantAnimation::valueChanged, [&](const QVariant& v) { step(v.toDouble()); });
    QEventLoop loop;
    QObject::connect(&a, &QVariantAnimation::finished, &loop, &QEventLoop::quit);
    a.start();
    loop.exec();
}

QPixmap makeSplash() {
    QPixmap pm(640, 380);
    pm.fill(Qt::white);
    QPainter p(&pm);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
#ifdef ONY_HAS_LOGO
    const QPixmap logo(":/ony_logo.png");
    if (!logo.isNull()) {
        const QPixmap s = logo.scaledToWidth(400, Qt::SmoothTransformation);
        p.drawPixmap((pm.width() - s.width()) / 2, (pm.height() - s.height()) / 2 - 14, s);
    }
#else
    QFont f = p.font();
    f.setPixelSize(96);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0x4f, 0xc8, 0xc0));
    p.drawText(pm.rect().adjusted(0, 0, 0, -60), Qt::AlignCenter, "ONY");
#endif
    p.setPen(QColor(0xb8, 0xc4, 0xc4));
    p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    return pm;
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("ONY");
    app.setOrganizationName("Ony"); // 설정 저장 위치 (기존 설정 유지를 위해 이름 그대로)
#ifdef ONY_HAS_LOGO
    app.setWindowIcon(QIcon(":/ony.png"));
#endif
    const bool useSplash = QSettings("Ony", "Ony").value("start/splash", true).toBool() && QApplication::platformName() != "offscreen";
    QElapsedTimer clock;
    clock.start();
    QSplashScreen* splash = nullptr;
    if (useSplash) {
        splash = new QSplashScreen(makeSplash(), Qt::WindowStaysOnTopHint);
        splash->setWindowOpacity(0.0);
        splash->show();
        animate(260, [&](double t) { splash->setWindowOpacity(t); });   // 로고가 스르륵 나타난다
        splash->showMessage(QStringLiteral("불러오는 중..."), Qt::AlignBottom | Qt::AlignHCenter, QColor(0x7a, 0x8a, 0x8a));
        QApplication::processEvents();
    }

    app.setStyle(Style::createStyle());
    Style::installMotion();
    MainWindow w;
    w.resize(1600, 900);

    if (splash) {
        // 로고가 너무 빨리 사라지지 않게 최소 시간을 보장한 뒤, 로고가 사라지면서 화면이 켜진다
        const int left = 1100 - int(clock.elapsed());
        if (left > 0) pump(left);
        w.setWindowOpacity(0.0);
        w.show();
        QApplication::processEvents();
        animate(380, [&](double t) {
            splash->setWindowOpacity(1.0 - t);
            w.setWindowOpacity(t);
        });
        w.setWindowOpacity(1.0);
        splash->finish(&w);
        delete splash;
    } else {
        w.show();
    }

    QTimer::singleShot(300, &w, [&w] {
        Privacy::askIfNeeded(&w);
        if (QSettings("Ony", "Ony").value("start/openLast", false).toBool()) w.openLastProject();
    });
    return app.exec();
}
