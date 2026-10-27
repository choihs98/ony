#include <QApplication>
#include <QElapsedTimer>
#include <QPainter>
#include <QPen>
#include <QTest>
#include <QDebug>
#include "MainWindow.h"
#include "CanvasWidget.h"
#include "Document.h"
#include "Style.h"
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setStyle(Style::createStyle());
    Style::installMotion();
    QElapsedTimer t;
    t.start();
    MainWindow w;
    w.resize(1600, 900);
    w.show();
    QTest::qWait(50);
    qInfo() << "startup ms" << t.elapsed();
    Document* d = w.document();
    const int L = 8;
    d->newProject(QSize(1920, 1080), 48, 24);
    for (int l = 0; l < L; ++l) {
        if (l) d->addLayer("L");
        QImage im(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        im.fill(Qt::transparent);
        QPainter p(&im);
        p.setPen(QPen(QColor(20, 20, 30, 200), 6));
        for (int i = 0; i < 40; ++i) p.drawLine(i * 40, 0, 1920 - i * 30, 1080);
        p.end();
        for (int f = 0; f < 48; f += 2) d->pushKeyEdit(l, f, QImage(), im, "x");
    }
    t.restart();
    for (int i = 0; i < 20; ++i) d->composite(i, true, false);
    qInfo() << "composite(8층) ms each" << t.elapsed() / 20.0;
    for (int l = 0; l < L; ++l) d->layer(l).blend = 11;
    t.restart();
    for (int i = 0; i < 10; ++i) d->composite(i, true, false);
    qInfo() << "composite(블렌딩) ms each" << t.elapsed() / 10.0;
    for (int l = 0; l < L; ++l) d->layer(l).blend = 0;
    d->addFx(0, {Effects::Type::GaussBlur, 6});
    t.restart();
    for (int i = 0; i < 10; ++i) d->composite(i, true, false);
    qInfo() << "composite(FX) ms each" << t.elapsed() / 10.0;
    d->removeFx(0, 0);
    t.restart();
    for (int i = 0; i < 20; ++i) w.canvas()->grab();
    qInfo() << "canvas grab ms each" << t.elapsed() / 20.0;
    w.canvas()->setLightTable(true);
    t.restart();
    for (int i = 0; i < 10; ++i) { d->setCurrentFrame(10 + i); w.canvas()->grab(); }
    qInfo() << "canvas+라이트테이블 ms each" << t.elapsed() / 10.0;
    w.canvas()->setLightTable(false);
    t.restart();
    for (int i = 0; i < 10; ++i) w.grab();
    qInfo() << "window grab ms each" << t.elapsed() / 10.0;
    CanvasWidget* c = w.canvas();
    c->setTool(CanvasWidget::Tool::Pen);
    BrushParams bp; bp.size = 12; c->setBrush(bp);
    t.restart();
    QTest::mousePress(c, Qt::LeftButton, {}, QPoint(300, 300));
    for (int i = 0; i < 60; ++i) QTest::mouseMove(c, QPoint(300 + i * 8, 300 + (i % 7) * 9));
    QTest::mouseRelease(c, Qt::LeftButton, {}, QPoint(800, 300));
    qInfo() << "60-move stroke ms total" << t.elapsed();
    t.restart();
    for (int f = 0; f < 24; ++f) d->renderFrame(f, Qt::white, false);
    qInfo() << "renderFrame ms each" << t.elapsed() / 24.0;
    return 0;
}
