// 데모: 실제 입력/스크립트로 장면을 만들고 화면을 캡처한다.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QtTest/QtTest>
#include <cmath>

#include "CanvasWidget.h"
#include "Document.h"
#include "Extras.h"
#include "MainWindow.h"
#include "Style.h"

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setStyle(Style::createStyle());
    Style::installMotion();
    const QString out = argc > 1 ? argv[1] : "/tmp/demo";
    QDir().mkpath(out);

    MainWindow w;
    w.resize(1600, 900);
    w.show();
    QTest::qWait(300);
    Document* d = w.document();
    CanvasWidget* c = w.canvas();
    d->newProject(QSize(640, 360), 12, 12);
    c->zoomFit();

    ScriptEngine eng(d);
    // 배경 레이어
    d->layer(0).name = "배경";
    d->notifyStructure();
    eng.run("frame 1\nrect 0 0 640 220 #9bd4ff\nrect 0 220 640 140 #6cc26c\nellipse 480 30 90 90 #ffe066");
    // 공 레이어 (프레임마다 새 이미지: 바운스)
    eng.run("layer.new 공");
    for (int f = 0; f < 12; ++f) {
        const double t = f / 11.0;
        const double x = 40 + t * 520;
        const double y = 230 - std::abs(std::sin(t * M_PI * 2.0)) * 170;
        const double squash = (y > 215) ? 14 : 0;
        eng.run(QString("frame %1\nkey.new\nellipse %2 %3 %4 %5 #e53935").arg(f + 1).arg(x).arg(y).arg(60 + squash).arg(60 - squash));
    }
    // 선화 레이어: 실제 마우스 입력으로 그림
    eng.run("layer.new 선화\nframe 1");
    d->setCurrentFrame(0);
    c->setTool(CanvasWidget::Tool::Pen);
    c->setColorA(QColor("#222222"));
    BrushParams bp; bp.size = 5; c->setBrush(bp);
    c->zoomActual();
    QTest::qWait(100);
    auto W = [&](QPoint p) { return QPoint((c->width() - d->size().width()) / 2 + p.x(), (c->height() - d->size().height()) / 2 + p.y()); };
    QTest::mousePress(c, Qt::LeftButton, {}, W({60, 60}));
    for (int i = 0; i <= 40; ++i) {
        const double a = i / 40.0 * 6.2832;
        QTest::mouseMove(c, W({int(60 + 40 * std::cos(a) + 14 * std::sin(a * 3)), int(60 + 40 * std::sin(a))}));
    }
    QTest::mouseRelease(c, Qt::LeftButton, {}, W({100, 60}));
    c->zoomFit();
    d->setCurrentFrame(4);
    QTest::qWait(200);
    w.grab().save(out + "/01_main.png");

    // 라이트 테이블 + 공 레이어 선택
    d->setCurrentLayer(1);
    c->setLightTable(true);
    d->setCurrentFrame(5);
    QTest::qWait(200);
    w.grab().save(out + "/02_lighttable.png");
    c->setLightTable(false);

    // 룸 전환
    QComboBox* rooms = nullptr;
    for (auto* cb : w.findChildren<QComboBox*>())
        if (cb->findText("스토리보드") >= 0) rooms = cb;
    if (rooms) {
        rooms->setCurrentIndex(1);
        QTest::qWait(250);
        w.grab().save(out + "/03_animation_room.png");
        rooms->setCurrentIndex(3);
        QTest::qWait(250);
        w.grab().save(out + "/04_storyboard_room.png");
        rooms->setCurrentIndex(0);
        QTest::qWait(200);
    }

    // 재생 프레임 캡처 (GIF 용)
    d->setCurrentLayer(2);
    for (int f = 0; f < 12; ++f) {
        d->setCurrentFrame(f);
        QTest::qWait(60);
        w.grab().scaled(960, 540, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(QString("%1/play_%2.png").arg(out).arg(f, 2, 10, QChar('0')));
    }
    d->save(out + "/demo.ony");
    return 0;
}
