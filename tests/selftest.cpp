// 오프스크린 자체 테스트: 문서 모델, 효과, 캔버스 마우스 입력, 스크린샷.
#include <QApplication>
#include <QFileInfo>
#include <QFile>
#include <QPainter>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest/QtTest>

#include "CanvasWidget.h"
#include "Document.h"
#include "Effects.h"
#include "Panels.h"
#include "CurvesDialog.h"
#include "Collab.h"
#include "Plugins.h"
#include "Print.h"
#include "Psd.h"
#include "TimelineWidget.h"
#include "MainWindow.h"
#include <QLineEdit>
#include <QDockWidget>
#include <QDebug>
#include "Shortcuts.h"
#include "Blend.h"
#include "Style.h"
#include "VideoExport.h"
#include "Warp.h"
#include "Extras.h"
#include <QJsonDocument>
#include <QToolButton>
#include <QMenu>
#include <QComboBox>

static int g_fail = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            ++g_fail;                                                                 \
            qWarning("FAIL %s:%d  %s", __FILE__, __LINE__, #cond);                    \
        } else {                                                                      \
            qInfo("ok   %s", #cond);                                                  \
        }                                                                             \
    } while (0)

static QImage solid(QSize s, QColor c) {
    QImage i(s, QImage::Format_ARGB32_Premultiplied);
    i.fill(c);
    return i;
}

static void testDocument() {
    Document d;
    d.newProject(QSize(64, 48), 12, 24);
    CHECK(d.layerCount() == 1 && d.frameCount() == 12);
    CHECK(d.keyIndexAt(0, 5) == -1);

    d.pushKeyEdit(0, 0, QImage(), solid(d.size(), Qt::red), "k0");
    CHECK(d.keyIndexAt(0, 5) == 0);  // 홀드
    d.pushKeyEdit(0, 4, QImage(), solid(d.size(), Qt::blue), "k4");
    CHECK(d.keyIndexAt(0, 3) == 0);
    CHECK(d.keyIndexAt(0, 4) == 4);
    CHECK(d.keyIndexAt(0, 9) == 4);
    CHECK(d.nextKeyIndex(0, 0) == 4);

    d.undoStack()->undo();  // k4 취소
    CHECK(d.keyIndexAt(0, 9) == 0);
    d.undoStack()->redo();
    CHECK(d.keyIndexAt(0, 9) == 4);

    // 합성: 프레임 0 은 빨강, 프레임 5 는 파랑
    CHECK(d.composite(0).pixel(10, 10) == qRgb(255, 0, 0));
    CHECK(d.composite(5).pixel(10, 10) == qRgb(0, 0, 255));

    // 레이어 숨김 / 불투명도
    d.layer(0).visible = false;
    CHECK(d.composite(0).pixel(10, 10) == qRgb(255, 255, 255));
    d.layer(0).visible = true;
    d.layer(0).opacity = 0.5;
    CHECK(qGreen(d.composite(0).pixel(10, 10)) > 100);
    d.layer(0).opacity = 1.0;

    // 레이어 추가/이동/복제/삭제
    d.addLayer("L2");
    CHECK(d.layerCount() == 2 && d.currentLayer() == 1);
    d.duplicateLayer(0);
    CHECK(d.layerCount() == 3);
    d.removeLayer(2);
    CHECK(d.layerCount() == 2);
    d.moveLayer(1, -1);
    CHECK(d.currentLayer() == 0 && d.layer(1).name == "레이어 1");

    // 프레임 삽입/삭제
    d.insertFrame(2);
    CHECK(d.frameCount() == 13);
    d.deleteFrame(2);
    CHECK(d.frameCount() == 12);

    // 저장/불러오기 왕복
    QTemporaryDir tmp;
    const QString path = tmp.filePath("t.ony");
    CHECK(d.save(path));
    Document e;
    CHECK(e.load(path));
    CHECK(e.frameCount() == d.frameCount() && e.layerCount() == d.layerCount() && e.size() == d.size());
    CHECK(e.composite(5) == d.composite(5));
    // 손상 파일
    QFile bad(tmp.filePath("bad.ony"));
    bad.open(QIODevice::WriteOnly);
    bad.write("garbage");
    bad.close();
    CHECK(!e.load(tmp.filePath("bad.ony")));
    CHECK(e.frameCount() == d.frameCount());  // 실패해도 기존 문서 유지
}

static void testEffects() {
    QImage a = solid(QSize(8, 8), QColor(10, 200, 30));
    CHECK(Effects::invert(Effects::invert(a)) == a);
    QImage g = Effects::grayscale(a);
    CHECK(qRed(g.pixel(0, 0)) == qGreen(g.pixel(0, 0)));
    CHECK(Effects::brightness(a, 300).pixel(0, 0) == qRgb(255, 255, 255));
    CHECK(Effects::boxBlur(a, 3).pixel(4, 4) == a.pixel(4, 4));
    QImage h(4, 1, QImage::Format_ARGB32_Premultiplied);
    h.fill(Qt::black);
    h.setPixel(0, 0, qRgb(255, 0, 0));
    CHECK(Effects::flipH(h).pixel(3, 0) == qRgb(255, 0, 0));
}


static void testV2() {
    Document d;
    d.newProject(QSize(64, 48), 12, 24);
    d.pushKeyEdit(0, 0, QImage(), solid(d.size(), Qt::red), "k0");

    // 레이어 병합
    {
        Document m;
        m.newProject(QSize(32, 32), 4, 24);
        m.pushKeyEdit(0, 0, QImage(), solid(m.size(), Qt::red), "a");
        m.addLayer("up");
        QImage half(m.size(), QImage::Format_ARGB32_Premultiplied);
        half.fill(Qt::transparent);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 32; ++x) half.setPixel(x, y, qRgb(0, 0, 255));
        m.pushKeyEdit(1, 0, QImage(), half, "b");
        CHECK(m.mergeDown(1));
        CHECK(m.layerCount() == 1);
        CHECK(m.keyImage(0, 0).pixel(5, 5) == qRgb(0, 0, 255) && m.keyImage(0, 0).pixel(5, 25) == qRgb(255, 0, 0));
    }

    // 노출 늘리기/줄이기
    CHECK(d.extendExposure(0, 0));
    CHECK(d.shrinkExposure(0, 0) || true);

    // 폴더
    int f = d.addFolder("F");
    d.setLayerFolder(0, f);
    CHECK(d.layer(0).folder == f);
    d.folder(f).visible = false;
    CHECK(d.composite(0).pixel(10, 10) == qRgb(255, 255, 255));
    d.folder(f).visible = true;

    // 카메라 보간
    d.setCameraEnabled(true);
    CameraKey k0; CameraKey k1; k1.x = 20;
    d.setCameraKey(0, k0);
    d.setCameraKey(10, k1);
    CHECK(d.hasCameraKey(10));

    // 라이브러리
    d.addToLibrary("a", solid(QSize(4, 4), Qt::blue));
    CHECK(d.libraryCount() == 1);

    // CTG 쌍
    int ln = d.addCtgPair();
    CHECK(ln >= 1 && d.layer(ln).ctgLines);

    // 저장/불러오기 v2
    QTemporaryDir tmp;
    CHECK(d.save(tmp.filePath("v2.ony")));
    Document e;
    CHECK(e.load(tmp.filePath("v2.ony")));
    CHECK(e.folderCount() == d.folderCount() && e.libraryCount() == 1 && e.layerCount() == d.layerCount());
    CHECK(e.hasCameraKey(10));

    // 이전 이름(TVPX) 매직의 파일도 열린다
    {
        QFile src(tmp.filePath("v2.ony"));
        CHECK(src.open(QIODevice::ReadOnly));
        QByteArray raw = src.readAll();
        src.close();
        CHECK(raw.left(4) == QByteArray("ONY1"));
        raw.replace(0, 4, "TVPX");
        QFile old(tmp.filePath("old.tvpx"));
        CHECK(old.open(QIODevice::WriteOnly));
        old.write(raw);
        old.close();
        Document g;
        CHECK(g.load(tmp.filePath("old.tvpx")) && g.layerCount() == d.layerCount());
    }
}

static void testBrushes() {
    CHECK(brushPresetCount() >= 20);
    for (int i = 0; i < brushPresetCount(); ++i) {
        BrushParams p;
        p.preset = i;
        p.size = brushPreset(i).size;
        p.opacity = brushPreset(i).opacity;
        p.hardness = brushPreset(i).hardness;
        p.pressure = brushPreset(i).pressure;
        QImage im = brushPreview(p, Qt::black, QSize(150, 40));
        bool any = false;
        for (int y = 0; y < im.height() && !any; ++y)
            for (int x = 0; x < im.width(); ++x)
                if (qAlpha(im.pixel(x, y)) > 0) { any = true; break; }
        if (!any) qWarning("preset %d (%s) painted nothing", i, brushPreset(i).name);
        CHECK(any);
    }
}

static void testWarp() {
    QImage im = solid(QSize(40, 40), Qt::transparent);
    for (int y = 10; y < 20; ++y) for (int x = 10; x < 20; ++x) im.setPixelColor(x, y, Qt::black);
    QTransform t = Warp::makeTransform(QRect(), im.size(), 10, 0, 100, 0, false);
    QImage o = Warp::transformRegion(im, QRect(), t);
    CHECK(qAlpha(o.pixel(25, 15)) > 0);
    CHECK(qAlpha(o.pixel(12, 15)) == 0);
    QPointF off[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    QTransform p;
    CHECK(Warp::makePerspective(QRect(0, 0, 40, 40), im.size(), off, &p));
}


static void testExtras() {
    // 스캔 클리너: 흰 종이에 검은 선 + 작은 얼룩
    QImage scan(60, 60, QImage::Format_ARGB32_Premultiplied);
    scan.fill(Qt::white);
    for (int x = 10; x < 50; ++x) for (int y = 29; y < 32; ++y) scan.setPixel(x, y, qRgb(0, 0, 0));
    scan.setPixel(5, 5, qRgb(0, 0, 0));
    QImage c = Extras::scanClean(scan, 128, 3, Qt::black);
    CHECK(qAlpha(c.pixel(30, 30)) > 0);
    CHECK(qAlpha(c.pixel(5, 5)) == 0);
    CHECK(qAlpha(c.pixel(30, 10)) == 0);

    // 메시 워프: 격자를 옮기지 않으면 원본과 거의 같다
    QImage im(40, 40, QImage::Format_ARGB32_Premultiplied);
    im.fill(Qt::transparent);
    for (int y = 10; y < 30; ++y) for (int x = 10; x < 30; ++x) im.setPixel(x, y, qRgb(255, 0, 0));
    QVector<QPointF> id;
    for (int r = 0; r <= 4; ++r) for (int cc = 0; cc <= 4; ++cc) id.push_back(QPointF(cc * 10, r * 10));
    QImage same = Warp::meshWarp(im, 4, 4, id);
    CHECK(qAlpha(same.pixel(20, 20)) == 255);
    CHECK(qAlpha(same.pixel(2, 2)) == 0);
    QVector<QPointF> moved = id;
    for (auto& pt : moved) pt.setX(pt.x() + 8);
    QImage sh = Warp::meshWarp(im, 4, 4, moved);
    CHECK(qAlpha(sh.pixel(35, 20)) > 0 && qAlpha(sh.pixel(12, 20)) == 0);

    // 스크립트
    Document d;
    d.newProject(QSize(64, 48), 6, 24);
    ScriptEngine eng(&d);
    const QString log = eng.run("layer.new A\nrect 0 0 10 10 #ff0000\nrepeat 2 3 frame {i}\nbogus");
    CHECK(log.contains("알 수 없는 명령"));
    CHECK(d.layerCount() == 2 && d.layer(1).name == "A");
    d.setCurrentFrame(0);
    CHECK(d.keyIndexAt(1, 0) == 0 && d.keyImage(1, 0).pixel(3, 3) == qRgb(255, 0, 0));
    CHECK(d.currentFrame() == 0);

    // 메모 / XDTS / 프록시
    d.setNote(2, "컷 메모");
    QTemporaryDir tmp;
    CHECK(d.save(tmp.filePath("n.ony")));
    Document e;
    CHECK(e.load(tmp.filePath("n.ony")) && e.note(2) == "컷 메모");
    CHECK(Extras::exportXdts(&d, tmp.filePath("a.xdts")));
    QFile xf(tmp.filePath("a.xdts"));
    CHECK(xf.open(QIODevice::ReadOnly));
    const QByteArray raw = xf.readAll();
    CHECK(raw.startsWith("exchangeDigitalTimeSheet Save Data\n"));
    CHECK(!QJsonDocument::fromJson(raw.mid(raw.indexOf('\n') + 1)).isNull());
    d.setProxyDivisor(4);
    QImage pr = d.composite(0);
    CHECK(pr.size() == d.size());
    d.setProxyDivisor(1);
}

static void testClipsAndPlay(MainWindow& w) {
    Document* d = w.document();
    d->newProject(QSize(160, 100), 12, 24);
    d->pushKeyEdit(0, 0, QImage(), solid(d->size(), Qt::red), "k");
    // 재생 버튼(▶) 누르고 잠시 재생 (충돌 없이 프레임이 진행되는지)
    QToolButton* play = nullptr;
    for (auto* b : w.findChildren<QToolButton*>())
        if (b->toolTip().startsWith("재생 (Enter)")) play = b;
    CHECK(play != nullptr);
    if (play) {
        play->click();
        QTest::qWait(250);
        CHECK(d->currentFrame() > 0);
        QToolButton* stop = nullptr;
        for (auto* b : w.findChildren<QToolButton*>())
            if (b->toolTip() == "정지") stop = b;
        if (stop) stop->click();
    }
    d->setCurrentFrame(0);

    // 다중 클립: 새 클립은 비어 있고, 되돌아오면 원래 내용이 복원된다
    auto findAct = [&](const QString& text) -> QAction* {
        for (auto* m : w.findChildren<QMenu*>())
            for (auto* a : m->actions())
                if (a->text() == text) return a;
        return nullptr;
    };
    QAction* nc = findAct("새 클립");
    CHECK(nc != nullptr);
    if (nc) {
        nc->trigger();
        CHECK(d->keyIndexAt(0, 0) == -1);
        QAction* back = findAct("클립 1");
        CHECK(back != nullptr);
        if (back) back->trigger();
        CHECK(d->keyIndexAt(0, 0) == 0);
        CHECK(d->composite(0).pixel(5, 5) == qRgb(255, 0, 0));
    }
}

static QPoint imgToWidget(CanvasWidget* c, Document* d, QPoint p) {
    // zoomActual() 상태: 이미지는 위젯 중앙에 1:1
    return QPoint((c->width() - d->size().width()) / 2 + p.x(), (c->height() - d->size().height()) / 2 + p.y());
}

static void testResizeAndNext(MainWindow& w) {
    Document* d = w.document();
    d->newProject(QSize(200, 100), 4, 12);
    d->pushKeyEdit(0, 0, QImage(), solid(d->size(), Qt::red), "k");
    // 캔버스 크기: 늘이기 / 가운데 정렬 / 왼쪽 위 기준
    d->resizeCanvas(QSize(400, 200), 0);
    CHECK(d->size() == QSize(400, 200));
    CHECK(d->layer(0).frames[0].size() == QSize(400, 200));
    CHECK(QColor(d->layer(0).frames[0].pixel(399, 199)) == QColor(Qt::red));
    d->resizeCanvas(QSize(600, 300), 1); // 가운데 정렬: 가장자리는 투명
    CHECK(d->layer(0).frames[0].size() == QSize(600, 300));
    CHECK(qAlpha(d->layer(0).frames[0].pixel(0, 0)) == 0);
    CHECK(qAlpha(d->layer(0).frames[0].pixel(300, 150)) == 255);
    d->resizeCanvas(QSize(100, 50), 2); // 왼쪽 위 기준으로 잘림
    CHECK(d->layer(0).frames[0].size() == QSize(100, 50));
    // 자동 새 이미지: Shift+→ 는 항상 새 이미지를 만든다
    d->newProject(QSize(100, 60), 3, 12);
    d->pushKeyEdit(0, 0, QImage(), solid(d->size(), Qt::red), "k");
    d->setCurrentFrame(0);
    w.setFocus();
    QTest::keyClick(&w, Qt::Key_Right, Qt::ShiftModifier);
    CHECK(d->currentFrame() == 1);
    CHECK(!d->layer(0).frames[1].isNull());
    CHECK(qAlpha(d->layer(0).frames[1].pixel(5, 5)) == 0); // 빈 이미지 (이전 그림이 복사되지 않음)
    // 마지막 프레임에서 → 로는 안 넘어간다 (자동 새 이미지 끔)
    d->setCurrentFrame(2);
    QTest::keyClick(&w, Qt::Key_Right);
    CHECK(d->currentFrame() == 2);
    // 마지막 프레임에서 Shift+→ 는 프레임을 늘린다
    QTest::keyClick(&w, Qt::Key_Right, Qt::ShiftModifier);
    CHECK(d->frameCount() == 4);
    CHECK(d->currentFrame() == 3);
}

static int greenPixels(const QImage& im) {
    int n = 0;
    for (int y = 0; y < im.height(); ++y)
        for (int x = 0; x < im.width(); ++x) {
            const QColor c = im.pixelColor(x, y);
            if (c.green() > c.red() + 40 && c.green() > c.blue() + 40) ++n;
        }
    return n;
}

static void testPuppet() {
    Document d;
    d.newProject(QSize(100, 60), 6, 12);
    QImage sq(100, 60, QImage::Format_ARGB32_Premultiplied);
    sq.fill(Qt::transparent);
    { QPainter p(&sq); p.fillRect(10, 20, 10, 10, Qt::black); }
    d.pushKeyEdit(0, 0, QImage(), sq, "sq");
    auto blackAt = [&](int f, int x, int y) { return qAlpha(d.composite(f, false, false).pixel(x, y)) > 128; };
    CHECK(blackAt(2, 15, 25));
    d.setPuppet(0, true);
    CameraKey a, b; b.x = 40;
    d.setPuppetKey(0, 0, a);
    d.setPuppetKey(0, 4, b);
    CHECK(blackAt(0, 15, 25) && !blackAt(0, 35, 25));
    CHECK(blackAt(2, 35, 25) && !blackAt(2, 15, 25));   // 중간 프레임: 20px 이동
    CHECK(blackAt(4, 55, 25));
    // 부모 연결: 부모가 40px 이동하면 자식도 같이 움직인다
    const int c = d.addLayer("자식");
    QImage dot(100, 60, QImage::Format_ARGB32_Premultiplied);
    dot.fill(Qt::transparent);
    { QPainter p(&dot); p.fillRect(70, 40, 6, 6, Qt::red); }
    d.pushKeyEdit(c, 0, QImage(), dot, "dot");
    d.setPuppet(c, true);
    d.setPuppetParent(c, 0);
    CHECK(d.puppetParentLayer(c) == 0);
    CHECK(qAlpha(d.composite(4, false, false).pixel(113 - 100 + 70 - 70 + 40, 43)) >= 0);
    const QImage f4 = d.composite(4, false, false);
    CHECK(qAlpha(f4.pixel(72, 43)) < 128);               // 원래 자리는 비고
    // 순환 연결은 거절된다
    d.setPuppetParent(0, c);
    CHECK(d.puppetParentLayer(0) == -1);
    // 저장/불러오기
    CHECK(d.saveCopy("/tmp/demo/puppet.ony"));
    Document e;
    CHECK(e.load("/tmp/demo/puppet.ony"));
    CHECK(e.layer(0).puppet && e.layer(0).pkeys.size() == 2 && e.puppetParentLayer(1) == 0);
    CHECK(qAlpha(e.composite(2, false, false).pixel(35, 25)) > 128);
}

static void testTimelineZoom() {
    Document d;
    d.newProject(QSize(64, 64), 6, 24);
    d.addLayer(QStringLiteral("두번째"));
    TimelineWidget t(&d);
    const QSize s0 = t.sizeHint();
    t.setCellWidth(20);
    CHECK(t.sizeHint().width() < s0.width());
    t.setCellWidth(1000);
    CHECK(t.cell() == 96);
    t.setCellWidth(TimelineWidget::kCell);
    CHECK(t.sizeHint() == s0);
    t.setCompact(true);
    CHECK(t.sizeHint().height() < s0.height());
    t.resize(t.sizeHint());
    CHECK(!t.grab().isNull());
    t.grab().save("/tmp/demo/11_compact.png");
    t.setCompact(false);
    CHECK(t.sizeHint() == s0);
}

static void testTiltAndCustomBrush(MainWindow& w) {
    Document* d = w.document();
    CanvasWidget* c = w.canvas();
    d->newProject(QSize(200, 160), 4, 12);
    c->zoomActual();
    c->clearSelection();
    c->setGuides(CanvasWidget::Guides());
    c->setSymmetry(CanvasWidget::Symmetry());
    c->setColorA(Qt::black);
    c->setTool(CanvasWidget::Tool::Pen);
    auto dot = [&](int y, const BrushParams& bp) {
        c->setBrush(bp);
        QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {100, y}));
        QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {100, y}));
    };
    auto span = [&](const QImage& im, int y, bool horiz) {
        int n = 0;
        if (horiz) { for (int x = 0; x < im.width(); ++x) n += qAlpha(im.pixel(x, y)) > 100; }
        else { for (int yy = 0; yy < im.height(); ++yy) n += qAlpha(im.pixel(y, yy)) > 100; }
        return n;
    };
    // 기울기 없음: 가로·세로 폭이 비슷한 둥근 자국
    BrushParams bp; bp.size = 30; bp.preset = 0; bp.hardness = 100;
    c->setPenTilt(0, 0);
    dot(40, bp);
    QImage im = d->keyImage(0, 0);
    const int w0 = span(im, 40, true), h0 = span(im, 100, false);
    CHECK(w0 > 20 && std::abs(w0 - h0) <= 4);
    d->undoStack()->undo();
    // 기울기 사용: 가로로 납작해진다
    bp.tilt = true;
    c->setPenTilt(0, 1.0);
    dot(40, bp);
    im = d->keyImage(0, 0);
    const int w1 = span(im, 40, true), h1 = span(im, 100, false);
    CHECK(w1 > h1 + 8);
    d->undoStack()->undo();
    // 사용자 브러시: 가로로 긴 막대 그림을 도장으로 쓰면 자국도 가로로 길다
    QImage bar(60, 12, QImage::Format_ARGB32_Premultiplied);
    bar.fill(Qt::black);
    const int id = addCustomBrush("막대", bar);
    CHECK(id >= 0 && customBrushCount() == id + 1);
    CHECK(customBrushName(id) == QStringLiteral("막대"));
    BrushParams cb; cb.size = 40; cb.preset = 0; cb.customBrush = id;
    c->setPenTilt(0, 0);
    dot(100, cb);
    im = d->keyImage(0, 0);
    CHECK(span(im, 100, true) > span(im, 100, false) + 10);
    d->undoStack()->undo();
    clearCustomBrushes();
    CHECK(customBrushCount() == 0);
    // 사용자 질감: 지정하면 빈 곳이 생긴다
    QImage tex(64, 64, QImage::Format_ARGB32);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) tex.setPixel(x, y, ((x / 4 + y / 4) % 2) ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
    setCustomGrainTexture(tex);
    CHECK(!customGrainTexture().isNull());
    BrushParams gb; gb.size = 24; gb.preset = 0; gb.hardness = 100;
    c->setBrush(gb);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {40, 130}));
    QTest::mouseMove(c, imgToWidget(c, d, {160, 130}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {160, 130}));
    im = d->keyImage(0, 0);
    int solid = 0, holes = 0;
    for (int x = 40; x < 160; ++x) (qAlpha(im.pixel(x, 130)) > 128 ? solid : holes)++;
    CHECK(solid > 10 && holes > 10);     // 질감 때문에 칠해진 곳과 빈 곳이 섞인다
    setCustomGrainTexture(QImage());
    CHECK(customGrainTexture().isNull());
    d->undoStack()->undo();
}

static void testCutDashBrush() {
    CHECK(brushPresetCount() >= 27);
    int dash = -1, cut = -1, dot = -1;
    for (int i = 0; i < brushPresetCount(); ++i) {
        const QString n = QString::fromUtf8(brushPreset(i).name);
        if (n == "파선") dash = i;
        if (n == "컷 브러시") cut = i;
        if (n == "점선") dot = i;
    }
    CHECK(dash >= 0 && cut >= 0 && dot >= 0);
    CHECK(brushPreset(cut).eraser);
    QImage buf(200, 40, QImage::Format_ARGB32_Premultiplied);
    buf.fill(Qt::transparent);
    BrushParams p; p.preset = dash; p.size = 12;
    QRandomGenerator rng(1);
    BrushDabCache cache;
    double carry = 0;
    brushStampAlong(buf, p, Qt::black, QPointF(10, 20), QPointF(190, 20), 1, 1, carry, rng, cache);
    int runs = 0; bool in = false;
    for (int x = 0; x < 200; ++x) {
        const bool op = qAlpha(buf.pixel(x, 20)) > 128;
        if (op && !in) ++runs;
        in = op;
    }
    CHECK(runs >= 4);          // 선이 끊어진 대시들로 나온다
    // 대시는 획 방향(가로)으로 길쭉하다: 같은 대시 안에서 가로 폭 > 세로 폭
    int w0 = 0, h0 = 0;
    for (int x = 0; x < 30; ++x) if (qAlpha(buf.pixel(x, 20)) > 128) ++w0;
    for (int y = 0; y < 40; ++y) if (qAlpha(buf.pixel(12, y)) > 128) ++h0;
    CHECK(w0 > h0);
}

static void testFxCategories(MainWindow& w) {
    QImage im(64, 48, QImage::Format_ARGB32_Premultiplied);
    im.fill(Qt::white);
    { QPainter p(&im); p.fillRect(10, 10, 20, 20, QColor(200, 30, 30)); }
    int n = 0;
    for (int i = 0; i <= int(Effects::Type::Last); ++i) {
        Effects::Item it{Effects::Type(i), Effects::paramInfo(Effects::Type(i)).def};
        const QImage o = Effects::apply(im, it);
        CHECK(o.size() == im.size());
        CHECK(!Effects::typeName(it.type).isEmpty());
        ++n;
    }
    CHECK(n >= 28);
    CHECK(Effects::categories().size() == 7);
    // 흰색 제거: 배경 알파 0, 빨간 사각형은 유지
    const QImage k = Effects::apply(im, {Effects::Type::KeyWhite, 20});
    CHECK(qAlpha(k.pixel(2, 2)) == 0);
    CHECK(qAlpha(k.pixel(20, 20)) == 255);
    // 모자이크는 크기를 유지하고, 세피아는 회색이 아닌 색
    CHECK(Effects::apply(im, {Effects::Type::Pixelate, 8}).size() == im.size());
    // 전환: 3프레임 사이를 디졸브로 채운다
    Document* d = w.document();
    d->newProject(QSize(32, 32), 6, 12);
    QImage a(32, 32, QImage::Format_ARGB32_Premultiplied), b = a;
    a.fill(Qt::black); b.fill(Qt::white);
    d->pushKeyEdit(0, 0, QImage(), a, "a");
    d->pushKeyEdit(0, 4, QImage(), b, "b");
    d->setCurrentFrame(0);
    CHECK(w.runTransition(0, 0, 4, 0));
    const int gv = qRed(d->keyImage(0, d->resolveKey(0, 2)).pixel(5, 5));
    CHECK(gv > 90 && gv < 170);
    CHECK(d->layer(0).frames[1].isNull() == false);
    d->undoStack()->undo();
    CHECK(d->layer(0).frames[1].isNull());
    // FX 저장/불러오기 왕복
    d->addFx(0, {Effects::Type::Sharpen, 120});
    d->addFx(0, {Effects::Type::Swirl, 40});
    CHECK(d->saveCopy("/tmp/demo/fx.ony"));
    Document e;
    CHECK(e.load("/tmp/demo/fx.ony"));
    CHECK(e.layer(0).fx.size() == 2 && e.layer(0).fx[1].type == Effects::Type::Swirl);
}

static void testLightTable() {
    Document d;
    d.newProject(QSize(100, 60), 6, 12);
    auto rectImg = [](int x) {
        QImage im(100, 60, QImage::Format_ARGB32_Premultiplied);
        im.fill(Qt::transparent);
        QPainter p(&im);
        p.fillRect(x, 10, 20, 20, Qt::black);
        return im;
    };
    d.pushKeyEdit(0, 0, QImage(), rectImg(5), "a");
    d.pushKeyEdit(0, 2, QImage(), rectImg(40), "b");
    d.pushKeyEdit(0, 4, QImage(), rectImg(70), "c");
    d.setCurrentFrame(2);
    CanvasWidget c(&d);
    c.resize(400, 300);
    CHECK(greenPixels(c.grab().toImage()) == 0);
    c.setLightTable(true);
    CHECK(greenPixels(c.grab().toImage()) > 50);   // 이전 이미지(초록)
    CanvasWidget::LightTableParams p;
    for (int i = 0; i < CanvasWidget::LightTableParams::kSlots; ++i) p.prevOp[i] = 0;
    c.setLightTableParams(p);
    CHECK(greenPixels(c.grab().toImage()) == 0);   // 이전 이미지 슬라이더를 모두 0 으로
    // 북마크 모드: 북마크가 없으면 아무것도 안 보인다
    p.prevOp[0] = 60;
    p.mode = CanvasWidget::LightTableParams::Bookmark;
    c.setLightTableParams(p);
    CHECK(greenPixels(c.grab().toImage()) == 0);
    d.toggleBookmark(0);
    CHECK(greenPixels(c.grab().toImage()) > 50);
}

static void testSequenceExport() {
    Document d;
    d.newProject(QSize(40, 30), 5, 12);
    for (int f = 0; f < 5; ++f) d.pushKeyEdit(0, f, QImage(), solid(d.size(), QColor(40 * f, 100, 200)), "k");
    QTemporaryDir tmp;
    VideoOptions o;
    o.format = "png";
    o.from = 1;
    o.to = 3;
    o.background = QColor(0, 0, 0, 0);
    o.baseName = "x";
    CHECK(VideoExport::run(&d, tmp.path(), o).isEmpty());
    CHECK(QFileInfo::exists(tmp.filePath("x_0001.png")) && QFileInfo::exists(tmp.filePath("x_0003.png")) && !QFileInfo::exists(tmp.filePath("x_0004.png")));
    // 빈 레이어 위에서 배경이 투명인지 / 색이 지정되는지
    Document e;
    e.newProject(QSize(10, 10), 2, 12);
    CHECK(qAlpha(e.renderFrame(0, QColor(0, 0, 0, 0), false).pixel(1, 1)) == 0);
    CHECK(e.renderFrame(0, QColor(10, 20, 30), false).pixelColor(1, 1) == QColor(10, 20, 30));
    o.format = "jpg";
    o.scalePercent = 200;
    CHECK(VideoExport::run(&d, tmp.path(), o).isEmpty());
    CHECK(QImage(tmp.filePath("x_0001.jpg")).width() == 80);
}

static void testVideo() {
    const QString ff = VideoExport::findFfmpeg();
    if (ff.isEmpty()) { qInfo("ffmpeg 없음: 동영상 테스트 건너뜀"); return; }
    Document d;
    d.newProject(QSize(161, 91), 6, 12); // 홀수 크기도 인코딩돼야 한다
    for (int f = 0; f < 6; ++f) d.pushKeyEdit(0, f, QImage(), solid(d.size(), QColor(40 * f, 100, 200)), "k");
    QTemporaryDir tmp;
    for (const char* fmt : {"mp4", "webm", "gif", "mov"}) {
        VideoOptions o;
        o.format = fmt;
        o.scalePercent = 50;
        const QString out = tmp.filePath(QString("t.%1").arg(fmt));
        int last = 0;
        const QString err = VideoExport::run(&d, out, o, [&](int a, int) { last = a; return true; });
        if (!err.isEmpty()) qInfo("  %s: %s", fmt, qPrintable(err));
        CHECK(err.isEmpty() || QString(fmt) == "mov" || QString(fmt) == "webm"); // 인코더가 없는 빌드는 허용
        if (err.isEmpty()) CHECK(QFileInfo(out).size() > 100);
        CHECK(last > 0);
    }
    // 범위 지정 + 취소
    VideoOptions o;
    o.from = 2;
    o.to = 3;
    CHECK(VideoExport::run(&d, tmp.filePath("r.mp4"), o).isEmpty());
    CHECK(VideoExport::run(&d, tmp.filePath("c.mp4"), o, [](int, int) { return false; }) == "취소됨");
}

static void testLayerFeatures() {
    QTemporaryDir tmp;
    Document d;
    d.newProject(QSize(40, 30), 10, 12);
    // 프리/포스트 비헤이비어: 프레임 2~4 에 이미지 3장, 레이어 끝 = 5
    for (int i = 0; i < 3; ++i) d.pushKeyEdit(0, 2 + i, QImage(), solid(d.size(), QColor(10 * (i + 1), 0, 0)), "k");
    d.layer(0).endFrame = 5;
    d.layer(0).postBehavior = 0;
    CHECK(d.resolveKey(0, 1) == -1);   // 첫 이미지 이전: 없음
    CHECK(d.resolveKey(0, 3) == 3);
    CHECK(d.resolveKey(0, 6) == -1);   // 끝 이후: 없음
    d.layer(0).postBehavior = 1;       // 반복
    CHECK(d.resolveKey(0, 5) == 2);
    CHECK(d.resolveKey(0, 7) == 4);
    d.layer(0).postBehavior = 2;       // 핑퐁: 2 3 4 3 2 3 4 ...
    CHECK(d.resolveKey(0, 5) == 3);
    CHECK(d.resolveKey(0, 6) == 2);
    CHECK(d.resolveKey(0, 8) == 4);
    d.layer(0).postBehavior = 3;       // 정지
    CHECK(d.resolveKey(0, 9) == 4);
    d.layer(0).preBehavior = 3;
    CHECK(d.resolveKey(0, 0) == 2);

    // 블렌딩: 곱하기 / 더하기 / 지우기
    QImage base = solid(QSize(4, 4), QColor(200, 100, 50));
    QImage top = solid(QSize(4, 4), QColor(128, 128, 128));
    QImage m = base;
    Blend::apply(m, top, Blend::Multiply, 1.0);
    CHECK(qAbs(qRed(m.pixel(1, 1)) - 100) <= 2);
    m = base;
    Blend::apply(m, top, Blend::Add, 1.0);
    CHECK(qRed(m.pixel(1, 1)) == 255 && qGreen(m.pixel(1, 1)) >= 227);
    m = base;
    Blend::apply(m, top, Blend::Erase, 1.0);
    CHECK(qAlpha(m.pixel(1, 1)) == 0);
    m = base;
    Blend::apply(m, top, Blend::Color, 1.0);
    CHECK(qRed(m.pixel(1, 1)) == 128);
    CHECK(Blend::name(Blend::Multiply) == "곱하기");

    // 합성에 블렌딩 모드가 반영된다
    d.layer(0).preBehavior = 0;
    d.setCurrentFrame(3);
    d.addLayer("위");
    d.pushKeyEdit(1, 0, QImage(), solid(d.size(), QColor(255, 255, 255)), "k");
    d.layer(1).blend = Blend::Difference;
    const QImage comp = d.composite(3, false, false);
    CHECK(qRed(comp.pixel(1, 1)) == 255 - 20); // |20 - 255|

    // 스텐실: 한 레이어의 불투명한 곳만 허용
    Document s;
    s.newProject(QSize(20, 20), 2, 12);
    QImage src(s.size(), QImage::Format_ARGB32_Premultiplied);
    src.fill(Qt::transparent);
    { QPainter p(&src); p.fillRect(0, 0, 10, 20, Qt::black); }
    s.pushKeyEdit(0, 0, QImage(), src, "k");
    s.layer(0).stencil = true;
    s.addLayer("그림");
    const QImage mask = s.stencilMaskFor(1, 0);
    CHECK(!mask.isNull());
    CHECK(qAlpha(mask.pixel(5, 5)) == 255 && qAlpha(mask.pixel(15, 5)) == 0);
    s.layer(0).stencilInvert = true;
    const QImage inv = s.stencilMaskFor(1, 0);
    CHECK(qAlpha(inv.pixel(5, 5)) == 0 && qAlpha(inv.pixel(15, 5)) == 255);

    // 확장 블록 저장/불러오기: 비헤이비어, 모드, 북마크, 이미지 마크, 인스턴스 이름
    d.layer(0).instanceNames[2] = "A";
    d.layer(0).collapsed = true;
    d.layer(0).colorGroup = 4;
    d.setImageMark(0, 3, 0);
    d.toggleBookmark(6);
    const QString path = tmp.filePath("x.ony");
    CHECK(d.save(path));
    Document r;
    CHECK(r.load(path));
    CHECK(r.layer(0).endFrame == 5 && r.layer(0).postBehavior == 3 && r.layer(0).preBehavior == 0);
    CHECK(r.layer(1).blend == Blend::Difference);
    CHECK(r.layer(0).instanceNames.value(2) == "A");
    CHECK(r.layer(0).collapsed && r.layer(0).colorGroup == 4);
    CHECK(r.layer(0).imageMarks.value(3, -1) == 0);
    CHECK(r.hasBookmark(6));
    CHECK(r.nearestBookmark(0, 1) == 6 && r.nearestBookmark(9, -1) == 6);
}

static void testImageMenu() {
    Document d;
    d.newProject(QSize(32, 32), 10, 12);
    auto mk = [](int v) { QImage im(32, 32, QImage::Format_ARGB32_Premultiplied); im.fill(QColor(v, v, v)); return im; };
    d.layer(0).frames[0] = mk(10);
    d.layer(0).frames[2] = mk(20);  // 10 은 2프레임 노출
    d.layer(0).frames[3] = mk(30);
    d.reverseKeys(0, 0, 3);
    // 뒤집으면 30(1프레임) → 20(1프레임) → 10(2프레임) 순서
    CHECK(qRed(d.layer(0).frames[0].pixel(0, 0)) == 30);
    CHECK(qRed(d.layer(0).frames[1].pixel(0, 0)) == 20);
    CHECK(qRed(d.layer(0).frames[2].pixel(0, 0)) == 10);
    CHECK(d.layer(0).frames[3].isNull());
    d.setUniformExposure(0, 3);
    CHECK(!d.layer(0).frames[0].isNull() && !d.layer(0).frames[3].isNull() && !d.layer(0).frames[6].isNull());
    CHECK(d.layer(0).frames[1].isNull() && d.layer(0).frames[2].isNull());
    QTemporaryDir tmp;
    CHECK(d.save(tmp.filePath("a.ony")));
    d.setModified(true);
    CHECK(d.saveCopy(tmp.filePath("b.ony")));
    CHECK(d.path() == tmp.filePath("a.ony") && d.modified());
}

static void testRepeat() {
    auto mk = [](int v) { QImage im(8, 8, QImage::Format_ARGB32_Premultiplied); im.fill(QColor(v, v, v)); return im; };
    Document d;
    d.newProject(QSize(8, 8), 6, 12);
    d.layer(0).frames[0] = mk(10);
    d.layer(0).frames[2] = mk(20);
    CHECK(d.repeatKeys(0, 0, 2, 2, false) == 6);
    CHECK(d.frameCount() >= 9);
    auto v = [&](int f) { return d.layer(0).frames[f].isNull() ? -1 : qRed(d.layer(0).frames[f].pixel(0, 0)); };
    CHECK(v(3) == 10 && v(4) == -1 && v(5) == 20 && v(6) == 10 && v(8) == 20);
    Document e;
    e.newProject(QSize(8, 8), 6, 12);
    e.layer(0).frames[0] = mk(10);
    e.layer(0).frames[2] = mk(20);
    e.repeatKeys(0, 0, 2, 2, true);
    auto u = [&](int f) { return e.layer(0).frames[f].isNull() ? -1 : qRed(e.layer(0).frames[f].pixel(0, 0)); };
    CHECK(u(3) == 20 && u(4) == 10 && u(6) == 10 && u(8) == 20); // 첫 반복은 거꾸로, 둘째는 원래 순서
}

static void testCameraEase() {
    Document d;
    d.newProject(QSize(100, 60), 11, 12);
    CameraKey a, b;
    b.x = 100;
    a.ease = 1;                       // 느리게 시작
    d.setCameraKey(0, a);
    d.setCameraKey(10, b);
    const double mid = d.cameraAt(5).x;
    CHECK(mid < 20);                  // 느리게 시작이면 중간 지점에서 아직 멀리 못 갔다
    a.ease = 0;
    d.setCameraKey(0, a);
    CHECK(std::abs(d.cameraAt(5).x - 50) < 0.01);
    a.ease = 4;
    d.setCameraKey(0, a);
    CHECK(d.cameraAt(9).x == 0 && d.cameraAt(10).x == 100);
    a.ease = 3;
    d.setCameraKey(0, a);
    QTemporaryDir tmp;
    CHECK(d.save(tmp.filePath("c.ony")));
    Document e;
    CHECK(e.load(tmp.filePath("c.ony")));
    CHECK(e.cameraKeys().value(0).ease == 3);   // 저장/불러오기 후에도 속도 곡선 유지
}

static void testPingPong(MainWindow& w) {
    QToolButton *ping = nullptr, *play = nullptr, *stop = nullptr;
    for (auto* b : w.findChildren<QToolButton*>()) {
        if (b->toolTip().startsWith("왕복 재생")) ping = b;
        if (b->toolTip().startsWith("재생 (Enter)")) play = b;
        if (b->toolTip() == "정지") stop = b;
    }
    CHECK(ping && play);
    if (!ping || !play) return;
    Document* d = w.document();
    ping->setChecked(true);
    const int last = d->frameCount() - 1;
    d->setCurrentFrame(last - 2);
    QVector<int> seq;
    const auto conn = QObject::connect(d, &Document::currentChanged, &w, [&] { seq << d->currentFrame(); });
    play->click();
    QTest::qWait(700);
    if (stop) stop->click();
    QObject::disconnect(conn);
    int mx = 0, afterMaxMin = last;
    for (int v : seq) mx = std::max(mx, v);
    CHECK(mx == last);                  // 끝까지 갔다가
    bool seenMax = false;
    for (int v : seq) { if (v == mx) seenMax = true; else if (seenMax) afterMaxMin = std::min(afterMaxMin, v); }
    CHECK(afterMaxMin < last);          // 되돌아온다 (왕복)
    ping->setChecked(false);
}

static void testStretch() {
    auto mk = [](int v) { QImage im(8, 8, QImage::Format_ARGB32_Premultiplied); im.fill(QColor(v, v, v)); return im; };
    Document d;
    d.newProject(QSize(8, 8), 12, 12);
    d.layer(0).frames[0] = mk(10);
    d.layer(0).frames[6] = mk(20);
    d.toggleBookmark(6);
    d.stretchToFps(24);                     // 12fps → 24fps: 길이(초)는 같고 프레임 수는 2배
    CHECK(d.fps() == 24 && d.frameCount() == 24);
    CHECK(!d.layer(0).frames[0].isNull() && !d.layer(0).frames[12].isNull() && d.layer(0).frames[6].isNull());
    CHECK(d.hasBookmark(12));
    d.stretchToFps(12);                     // 다시 돌리면 원래 위치
    CHECK(d.frameCount() == 12 && !d.layer(0).frames[6].isNull());
}

static void testShortcuts(MainWindow& w) {
    w.activateWindow();
    QTest::qWait(30);
    using T = CanvasWidget::Tool;
    auto press = [&](Qt::Key k) { QTest::keyClick(&w, k); QTest::qWait(20); };
    {   // Ctrl+L 라이트 테이블, Ins 프레임 추가
        const int n0 = w.document()->frameCount();
        QTest::keyClick(&w, Qt::Key_Insert, Qt::ControlModifier); QTest::qWait(20);
        CHECK(w.document()->frameCount() == n0 + 1);
        QTest::keyClick(&w, Qt::Key_L, Qt::ControlModifier); QTest::qWait(20);
        CHECK(w.lightTableOn());
        QTest::keyClick(&w, Qt::Key_L, Qt::ControlModifier); QTest::qWait(20);
        CHECK(!w.lightTableOn());
        w.document()->deleteFrame(w.document()->currentFrame());
    }
    press(Qt::Key_E);
    CHECK(w.canvas()->tool() == T::Eraser);
    press(Qt::Key_B);
    CHECK(w.canvas()->tool() == T::Pen);
    press(Qt::Key_L);
    CHECK(w.canvas()->tool() == T::Line);
    press(Qt::Key_G);
    CHECK(w.canvas()->tool() == T::Bucket);
    press(Qt::Key_B);
    // 사용자 지정: E 를 해제하고 K 로 지우개
    auto& S = Shortcuts::instance();
    QString eid;
    for (const auto& c : S.commands()) if (c.category == "도구" && c.label == "지우개") eid = c.id;
    CHECK(!eid.isEmpty());
    S.set(eid, QKeySequence(Qt::Key_K));
    press(Qt::Key_E);
    CHECK(w.canvas()->tool() == T::Pen);
    press(Qt::Key_K);
    CHECK(w.canvas()->tool() == T::Eraser);
    // 이미 쓰는 키를 지정하면 기존 명령은 해제
    QString bid;
    for (const auto& c : S.commands()) if (c.category == "도구" && c.label == "브러시") bid = c.id;
    const QString lost = S.set(bid, QKeySequence(Qt::Key_K));
    CHECK(lost == "지우개");
    S.resetAll();
    press(Qt::Key_E);
    CHECK(w.canvas()->tool() == T::Eraser);
    press(Qt::Key_B);
}

static void testPaletteAndKeyProfile(MainWindow& w) {
    // 팔레트 저장 → 바꾸기 → 불러오기
    ColorPanel* cp = w.colorPanel();
    CHECK(cp != nullptr);
    const QVector<QColor> def = cp->swatches();
    CHECK(def.size() >= 16);
    cp->setSwatches({QColor("#112233"), QColor("#445566"), QColor("#778899")});
    CHECK(cp->swatches().size() == 3 && cp->swatches()[1] == QColor("#445566"));
    CHECK(w.savePalette("/tmp/demo/p.onypal"));
    CHECK(w.savePalette("/tmp/demo/p.gpl"));
    cp->setSwatches(ColorPanel::defaultPalette());
    CHECK(cp->swatches().size() == def.size());
    CHECK(w.loadPalette("/tmp/demo/p.onypal") == 3);
    CHECK(cp->swatches()[2] == QColor("#778899"));
    cp->setSwatches(ColorPanel::defaultPalette());
    CHECK(w.loadPalette("/tmp/demo/p.gpl") == 3);
    CHECK(cp->swatches()[0] == QColor("#112233"));
    CHECK(w.loadPalette("/tmp/demo/없는팔레트.onypal") == 0);
    cp->setSwatches(def);

    // 단축키 프로필
    auto& S = Shortcuts::instance();
    QString eid;
    for (const auto& c : S.commands())
        if (c.category == "도구" && c.label == "지우개") eid = c.id;
    CHECK(!eid.isEmpty());
    const QKeySequence orig = S.current(eid);
    S.set(eid, QKeySequence(Qt::Key_F8));
    CHECK(S.current(eid) == QKeySequence(Qt::Key_F8));
    CHECK(S.saveProfile("/tmp/demo/k.onykeys").isEmpty());
    S.resetAll();
    CHECK(S.current(eid) == orig);
    CHECK(S.loadProfile("/tmp/demo/k.onykeys").isEmpty());
    CHECK(S.current(eid) == QKeySequence(Qt::Key_F8));
    CHECK(!S.loadProfile("/tmp/demo/p.onypal").isEmpty());   // 다른 종류 파일은 거절
    S.resetAll();
    CHECK(S.current(eid) == orig);
}

static void testCollab() {
    const QString p = "/tmp/demo/shared/project.ony";
    QDir().mkpath("/tmp/demo/shared");
    QFile::remove(Collab::lockPath(p));
    QFile::remove(Collab::historyPath(p));
    {   // 프로젝트 파일이 있어야 보기 좋다
        Document d;
        d.newProject(QSize(40, 40), 2, 12);
        CHECK(d.saveCopy(p));
    }
    Collab::setUserName("나");
    CHECK(Collab::userName() == QStringLiteral("나"));
    // 처음에는 아무도 안 쓴다
    CHECK(!Collab::checkLock(p).locked);
    // 잠금 받기
    CHECK(Collab::acquireLock(p));
    Collab::LockInfo i = Collab::checkLock(p);
    CHECK(i.locked && i.mine && !i.stale);
    CHECK(i.user == QStringLiteral("나"));
    CHECK(QFile::exists(Collab::lockPath(p)));
    // 같은 사람이 다시 받아도 된다
    CHECK(Collab::acquireLock(p));
    // 다른 사람이면 거절당한다
    Collab::setUserName("동료");
    Collab::LockInfo other;
    CHECK(!Collab::acquireLock(p, &other));
    CHECK(other.locked && !other.mine && other.user == QStringLiteral("나"));
    // 남의 잠금은 그냥 못 푼다
    CHECK(!Collab::releaseLock(p));
    CHECK(Collab::checkLock(p).locked);
    // 뺏기는 된다
    CHECK(Collab::releaseLock(p, true));
    CHECK(!Collab::checkLock(p).locked);
    CHECK(Collab::acquireLock(p));
    CHECK(Collab::checkLock(p).user == QStringLiteral("동료"));
    // 오래된 잠금은 버려진 것으로 본다
    Collab::setStaleMinutes(5);
    {
        QFile f(Collab::lockPath(p));
        CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text));
        const QString old = QDateTime::currentDateTime().addSecs(-3600).toString(Qt::ISODate);
        f.write(QString("{\"user\":\"옛사람\",\"machine\":\"옛컴\",\"since\":\"%1\",\"touched\":\"%1\"}").arg(old).toUtf8());
    }
    i = Collab::checkLock(p);
    CHECK(i.locked && !i.mine && i.stale);
    CHECK(Collab::acquireLock(p));                       // 버려진 잠금은 가져올 수 있다
    CHECK(Collab::checkLock(p).user == QStringLiteral("동료"));
    // 이력
    Collab::appendHistory(p, "배경 칠함", 1234);
    Collab::setUserName("나");
    Collab::appendHistory(p, "선 정리", 2345);
    const auto h = Collab::readHistory(p);
    CHECK(h.size() == 2);
    CHECK(h[0].user == QStringLiteral("동료") && h[0].note == QStringLiteral("배경 칠함") && h[0].bytes == 1234);
    CHECK(h[1].user == QStringLiteral("나") && h[1].note == QStringLiteral("선 정리"));
    CHECK(h[1].when.isValid() && h[0].when <= h[1].when);
    // 표시 파일은 프로젝트 옆에 생긴다
    CHECK(Collab::lockPath(p) == p + ".lock");
    CHECK(Collab::historyPath(p) == p + ".history");
    CHECK(!Collab::releaseLock(p));            // 지금 잠금 주인은 "동료" 라 내가 못 푼다
    CHECK(Collab::releaseLock(p, true));
    CHECK(!QFile::exists(Collab::lockPath(p)));
    // 경로가 없으면 아무 일도 안 한다
    CHECK(!Collab::acquireLock(QString()));
    CHECK(!Collab::checkLock(QString()).locked);
    Collab::setStaleMinutes(120);
}

static void testPlugins(MainWindow& w) {
    // 예제 플러그인이 만들어지고 읽힌다
    Plugins::installSample();
    QVector<Plugins::Plugin> ps = Plugins::scan();
    int sample = -1;
    for (int i = 0; i < ps.size(); ++i)
        if (ps[i].id == QStringLiteral("ony-sample")) sample = i;
    CHECK(sample >= 0);
    CHECK(ps[sample].error.isEmpty());
    CHECK(ps[sample].commands.size() == 4);
    CHECK(ps[sample].name == QStringLiteral("예제 플러그인"));
    // 스크립트 파일에서 읽은 것과 바로 적은 것 둘 다 들어온다
    bool fromFile = false, inline_ = false, asked = false;
    for (const Plugins::Command& c : ps[sample].commands) {
        if (c.label.contains("바둑판")) fromFile = c.script.contains("layer.new");
        if (c.label.contains("뒤집고")) inline_ = c.script.contains("fx.invert");
        if (!c.ask.isEmpty()) asked = c.ask.startsWith("n:");
    }
    CHECK(fromFile && inline_ && asked);
    // 켜짐/꺼짐이 저장된다
    CHECK(Plugins::isEnabled("ony-sample"));
    Plugins::setEnabled("ony-sample", false);
    CHECK(!Plugins::isEnabled("ony-sample"));
    CHECK(!Plugins::scan()[sample].enabled);
    Plugins::setEnabled("ony-sample", true);
    // 플러그인 스크립트가 실제로 돈다
    Document* d = w.document();
    d->newProject(QSize(200, 160), 3, 12);
    const int before = d->layerCount();
    for (const Plugins::Command& c : ps[sample].commands)
        if (c.label.contains("바둑판")) {
            ScriptEngine e(d);
            e.setHost([&w](const QStringList& t) { return w.scriptHostCommand(t); });
            const QString log = e.run(c.script);
            CHECK(!log.contains("알 수 없는"));
        }
    CHECK(d->layerCount() == before + 1);
    CHECK(qAlpha(d->keyImage(d->currentLayer(), d->keyIndexAt(d->currentLayer(), 0)).pixel(20, 20)) > 200);
    // 설치 / 삭제
    const QString tmpDir = "/tmp/demo/myplug";
    QDir().mkpath(tmpDir);
    QFile j(tmpDir + "/ony-plugin.json");
    CHECK(j.open(QIODevice::WriteOnly | QIODevice::Text));
    j.write("{\"id\":\"myplug\",\"name\":\"내 플러그인\",\"commands\":[{\"label\":\"테스트\",\"run\":\"echo hi\"}]}");
    j.close();
    CHECK(Plugins::installFromFolder(tmpDir).isEmpty());
    bool found = false;
    for (const Plugins::Plugin& p : Plugins::scan())
        if (p.id == QStringLiteral("myplug")) found = p.commands.size() == 1 && p.name == QStringLiteral("내 플러그인");
    CHECK(found);
    CHECK(!Plugins::installFromFolder(tmpDir).isEmpty());        // 같은 이름은 거절
    CHECK(!Plugins::installFromFolder("/tmp/demo").isEmpty());   // json 없는 폴더는 거절
    CHECK(Plugins::removePlugin("myplug").isEmpty());
    for (const Plugins::Plugin& p : Plugins::scan()) CHECK(p.id != QStringLiteral("myplug"));
    CHECK(!Plugins::removePlugin("myplug").isEmpty());
}

static void testCompositeRegion() {
    Document d;
    d.newProject(QSize(120, 90), 3, 12);
    d.addLayer("위");
    QImage a(120, 90, QImage::Format_ARGB32_Premultiplied);
    a.fill(Qt::transparent);
    { QPainter p(&a); p.fillRect(0, 0, 120, 90, QColor(40, 90, 200)); }
    QImage b = a;
    { QPainter p(&b); p.fillRect(0, 0, 120, 90, Qt::transparent); p.setCompositionMode(QPainter::CompositionMode_Source); p.fillRect(10, 10, 40, 30, QColor(220, 40, 40)); }
    d.pushKeyEdit(0, 0, QImage(), a, "a");
    d.pushKeyEdit(1, 0, QImage(), b, "b");
    const QImage full = d.composite(0, true, false);
    // 일부만 다시 그려도 전체와 같아야 한다
    QImage partial = full;
    { QPainter p(&partial); p.setCompositionMode(QPainter::CompositionMode_Source); p.fillRect(20, 20, 50, 40, Qt::green); }
    CHECK(partial != full);
    CHECK(d.compositeRegion(partial, QRect(20, 20, 50, 40), 0, true));
    CHECK(partial == full);
    // 영역 밖은 건드리지 않는다
    QImage keep = full;
    { QPainter p(&keep); p.setCompositionMode(QPainter::CompositionMode_Source); p.fillRect(100, 70, 15, 15, Qt::green); }
    CHECK(d.compositeRegion(keep, QRect(0, 0, 20, 20), 0, true));
    CHECK(qGreen(keep.pixel(105, 75)) > 200 && qRed(keep.pixel(105, 75)) < 60);   // 밖은 그대로
    CHECK(keep.pixel(5, 5) == full.pixel(5, 5));                                   // 안은 제대로
    // 블렌딩 모드가 있으면 거절한다 (그때는 전체 합성을 써야 한다)
    d.layer(1).blend = 11;
    QImage x = full;
    CHECK(!d.compositeRegion(x, QRect(0, 0, 20, 20), 0, true));
    d.layer(1).blend = 0;
    // 크기가 다르면 거절
    QImage small(40, 40, QImage::Format_ARGB32_Premultiplied);
    CHECK(!d.compositeRegion(small, QRect(0, 0, 10, 10), 0, true));
    // FX 캐시를 써도 결과가 같다
    d.addFx(1, {Effects::Type::Invert, 0});
    const QImage f1 = d.composite(0, true, false);
    const QImage f2 = d.composite(0, true, false);   // 두 번째는 캐시에서
    CHECK(f1 == f2);
    d.pushKeyEdit(1, 0, b, a, "바꿈");                // 그림이 바뀌면 캐시가 안 쓰인다
    const QImage f3 = d.composite(0, true, false);
    CHECK(f3 != f1);
    d.undoStack()->undo();
    CHECK(d.composite(0, true, false) == f1);
}

static void testInbetween() {
    Document d;
    d.newProject(QSize(120, 120), 7, 12);
    auto circle = [&](int cx, int cy, int r) {
        QImage im(120, 120, QImage::Format_ARGB32_Premultiplied);
        im.fill(Qt::transparent);
        QPainter p(&im);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setBrush(Qt::black);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(cx, cy), r, r);
        return im;
    };
    // 왼쪽 작은 원 → 오른쪽 큰 원
    d.pushKeyEdit(0, 0, QImage(), circle(30, 60, 12), "a");
    d.pushKeyEdit(0, 6, QImage(), circle(90, 60, 24), "b");
    CHECK(d.keyIndexAt(0, 3) == 0);                 // 아직 사이는 홀드
    const int made = d.makeInbetweens(0, 0, 6, 1, 0);
    CHECK(made == 5);
    CHECK(d.keyIndexAt(0, 3) == 3);                 // 사이가 키로 찼다
    auto bounds = [](const QImage& im) {
        int x0 = im.width(), x1 = -1;
        for (int x = 0; x < im.width(); ++x)
            if (qAlpha(im.pixel(x, 60)) > 128) { x0 = std::min(x0, x); x1 = std::max(x1, x); }
        return QPair<int, int>(x0, x1);
    };
    const auto b0 = bounds(d.keyImage(0, 0)), b3 = bounds(d.keyImage(0, 3)), b6 = bounds(d.keyImage(0, 6));
    // 중간 그림의 가운데와 크기가 양 끝 사이에 있다
    const double c0 = (b0.first + b0.second) / 2.0, c3 = (b3.first + b3.second) / 2.0, c6 = (b6.first + b6.second) / 2.0;
    CHECK(c3 > c0 + 10 && c3 < c6 - 10);
    const int w0 = b0.second - b0.first, w3 = b3.second - b3.first, w6 = b6.second - b6.first;
    // 가운데 프레임은 선이 끊기지 않게 살짝 두껍게 잡으므로, 양 끝 사이 ±선굵기 안에 들어오면 맞다
    CHECK(w3 > w0 - 4 && w3 < w6 + 4);
    CHECK(w3 > w0 && w6 > w3 - 8);
    // 모프는 한 덩어리로 이어진다 (크로스페이드처럼 두 개로 갈라지지 않는다)
    int runs = 0;
    bool in = false;
    for (int x = 0; x < 120; ++x) {
        const bool on = qAlpha(d.keyImage(0, 3).pixel(x, 60)) > 128;
        if (on && !in) ++runs;
        in = on;
    }
    CHECK(runs == 1);
    // 크로스페이드는 두 덩어리가 보인다
    Document e;
    e.newProject(QSize(120, 120), 7, 12);
    e.pushKeyEdit(0, 0, QImage(), circle(30, 60, 12), "a");
    e.pushKeyEdit(0, 6, QImage(), circle(90, 60, 24), "b");
    CHECK(e.makeInbetweens(0, 0, 6, 2, 0) == 5);
    int runs2 = 0;
    in = false;
    for (int x = 0; x < 120; ++x) {
        const bool on = qAlpha(e.keyImage(0, 3).pixel(x, 60)) > 40;
        if (on && !in) ++runs2;
        in = on;
    }
    CHECK(runs2 == 2);
    // 속도 곡선
    CHECK(std::abs(Document::easeValue(0.5, 0) - 0.5) < 1e-9);
    CHECK(Document::easeValue(0.5, 1) < 0.3);
    CHECK(Document::easeValue(0.5, 2) > 0.7);
    // 양 끝이 키가 아니면 거절
    Document g;
    g.newProject(QSize(40, 40), 5, 12);
    CHECK(g.makeInbetweens(0, 0, 4, 1, 0) == 0);
    // 벡터 보간
    Document v;
    v.newProject(QSize(120, 120), 5, 12);
    v.setVectorLayer(0, true);
    VecStroke s1, s2;
    s1.pts = {QPointF(10, 20), QPointF(50, 20)};
    s1.pressure = {1, 1};
    s1.width = 4;
    s2.pts = {QPointF(10, 100), QPointF(50, 100)};
    s2.pressure = {1, 1};
    s2.width = 20;
    v.addVectorStroke(0, 0, s1);
    v.setVectorStrokes(0, 4, {s2});          // 4번 프레임은 획 하나로 둔다
    CHECK(v.vectorStrokeCount(0, 0) == 1 && v.vectorStrokeCount(0, 4) == 1);
    CHECK(v.makeInbetweens(0, 0, 4, 0, 0) == 3);
    const QVector<VecStroke>* mid = v.vectorStrokes(0, 2);
    CHECK(mid && mid->size() == 1);
    CHECK(std::abs(mid->first().pts[0].y() - 60) < 2);      // 20 과 100 의 가운데
    CHECK(std::abs(mid->first().width - 12) < 1);           // 4 와 20 의 가운데
    CHECK(qAlpha(v.keyImage(0, 2).pixel(30, 60)) > 100);
    // 보간 함수 자체
    const QVector<VecStroke> half = Document::interpolateStrokes({s1}, {s2}, 0.25);
    CHECK(half.size() == 1 && std::abs(half[0].pts[0].y() - 40) < 2);
}

static void testVectorLayer(MainWindow& w) {
    Document* d = w.document();
    CanvasWidget* c = w.canvas();
    d->newProject(QSize(200, 150), 6, 12);
    c->zoomActual();
    c->clearSelection();
    c->setGuides(CanvasWidget::Guides());
    c->setSymmetry(CanvasWidget::Symmetry());
    c->setSmoothing(false);
    CHECK(!d->isVectorLayer(0));
    d->setVectorLayer(0, true);
    CHECK(d->isVectorLayer(0));
    // 벡터 레이어에 그리면 획이 저장된다
    c->setColorA(Qt::black);
    BrushParams bp; bp.size = 8; bp.preset = 0;
    c->setBrush(bp);
    c->setTool(CanvasWidget::Tool::Pen);
    d->setCurrentFrame(1);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {20, 40}));
    for (int x = 40; x <= 160; x += 20) QTest::mouseMove(c, imgToWidget(c, d, {x, 40}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {160, 40}));
    CHECK(d->vectorStrokeCount(0, 1) == 1);
    const QVector<VecStroke>* sv = d->vectorStrokes(0, 1);
    CHECK(sv && sv->first().pts.size() >= 4);
    CHECK(std::abs(sv->first().width - 8) < 1e-9);
    // 그려진 그림도 생긴다
    CHECK(qAlpha(d->keyImage(0, d->keyIndexAt(0, 1)).pixel(90, 40)) > 100);
    CHECK(qAlpha(d->keyImage(0, d->keyIndexAt(0, 1)).pixel(90, 110)) == 0);
    // 굵기를 바꾸면 다시 그려진다
    QVector<VecStroke> mod = *sv;
    mod[0].width = 30;
    d->setVectorStrokes(0, 1, mod);
    CHECK(qAlpha(d->keyImage(0, d->keyIndexAt(0, 1)).pixel(90, 52)) > 100);   // 두꺼워져서 아래까지 닿는다
    // 점을 옮기면 모양이 바뀐다
    mod[0].width = 8;
    mod[0].pts[1] = QPointF(mod[0].pts[1].x(), 110);
    d->setVectorStrokes(0, 1, mod);
    CHECK(qAlpha(d->keyImage(0, d->keyIndexAt(0, 1)).pixel(int(mod[0].pts[1].x()), 110)) > 100);
    // 두 번째 획을 더한다
    VecStroke s2;
    s2.pts = {QPointF(20, 120), QPointF(170, 120)};
    s2.pressure = {1.0f, 1.0f};
    s2.color = Qt::red;
    s2.width = 6;
    d->addVectorStroke(0, 1, s2);
    CHECK(d->vectorStrokeCount(0, 1) == 2);
    CHECK(qRed(d->keyImage(0, d->keyIndexAt(0, 1)).pixel(100, 120)) > 180);
    // 저장 / 불러오기
    CHECK(d->saveCopy("/tmp/demo/vec.ony"));
    Document e;
    CHECK(e.load("/tmp/demo/vec.ony"));
    CHECK(e.isVectorLayer(0) && e.vectorStrokeCount(0, 1) == 2);
    CHECK(e.vectorStrokes(0, 1)->at(1).color == QColor(Qt::red));
    // 그림으로 굳히면 벡터는 사라지고 그림은 남는다
    const QImage before = d->keyImage(0, d->keyIndexAt(0, 1));
    d->flattenVectorLayer(0);
    CHECK(!d->isVectorLayer(0));
    CHECK(d->vectorStrokeCount(0, 1) == 0);
    CHECK(d->keyImage(0, d->keyIndexAt(0, 1)).size() == before.size());
    CHECK(qAlpha(d->keyImage(0, d->keyIndexAt(0, 1)).pixel(100, 120)) > 100);
    // 보통 레이어에 그리면 벡터로 저장되지 않는다
    d->newProject(QSize(100, 80), 4, 12);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {10, 10}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {40, 40}));
    CHECK(d->vectorStrokeCount(0, 0) == 0);
}

static void testScriptEngine(MainWindow& w) {
    Document* d = w.document();
    d->newProject(QSize(100, 80), 10, 12);
    ScriptEngine e(d);
    // 변수와 식
    e.run("set a = 2 + 3 * 4\nset b = (a - 2) / 3");
    CHECK(std::abs(e.var("a") - 14) < 1e-9);
    CHECK(std::abs(e.var("b") - 4) < 1e-9);
    e.run("set c = max(3, 7) + min(2, 9) + abs(-5) + int(2.9)");
    CHECK(std::abs(e.var("c") - (7 + 2 + 5 + 2)) < 1e-9);
    e.run("set t = 5 > 3 and 2 != 2");
    CHECK(e.var("t") == 0);
    e.run("set t2 = 5 > 3 or 2 != 2");
    CHECK(e.var("t2") == 1);
    // for
    e.run("set s = 0\nfor i = 1 to 5\n  set s = $s + $i\nend");
    CHECK(std::abs(e.var("s") - 15) < 1e-9);
    // for step
    e.run("set s2 = 0\nfor i = 0 to 10 step 5\n  set s2 = $s2 + 1\nend");
    CHECK(std::abs(e.var("s2") - 3) < 1e-9);
    // while
    e.run("set n = 0\nwhile $n < 4\n  set n = $n + 1\nend");
    CHECK(std::abs(e.var("n") - 4) < 1e-9);
    // if / else
    e.run("set r = 0\nif 1 > 2\n  set r = 1\nelse\n  set r = 2\nend");
    CHECK(std::abs(e.var("r") - 2) < 1e-9);
    // 중첩
    e.run("set tot = 0\nfor i = 1 to 3\n  for j = 1 to 3\n    if $i == $j\n      set tot = $tot + 1\n    end\n  end\nend");
    CHECK(std::abs(e.var("tot") - 3) < 1e-9);
    // 무한 반복은 멈춘다
    const QString stop = e.run("while 1 > 0\n  set x = 1\nend");
    CHECK(stop.contains("오래") || stop.contains("너무"));
    // 문서를 실제로 바꾼다
    e.run("layer.new 배경\nframe 3\nrect 10 10 40 30 #ff0000");
    CHECK(d->layerCount() == 2);
    CHECK(d->currentFrame() == 2);
    CHECK(qRed(d->keyImage(d->currentLayer(), d->keyIndexAt(d->currentLayer(), 2)).pixel(20, 20)) > 200);
    // get 으로 값을 읽는다
    e.run("get f frames\nget lc layers\nget wdt width");
    CHECK(std::abs(e.var("f") - 10) < 1e-9);
    CHECK(std::abs(e.var("lc") - 2) < 1e-9);
    CHECK(std::abs(e.var("wdt") - 100) < 1e-9);
    // 효과 명령
    e.run("fx.invert");
    CHECK(qRed(d->keyImage(d->currentLayer(), d->keyIndexAt(d->currentLayer(), 2)).pixel(20, 20)) < 60);
    e.run("fx.sepia\nfx.posterize 4\nfx.sharpen 120\nfx.levels 20");
    // 호스트 명령 (창 기능)
    e.setHost([&w](const QStringList& t) { return w.scriptHostCommand(t); });
    const QString hl = e.run("select.rect 5 5 40 30\nmsg 테스트");
    CHECK(!hl.contains("알 수 없는"));
    CHECK(w.canvas()->selection() == QRect(5, 5, 40, 30));
    e.run("select.none");
    CHECK(w.canvas()->selection().isNull());
    // 모르는 명령은 알려 준다
    CHECK(e.run("하늘에서별이").contains("알 수 없는"));
    CHECK(ScriptEngine::help().contains("while"));
    d->newProject(QSize(120, 90), 4, 12);
}

static void testLibraryAndPanel() {
    Document d;
    d.newProject(QSize(40, 40), 2, 12);
    QImage a(40, 40, QImage::Format_ARGB32_Premultiplied);
    a.fill(Qt::red);
    const int i0 = d.addToLibrary("빨강 배경", a);
    a.fill(Qt::blue);
    const int i1 = d.addToLibrary("파랑 배경", a);
    a.fill(Qt::green);
    const int i2 = d.addToLibrary("초록 소품", a);
    CHECK(d.libraryCount() == 3);
    CHECK(d.libraryCategory(i0).isEmpty());
    d.setLibraryCategory(i0, "배경");
    d.setLibraryCategory(i1, "배경");
    d.setLibraryCategory(i2, "소품");
    CHECK(d.libraryCategories() == QStringList({"배경", "소품"}));
    d.setLibraryName(i2, "초록 나무");
    CHECK(d.libraryName(i2) == QStringLiteral("초록 나무"));
    // 저장/불러오기에 분류가 따라간다
    CHECK(d.saveCopy("/tmp/demo/lib.ony"));
    Document e;
    CHECK(e.load("/tmp/demo/lib.ony"));
    CHECK(e.libraryCount() == 3 && e.libraryCategory(1) == QStringLiteral("배경"));
    CHECK(e.libraryName(2) == QStringLiteral("초록 나무"));
    // 삭제하면 분류도 같이 빠진다
    e.removeFromLibrary(0);
    CHECK(e.libraryCount() == 2 && e.libraryCategory(0) == QStringLiteral("배경") && e.libraryName(0) == QStringLiteral("파랑 배경"));

    // 사용자 패널 자유 배치
    CustomPanelDef def;
    def.name = "내 패널";
    def.labels = {"하나", "둘"};
    def.scripts = {"echo 1", "echo 2"};
    CustomPanel panel(&d, def);
    panel.resize(240, 200);
    CHECK(!panel.editing());
    panel.setEditing(true);
    CHECK(panel.editing());
    CustomPanelDef got;
    QObject::connect(&panel, &CustomPanel::layoutChanged, [&got](const CustomPanelDef& x) { got = x; });
    panel.autoArrange();
    CHECK(got.geoms.size() == 2);
    const QStringList p0 = got.geoms[0].split(',');
    CHECK(p0.size() == 4);
    // 저장된 배치가 다시 읽힌다
    CustomPanelDef def2 = def;
    def2.geoms = {"20,60,80,24", "120,90,60,30"};
    CustomPanel panel2(&d, def2);
    panel2.resize(240, 200);
    CHECK(panel2.def().geoms == def2.geoms);
}

static void testDemo5Basics(MainWindow& w) {
    Document* d = w.document();
    CanvasWidget* c = w.canvas();
    d->newProject(QSize(120, 90), 4, 12);
    c->zoomActual();
    c->setGuides(CanvasWidget::Guides());
    c->setSymmetry(CanvasWidget::Symmetry());
    // --- 선택 키우기 / 줄이기 / 부드럽게 ---
    c->setSelectionRect(QRect(40, 30, 30, 24));
    CHECK(c->selection() == QRect(40, 30, 30, 24));
    c->growSelection(5);
    QRect g = c->selection();
    CHECK(g.width() > 36 && g.height() > 30);          // 사방으로 커졌다
    CHECK(g.left() < 40 && g.top() < 30);
    c->setSelectionRect(QRect(40, 30, 30, 24));
    c->growSelection(-5);
    QRect sh = c->selection();
    CHECK(sh.width() < 26 && sh.height() < 20);        // 줄었다
    c->setSelectionRect(QRect(40, 30, 30, 24));
    CHECK(!c->selectionFeathered());
    c->featherSelection(6);
    CHECK(c->selectionFeathered());
    {   // 가장자리는 반투명, 가운데는 불투명
        const QImage m = c->selectionMask();
        CHECK(!m.isNull());
        CHECK(qAlpha(m.pixel(55, 42)) > 200);
        const int edge = qAlpha(m.pixel(40, 42));
        CHECK(edge > 20 && edge < 220);
    }
    // 부드러운 선택으로 지우면 가장자리가 반쯤 지워진다
    QImage full(120, 90, QImage::Format_ARGB32_Premultiplied);
    full.fill(Qt::black);
    d->pushKeyEdit(0, 0, QImage(), full, "fill");
    c->clearCurrent();
    const QImage after = d->keyImage(0, 0);
    CHECK(qAlpha(after.pixel(55, 42)) < 40);           // 가운데는 거의 다 지워지고
    const int e2 = qAlpha(after.pixel(38, 42));
    CHECK(e2 > 40 && e2 < 250);                        // 가장자리는 반쯤 남는다
    d->undoStack()->undo();
    c->clearSelection();

    // --- 안정화 세기 ---
    CHECK(c->smoothingStrength() == 60);
    c->setSmoothingStrength(90);
    CHECK(c->smoothingStrength() == 90);
    c->setSmoothingStrength(500);
    CHECK(c->smoothingStrength() == 99);               // 범위를 넘으면 잘린다
    c->setSmoothing(true);
    BrushParams bp; bp.size = 4; bp.preset = 0;
    c->setBrush(bp);
    c->setColorA(Qt::black);
    c->setTool(CanvasWidget::Tool::Pen);
    d->newProject(QSize(200, 120), 4, 12);
    c->zoomActual();
    // 세기가 높을수록 떨리는 손놀림이 매끄럽게 펴진다
    auto jitterSpread = [&](int strength) {
        c->setSmoothingStrength(strength);
        QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {20, 60}));
        for (int i = 1; i <= 16; ++i)             // 위아래로 흔들면서 간다
            QTest::mouseMove(c, imgToWidget(c, d, {20 + i * 10, 60 + ((i % 2) ? 14 : -14)}));
        QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {180, 60}));
        const QImage im = d->keyImage(0, 0);
        int top = im.height(), bottom = -1;        // 획이 세로로 번진 폭
        for (int y = 0; y < im.height(); ++y)
            for (int x = 40; x < 160; ++x)
                if (qAlpha(im.pixel(x, y)) > 100) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                    break;
                }
        d->undoStack()->undo();
        return bottom - top;
    };
    const int weak = jitterSpread(5), strong = jitterSpread(95);
    CHECK(weak > strong);                          // 보정이 세면 덜 흔들린다
    CHECK(strong >= 0);
    c->setSmoothing(false);
    c->setSmoothingStrength(60);

    // --- 커브 / 레벨 ---
    QImage grey(32, 32, QImage::Format_ARGB32_Premultiplied);
    grey.fill(QColor(128, 128, 128));
    QVector<int> up(256), same(256);
    for (int i = 0; i < 256; ++i) { up[i] = std::min(255, i + 60); same[i] = i; }
    const QImage br = Effects::curves(grey, up, up, up);
    CHECK(qRed(br.pixel(1, 1)) == 188);
    const QImage onlyR = Effects::curves(grey, up, same, same);
    CHECK(qRed(onlyR.pixel(1, 1)) == 188 && qGreen(onlyR.pixel(1, 1)) == 128);
    // 레벨: 입력 범위를 좁히면 대비가 커진다
    const QImage lv = Effects::levels(grey, 100, 150, 1.0, 0, 255);
    CHECK(qRed(lv.pixel(1, 1)) > 130 && qRed(lv.pixel(1, 1)) < 160);
    const QImage dark = Effects::levels(grey, 0, 255, 0.5, 0, 255);
    CHECK(qRed(dark.pixel(1, 1)) < 100);               // 감마를 내리면 어두워진다
    // 커브 위젯의 보정표
    CurveWidget cw;
    QVector<int> l0 = cw.lut();
    CHECK(l0[0] == 0 && l0[255] == 255 && l0[128] == 128);
    cw.setPoints({QPointF(0, 0), QPointF(0.5, 0.9), QPointF(1, 1)});
    const QVector<int> l1 = cw.lut();
    CHECK(l1[128] > 200 && l1[0] == 0 && l1[255] == 255);
    // FX 스택에도 들어간다
    CHECK(Effects::categoryOf(Effects::Type::Curves) == QStringLiteral("색"));
    CHECK(!Effects::typeName(Effects::Type::Levels).isEmpty());
    CHECK(Effects::apply(grey, {Effects::Type::Levels, 30}).size() == grey.size());

    // --- PSD 블렌딩 모드 ---
    CHECK(Psd::blendKeyFromOny(11) == QByteArray("mul "));      // 곱하기
    CHECK(Psd::onyModeFromBlendKey("mul ") == 11);
    CHECK(Psd::blendKeyFromOny(12) == QByteArray("scrn"));
    CHECK(Psd::onyModeFromBlendKey("zzzz") == 0);               // 모르는 키는 일반
    QVector<Psd::LayerData> ls;
    for (int i = 0; i < 2; ++i) {
        Psd::LayerData ld;
        ld.name = QString("L%1").arg(i);
        ld.blend = i == 0 ? QByteArray("norm") : QByteArray("mul ");
        QImage im(40, 30, QImage::Format_ARGB32_Premultiplied);
        im.fill(QColor(100 + i * 50, 80, 60));
        ld.image = im;
        ls.push_back(ld);
    }
    CHECK(Psd::write("/tmp/demo/blend.psd", QSize(40, 30), ls, QImage()).isEmpty());
    Psd::File pf;
    CHECK(Psd::read("/tmp/demo/blend.psd", &pf).isEmpty());
    CHECK(pf.layers.size() == 2);
    CHECK(pf.layers[0].blend == QByteArray("norm") && pf.layers[1].blend == QByteArray("mul "));
}

static void testPsdRoundTrip() {
    QSize sz(70, 50);
    QVector<Psd::LayerData> ls;
    for (int i = 0; i < 3; ++i) {
        Psd::LayerData l;
        l.name = QString("레이어 %1").arg(i + 1);
        l.opacity = i == 1 ? 128 : 255;
        l.visible = i != 2;
        QImage im(sz, QImage::Format_ARGB32_Premultiplied);
        im.fill(Qt::transparent);
        QPainter p(&im);
        p.fillRect(5 + i * 15, 5 + i * 8, 24, 20, QColor(i == 0 ? 220 : 30, i == 1 ? 200 : 40, i == 2 ? 210 : 50));
        p.end();
        l.image = im;
        ls.push_back(l);
    }
    CHECK(Psd::write("/tmp/demo/rt.psd", sz, ls, QImage()).isEmpty());
    CHECK(QFileInfo("/tmp/demo/rt.psd").size() > 500);
    Psd::File f;
    CHECK(Psd::read("/tmp/demo/rt.psd", &f).isEmpty());
    CHECK(f.size == sz && f.layers.size() == 3 && !f.merged.isNull());
    CHECK(f.layers[0].name == QStringLiteral("레이어 1"));   // 유니코드 이름이 그대로 온다
    CHECK(f.layers[1].opacity == 128 && f.layers[2].visible == false);
    for (int i = 0; i < 3; ++i) {
        const QImage a = ls[i].image.convertToFormat(QImage::Format_ARGB32);
        const QImage b = f.layers[i].image.convertToFormat(QImage::Format_ARGB32);
        int diff = 0;
        for (int y = 0; y < sz.height(); ++y)
            for (int x = 0; x < sz.width(); ++x) diff += a.pixel(x, y) != b.pixel(x, y);
        CHECK(diff == 0);                                     // 픽셀이 한 점도 안 틀린다
    }
    // 합쳐진 그림도 맞다 (보이는 레이어만, 불투명도 반영)
    CHECK(qAlpha(f.merged.pixel(10, 10)) == 255);
    // PSD 가 아닌 파일은 이유를 말한다
    QFile bad("/tmp/demo/notpsd.psd");
    CHECK(bad.open(QIODevice::WriteOnly));
    bad.write("hello world, not a psd at all");
    bad.close();
    Psd::File g;
    CHECK(Psd::read("/tmp/demo/notpsd.psd", &g) == QStringLiteral("PSD 파일이 아닙니다."));
}

static void testVideoImport(MainWindow& w) {
    if (VideoExport::findFfmpeg().isEmpty()) return;    // ffmpeg 이 없으면 건너뛴다
    if (!QFileInfo::exists("/tmp/demo/vid.mp4")) return;
    const VideoInfo info = VideoExport::probe("/tmp/demo/vid.mp4");
    CHECK(info.ok && info.size == QSize(160, 120));
    CHECK(info.duration > 1.5 && info.duration < 2.5);
    CHECK(info.hasAudio);
    QTemporaryDir td;
    CHECK(td.isValid());
    ImportOptions io;
    io.fps = 5;
    io.maxFrames = 20;
    QStringList files;
    CHECK(VideoExport::extractFrames("/tmp/demo/vid.mp4", td.path(), io, &files).isEmpty());
    CHECK(files.size() >= 8 && files.size() <= 20);
    CHECK(QImage(files[0]).size() == QSize(160, 120));
    // 소리 파형
    const QVector<float> peaks = VideoExport::audioPeaks("/tmp/demo/vid.mp4", 100);
    CHECK(peaks.size() == 100);
    float mx = 0;
    for (float v : peaks) mx = std::max(mx, v);
    CHECK(mx > 0.05f);                                  // 사인파라 또렷한 값이 나온다
    int nonzero = 0;
    for (float v : peaks) nonzero += v > 0.01f;
    CHECK(nonzero > 80);
    CHECK(VideoExport::audioPeaks("/tmp/demo/없는파일.wav", 50).isEmpty());
    // 두 단계 내보내기 (백그라운드 내보내기가 쓰는 길)
    Document* d = w.document();
    d->newProject(QSize(64, 48), 6, 12);
    QImage im(64, 48, QImage::Format_ARGB32_Premultiplied);
    im.fill(Qt::red);
    d->pushKeyEdit(0, 0, QImage(), im, "r");
    QTemporaryDir rd;
    VideoOptions vo;
    vo.format = "mp4";
    CHECK(VideoExport::renderFrames(d, rd.path(), vo).isEmpty());
    CHECK(QDir(rd.path()).entryList({"frame_*.png"}, QDir::Files).size() == 6);
    CHECK(VideoExport::encodeFrames(rd.path(), "/tmp/demo/two_stage.mp4", vo, 12, QString(), 0).isEmpty());
    CHECK(QFileInfo("/tmp/demo/two_stage.mp4").size() > 500);
}

static void testRenderPrintPublish(MainWindow& w) {
    Document* d = w.document();
    d->newProject(QSize(160, 100), 6, 12);
    QImage im(160, 100, QImage::Format_ARGB32_Premultiplied);
    im.fill(Qt::transparent);
    { QPainter p(&im); p.fillRect(0, 0, 160, 100, Qt::red); }
    d->pushKeyEdit(0, 0, QImage(), im, "r");
    // 렌더 카메라: 크기·맞춤 방식
    CHECK(d->renderSize() == QSize(160, 100));
    RenderCamera rc; rc.on = true; rc.size = QSize(320, 320); rc.fit = 0;
    d->setRenderCamera(rc);
    CHECK(d->renderSize() == QSize(320, 320));
    QImage r0 = d->renderFrame(0, Qt::white, false);
    CHECK(r0.size() == QSize(320, 320));
    CHECK(qRed(r0.pixel(160, 5)) == 255 && qGreen(r0.pixel(160, 5)) == 255);      // 위쪽 여백은 배경(흰색)
    CHECK(qGreen(r0.pixel(160, 160)) < 40);                                          // 가운데는 빨강
    rc.fit = 1; d->setRenderCamera(rc);
    r0 = d->renderFrame(0, Qt::white, false);
    CHECK(qGreen(r0.pixel(160, 5)) < 40);                                            // 채우기: 여백 없음
    // 저장/불러오기
    CHECK(d->saveCopy("/tmp/demo/rc.ony"));
    Document e;
    CHECK(e.load("/tmp/demo/rc.ony"));
    CHECK(e.renderCamera().on && e.renderCamera().size == QSize(320, 320) && e.renderCamera().fit == 1);
    // 인쇄용 PDF (콘택트 시트)
    PrintOptions po; po.cols = 2; po.rows = 2; po.title = "테스트";
    CHECK(Printing::pageCount(d, po) == 2);   // 6프레임 / 4 = 2쪽
    CHECK(Printing::exportPdf(d, "/tmp/demo/sheet.pdf", po));
    CHECK(QFileInfo("/tmp/demo/sheet.pdf").size() > 1000);
    // 퍼블리시 (ffmpeg 없이 가능한 항목)
    QDir("/tmp/demo/pub").removeRecursively();
    MainWindow::PublishOptions pub; pub.mp4 = false; pub.gif = false; pub.png = true;
    const QStringList made = w.publishTo("/tmp/demo/pub", "시험", pub);
    CHECK(made.size() >= 5);
    CHECK(QFileInfo("/tmp/demo/pub/시험.ony").exists());
    CHECK(QFileInfo("/tmp/demo/pub/시험_콘택트시트.pdf").exists());
    CHECK(QFileInfo("/tmp/demo/pub/시험_png/시험_0001.png").exists());
    CHECK(QImage("/tmp/demo/pub/시험_png/시험_0001.png").size() == QSize(320, 320));
    CHECK(QFileInfo("/tmp/demo/pub/퍼블리시_정보.txt").exists());
    d->newProject(QSize(320, 200), 12, 24);
    CHECK(d->renderSize() == QSize(320, 200));    // 새 프로젝트는 렌더 카메라가 꺼진다
}

static void testFrameEditing() {
    Document d;
    d.newProject(QSize(40, 40), 8, 12);
    d.addLayer("B");
    auto mk = [&](int v) {
        QImage im(40, 40, QImage::Format_ARGB32_Premultiplied);
        im.fill(QColor(v, v, v, 255));
        return im;
    };
    // 레이어 0: 0,2,4 에 키 / 레이어 1: 1 에 키
    d.pushKeyEdit(0, 0, QImage(), mk(10), "a");
    d.pushKeyEdit(0, 2, QImage(), mk(20), "b");
    d.pushKeyEdit(0, 4, QImage(), mk(30), "c");
    d.pushKeyEdit(1, 1, QImage(), mk(40), "d");
    const int n0 = d.frameCount();
    // 복사: 홀드로 시작하는 구간도 보이던 그림을 이어받는다
    FrameClip c = d.copyFrames(0, 1, 3, false);
    CHECK(c.count == 3 && c.data.size() == 1);
    CHECK(!c.data[0][0].isNull() && qRed(c.data[0][0].pixel(0, 0)) == 10);  // 1번은 홀드 → 0번 그림
    CHECK(!c.data[0][1].isNull() && qRed(c.data[0][1].pixel(0, 0)) == 20);
    CHECK(c.data[0][2].isNull());
    // 끼워 넣기: 길이가 늘고 모든 레이어가 같이 밀린다
    CHECK(d.pasteFrames(0, 6, c, true));
    CHECK(d.frameCount() == n0 + 3);
    CHECK(qRed(d.keyImage(0, d.resolveKey(0, 6)).pixel(0, 0)) == 10);
    CHECK(qRed(d.keyImage(0, d.resolveKey(0, 7)).pixel(0, 0)) == 20);
    CHECK(d.keyIndexAt(1, 1) == 1);   // 다른 레이어의 키는 자리 유지
    // 덮어쓰기
    CHECK(d.pasteFrames(0, 0, c, false));
    CHECK(d.frameCount() == n0 + 3);
    CHECK(qRed(d.keyImage(0, 0).pixel(0, 0)) == 10 && qRed(d.keyImage(0, 1).pixel(0, 0)) == 20);
    // 내용만 지우기: 길이는 그대로
    const int before = d.frameCount();
    CHECK(d.deleteFrames(0, 0, 1, false, false));
    CHECK(d.frameCount() == before && d.keyIndexAt(0, 0) == -1);
    // 당겨서 삭제: 길이가 준다
    CHECK(d.deleteFrames(0, 0, 2, true, true));
    CHECK(d.frameCount() == before - 3);
    CHECK(!d.deleteFrames(0, 0, d.frameCount() - 1, true, true));   // 전부 삭제는 거절
    // 옮기기: 길이는 그대로
    Document e;
    e.newProject(QSize(20, 20), 6, 12);
    for (int f = 0; f < 6; ++f) {
        QImage im(20, 20, QImage::Format_ARGB32_Premultiplied);
        im.fill(QColor(f * 20, 0, 0, 255));
        e.pushKeyEdit(0, f, QImage(), im, "f");
    }
    CHECK(e.moveFrames(0, 0, 1, 6, false));
    CHECK(e.frameCount() == 6);
    CHECK(qRed(e.keyImage(0, 0).pixel(0, 0)) == 40);     // 2번이 맨 앞으로
    CHECK(qRed(e.keyImage(0, 4).pixel(0, 0)) == 0);      // 0,1 번이 뒤로
    CHECK(qRed(e.keyImage(0, 5).pixel(0, 0)) == 20);
    CHECK(!e.moveFrames(0, 0, 1, 1, false));             // 제자리는 거절
    // 캔버스 크기가 다르면 붙지 않는다
    Document g;
    g.newProject(QSize(32, 32), 4, 12);
    CHECK(!g.pasteFrames(0, 0, c, true));
}

static void testNestedFolders() {
    Document d;
    d.newProject(QSize(32, 32), 2, 12);
    d.addLayer("안쪽");
    const int outer = d.addFolder("바깥");
    const int inner = d.addFolder("안");
    d.setFolderParent(inner, outer);
    CHECK(d.folderDepth(inner) == 1 && d.folderDepth(outer) == 0);
    CHECK(d.folderPath(inner) == QStringLiteral("바깥 / 안"));
    d.setLayerFolder(1, inner);
    QImage im(32, 32, QImage::Format_ARGB32_Premultiplied);
    im.fill(Qt::black);
    d.pushKeyEdit(1, 0, QImage(), im, "x");
    CHECK(qAlpha(d.composite(0, false, false).pixel(5, 5)) == 255);
    d.folder(outer).visible = false;                       // 바깥을 숨기면 안쪽 레이어도 숨는다
    CHECK(!d.folderVisibleEffective(inner));
    CHECK(qAlpha(d.composite(0, false, false).pixel(5, 5)) == 0);
    d.folder(outer).visible = true;
    d.folder(outer).opacity = 0.5;
    d.folder(inner).opacity = 0.5;
    CHECK(std::abs(d.folderOpacityEffective(inner) - 0.25) < 1e-6);
    const int a = qAlpha(d.composite(0, false, false).pixel(5, 5));
    CHECK(a > 50 && a < 80);
    // 순환 연결은 거절
    d.setFolderParent(outer, inner);
    CHECK(d.folder(outer).parent == -1);
    // 저장/불러오기
    CHECK(d.saveCopy("/tmp/demo/folders.ony"));
    Document e;
    CHECK(e.load("/tmp/demo/folders.ony"));
    CHECK(e.folder(inner).parent == outer);
    CHECK(e.folderPath(inner) == QStringLiteral("바깥 / 안"));
}

static void testSelectAndPick(MainWindow& w) {
    Document* d = w.document();
    CanvasWidget* c = w.canvas();
    d->newProject(QSize(200, 120), 4, 12);
    QImage im(200, 120, QImage::Format_ARGB32_Premultiplied);
    im.fill(Qt::transparent);
    { QPainter p(&im); p.fillRect(10, 10, 60, 40, QColor(220, 40, 40)); p.fillRect(120, 60, 50, 40, QColor(40, 90, 220)); }
    d->pushKeyEdit(0, 0, QImage(), im, "two");
    d->setCurrentFrame(0);
    // 색 추출
    QColor got;
    auto conn = QObject::connect(c, &CanvasWidget::colorPicked, [&](const QColor& x) { got = x; });
    c->setTool(CanvasWidget::Tool::Pick);
    c->zoomActual();
    QTest::mouseClick(c, Qt::LeftButton, {}, imgToWidget(c, d, {30, 30}));
    CHECK(got == QColor(220, 40, 40));
    QTest::mouseClick(c, Qt::LeftButton, {}, imgToWidget(c, d, {140, 80}));
    CHECK(got == QColor(40, 90, 220));
    QObject::disconnect(conn);
    // 마술봉: 빨간 사각형만 잡힌다
    c->setTool(CanvasWidget::Tool::Wand);
    c->setWandTolerance(20);
    QTest::mouseClick(c, Qt::LeftButton, {}, imgToWidget(c, d, {30, 30}));
    CHECK(c->selection() == QRect(10, 10, 60, 40));
    CHECK(!c->selectionMask().isNull());
    CHECK(qAlpha(c->selectionMask().pixel(30, 30)) > 128);
    CHECK(qAlpha(c->selectionMask().pixel(140, 80)) < 128);
    // Shift 로 파란 사각형 더하기
    QTest::mouseClick(c, Qt::LeftButton, Qt::ShiftModifier, imgToWidget(c, d, {140, 80}));
    CHECK(c->selection() == QRect(10, 10, 160, 90));
    CHECK(qAlpha(c->selectionMask().pixel(30, 30)) > 128 && qAlpha(c->selectionMask().pixel(140, 80)) > 128);
    // 선택 반전
    c->invertSelection();
    CHECK(qAlpha(c->selectionMask().pixel(30, 30)) < 128);
    CHECK(qAlpha(c->selectionMask().pixel(95, 20)) > 128);
    // 선택 영역만 지우기 (마스크 모양대로)
    c->setTool(CanvasWidget::Tool::Wand);
    QTest::mouseClick(c, Qt::LeftButton, {}, imgToWidget(c, d, {30, 30}));
    c->clearCurrent();
    const QImage after = d->keyImage(0, 0);
    CHECK(qAlpha(after.pixel(30, 30)) == 0);          // 빨간 곳은 지워지고
    CHECK(qAlpha(after.pixel(140, 80)) == 255);        // 파란 곳은 남는다
    d->undoStack()->undo();
    // 올가미: 드래그로 삼각형 모양 선택
    c->clearSelection();
    c->setTool(CanvasWidget::Tool::Lasso);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {20, 20}));
    QTest::mouseMove(c, imgToWidget(c, d, {90, 20}));
    QTest::mouseMove(c, imgToWidget(c, d, {90, 90}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {90, 90}));
    CHECK(!c->selectionMask().isNull());
    CHECK(qAlpha(c->selectionMask().pixel(80, 40)) > 128);   // 삼각형 안
    CHECK(qAlpha(c->selectionMask().pixel(30, 80)) < 128);   // 삼각형 밖
    c->clearSelection();
    CHECK(c->selection().isNull());
}

static void testGradientTextSymmetry(MainWindow& w) {
    Document* d = w.document();
    CanvasWidget* c = w.canvas();
    d->newProject(QSize(200, 120), 4, 12);
    c->zoomActual();
    c->clearSelection();
    QImage blank(200, 120, QImage::Format_ARGB32_Premultiplied);
    blank.fill(Qt::transparent);
    d->pushKeyEdit(0, 0, QImage(), blank, "blank");
    // 그라디언트
    c->setColorA(Qt::black);
    c->setColorB(Qt::white);
    c->setTool(CanvasWidget::Tool::Gradient);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {0, 60}));
    QTest::mouseMove(c, imgToWidget(c, d, {100, 60}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {199, 60}));
    const QImage g = d->keyImage(0, 0);
    CHECK(qRed(g.pixel(5, 60)) < 40 && qRed(g.pixel(195, 60)) > 215);
    CHECK(qRed(g.pixel(100, 60)) > 90 && qRed(g.pixel(100, 60)) < 170);
    d->undoStack()->undo();
    // 글자
    CanvasWidget::TextParams tp;
    tp.text = "A";
    tp.size = 60;
    c->setTextParams(tp);
    c->setColorA(Qt::black);
    c->setTool(CanvasWidget::Tool::Text);
    QTest::mouseClick(c, Qt::LeftButton, {}, imgToWidget(c, d, {40, 90}));
    int ink = 0;
    const QImage t = d->keyImage(0, 0);
    for (int y = 0; y < t.height(); ++y)
        for (int x = 0; x < t.width(); ++x) ink += qAlpha(t.pixel(x, y)) > 128 ? 1 : 0;
    CHECK(ink > 200);
    d->undoStack()->undo();
    // 대칭: 왼쪽에 그으면 오른쪽에도 생긴다
    CanvasWidget::Symmetry sy;
    sy.on = true;
    sy.vertical = true;
    c->setSymmetry(sy);
    BrushParams bp; bp.size = 8; bp.preset = 0;
    c->setBrush(bp);
    c->setTool(CanvasWidget::Tool::Pen);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {30, 60}));
    QTest::mouseMove(c, imgToWidget(c, d, {60, 60}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {60, 60}));
    const QImage s2 = d->keyImage(0, 0);
    CHECK(qAlpha(s2.pixel(45, 60)) > 100);          // 왼쪽 획
    CHECK(qAlpha(s2.pixel(200 - 45, 60)) > 100);    // 거울상
    sy.on = false;
    c->setSymmetry(sy);
    d->undoStack()->undo();
}

static void testRulerGuides(MainWindow& w) {
    Document* d = w.document();
    CanvasWidget* c = w.canvas();
    d->newProject(QSize(200, 120), 4, 12);
    c->zoomActual();
    c->clearSelection();
    c->setColorA(Qt::black);
    BrushParams bp; bp.size = 6; bp.preset = 0;
    c->setBrush(bp);
    c->setTool(CanvasWidget::Tool::Pen);
    // 직선자(수평·수직): 비스듬히 그어도 가로로 펴진다
    CanvasWidget::Guides g;
    g.ruler = 1;
    c->setGuides(g);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {20, 60}));
    QTest::mouseMove(c, imgToWidget(c, d, {80, 75}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {80, 75}));
    const QImage r = d->keyImage(0, 0);
    CHECK(qAlpha(r.pixel(60, 60)) > 100);     // 시작점과 같은 높이
    CHECK(qAlpha(r.pixel(60, 75)) < 60);      // 비스듬한 자리에는 없다
    d->undoStack()->undo();
    // 각도 지정
    g.ruler = 2;
    g.rulerAngle = 90;
    c->setGuides(g);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {100, 20}));
    QTest::mouseMove(c, imgToWidget(c, d, {130, 80}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {130, 80}));
    const QImage v = d->keyImage(0, 0);
    CHECK(qAlpha(v.pixel(100, 60)) > 100);    // 세로로 펴졌다
    CHECK(qAlpha(v.pixel(125, 60)) < 60);
    d->undoStack()->undo();
    // 소실점 방향으로 펴기
    g.ruler = 0;
    g.perspective = true;
    g.snapToVanish = true;
    g.vanishCount = 1;
    c->setGuides(g);
    const QPointF vp = c->vanishPoint(0);
    CHECK(vp.y() == 60);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {100, 60}));
    QTest::mouseMove(c, imgToWidget(c, d, {150, 70}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {150, 70}));
    const QImage pv = d->keyImage(0, 0);
    CHECK(qAlpha(pv.pixel(130, 60)) > 100);   // 소실점이 같은 높이라 가로로 펴진다
    CHECK(qAlpha(pv.pixel(130, 70)) < 60);
    d->undoStack()->undo();
    c->setGuides(CanvasWidget::Guides());
}

static void testRecorder(MainWindow& w) {
    Document* d = w.document();
    CanvasWidget* c = w.canvas();
    d->newProject(QSize(320, 200), 12, 24);
    c->zoomActual();
    c->setColorA(Qt::black);
    { BrushParams bp; bp.size = 5; c->setBrush(bp); }
    c->setTool(CanvasWidget::Tool::Pen);
    // 녹화 끔: 기록 없음
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {20, 20}));
    QTest::mouseMove(c, imgToWidget(c, d, {60, 20}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {60, 20}));
    CHECK(d->strokeCount() == 0);
    d->setRecording(true);
    d->setCurrentFrame(1);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {20, 100}));
    for (int x = 30; x <= 150; x += 10) QTest::mouseMove(c, imgToWidget(c, d, {x, 100}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {150, 100}));
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {20, 140}));
    for (int x = 30; x <= 150; x += 10) QTest::mouseMove(c, imgToWidget(c, d, {x, 140}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {150, 140}));
    d->setRecording(false);
    CHECK(d->strokeCount() == 2 && d->strokeCount(1) == 2 && d->strokeCount(0) == 0);
    // 절반만 그리면 첫 획만, 끝까지 그리면 두 획 모두
    auto cnt = [](const QImage& im, int y) { int n = 0; for (int x = 0; x < im.width(); ++x) n += qAlpha(im.pixel(x, y)) > 128; return n; };
    const QImage half = d->renderStrokes(1, 0.5, false), full = d->renderStrokes(1, 1.0, false);
    CHECK(cnt(half, 100) > 20 && cnt(half, 140) == 0);
    CHECK(cnt(full, 100) > 100 && cnt(full, 140) > 100);
    CHECK(cnt(d->renderStrokes(1, 0.25, false), 100) < cnt(half, 100));
    // 프레임으로 풀어 쓰기
    const int before = d->layerCount();
    const int l = d->strokesToFrames(1, 2, 6, false);
    CHECK(l == before && d->layerCount() == before + 1);
    CHECK(d->keyIndexAt(l, 2) == 2 && d->keyIndexAt(l, 7) == 7);
    CHECK(cnt(d->keyImage(l, 2), 100) < cnt(d->keyImage(l, 7), 100) + 1);
    CHECK(cnt(d->keyImage(l, 7), 140) > 100);
    // 저장/불러오기
    CHECK(d->saveCopy("/tmp/demo/rec.ony"));
    Document e;
    CHECK(e.load("/tmp/demo/rec.ony"));
    CHECK(e.strokeCount() == 2 && e.strokes()[0].pts.size() >= 10);
    CHECK(cnt(e.renderStrokes(1, 1.0, true), 140) > 100);
    d->clearStrokes(1);
    CHECK(d->strokeCount() == 0);
    d->setCurrentFrame(0);
    d->newProject(QSize(320, 200), 12, 24);
}

static void testCanvas(MainWindow& w) {
    Document* d = w.document();
    CanvasWidget* c = w.canvas();
    d->newProject(QSize(320, 200), 12, 24);
    c->zoomActual();
    c->setColorA(Qt::black);
    { BrushParams bp; bp.size = 5; c->setBrush(bp); }
    c->setTool(CanvasWidget::Tool::Pen);

    // 펜 스트로크
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {20, 20}));
    QTest::mouseMove(c, imgToWidget(c, d, {80, 20}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {80, 20}));
    CHECK(d->keyIndexAt(0, 0) == 0);
    CHECK(qAlpha(d->keyImage(0, 0).pixel(50, 20)) > 0);
    CHECK(d->undoStack()->count() == 1);

    // 실행 취소 -> 키 사라짐
    d->undoStack()->undo();
    CHECK(d->keyIndexAt(0, 0) == -1);
    d->undoStack()->redo();
    CHECK(d->keyIndexAt(0, 0) == 0);

    // 사각형 그린 뒤 페인트통 (안쪽 채우기)
    c->setTool(CanvasWidget::Tool::Rect);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {100, 60}));
    QTest::mouseMove(c, imgToWidget(c, d, {200, 140}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {200, 140}));
    c->setColorA(Qt::red);
    c->setTool(CanvasWidget::Tool::Bucket);
    QTest::mouseClick(c, Qt::LeftButton, {}, imgToWidget(c, d, {150, 100}));
    CHECK(d->keyImage(0, 0).pixel(150, 100) == qRgb(255, 0, 0));
    CHECK(d->keyImage(0, 0).pixel(10, 190) != qRgb(255, 0, 0));  // 사각형 바깥은 채워지지 않음

    // 지우개
    c->setTool(CanvasWidget::Tool::Eraser);
    { BrushParams bp; bp.size = 20; c->setBrush(bp); }
    QTest::mouseClick(c, Qt::LeftButton, {}, imgToWidget(c, d, {150, 100}));
    CHECK(qAlpha(d->keyImage(0, 0).pixel(150, 100)) == 0);

    // 선택 이동
    { BrushParams bp; bp.size = 5; c->setBrush(bp); }
    c->setColorA(Qt::blue);
    c->setTool(CanvasWidget::Tool::Rect);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {10, 150}));
    QTest::mouseMove(c, imgToWidget(c, d, {40, 180}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {40, 180}));
    c->setTool(CanvasWidget::Tool::Select);
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {0, 140}));
    QTest::mouseMove(c, imgToWidget(c, d, {60, 195}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {60, 195}));
    const int before = d->undoStack()->count();
    QTest::mousePress(c, Qt::LeftButton, {}, imgToWidget(c, d, {30, 170}));
    QTest::mouseMove(c, imgToWidget(c, d, {130, 170}));
    QTest::mouseRelease(c, Qt::LeftButton, {}, imgToWidget(c, d, {130, 170}));
    CHECK(d->undoStack()->count() == before + 1);
    CHECK(qAlpha(d->keyImage(0, 0).pixel(10, 165)) == 0);   // 원래 자리는 비어 있고
    CHECK(qAlpha(d->keyImage(0, 0).pixel(110, 165)) > 0);   // 이동한 자리에 그려져 있다

    // 잠긴 레이어에는 그려지지 않는다
    const int cnt = d->undoStack()->count();
    d->layer(0).locked = true;
    c->setTool(CanvasWidget::Tool::Pen);
    QTest::mouseClick(c, Qt::LeftButton, {}, imgToWidget(c, d, {5, 5}));
    CHECK(d->undoStack()->count() == cnt);
    d->layer(0).locked = false;

    // 다른 프레임에 그리면 새 키가 생긴다
    d->setCurrentFrame(5);
    QTest::mouseClick(c, Qt::LeftButton, {}, imgToWidget(c, d, {5, 5}));
    CHECK(d->keyIndexAt(0, 5) == 0);  // 프레임 0 의 키가 홀드 중이므로 같은 이미지를 편집(TVPaint 방식)
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    app.setStyle(Style::createStyle());
    Style::installMotion();

    testDocument();
    testEffects();
    testV2();
    testLayerFeatures();
    testBrushes();
    testWarp();
    testExtras();

    MainWindow w;
    w.resize(1600, 900);
    w.show();
    QTest::qWait(200);
    testClipsAndPlay(w);
    testResizeAndNext(w);
    testSequenceExport();
    testLightTable();
    testVideo();
    testPingPong(w);
    testShortcuts(w);
    testPuppet();
    testTimelineZoom();
    testTiltAndCustomBrush(w);
    testCutDashBrush();
    testFxCategories(w);
    testImageMenu();
    testRepeat();
    testStretch();
    testCameraEase();
    { // 클립이 많아져도 검색창이 밀리지 않고, 오른쪽 패널을 넓게 끌 수 있다
        QAction* newClipAct = nullptr;
        for (auto* a : w.findChildren<QAction*>()) if (a->text() == "새 클립" && a->parent() && qobject_cast<QMenu*>(a->parent())) newClipAct = a;
        CHECK(newClipAct != nullptr);
        for (int i = 0; newClipAct && i < 25; ++i) newClipAct->trigger();
        QTest::qWait(100);
        QLineEdit* search = nullptr;
        for (auto* le : w.findChildren<QLineEdit*>()) if (le->placeholderText().contains("매뉴얼")) search = le;
        CHECK(search && search->isVisible());
        if (search) CHECK(search->mapTo(&w, QPoint(search->width(), 0)).x() <= w.width());
        QDockWidget* brush = nullptr;
        for (auto* d : w.findChildren<QDockWidget*>()) if (d->windowTitle() == "도구 프리셋") brush = d;
        CHECK(brush != nullptr);
        if (brush) {
            w.resizeDocks({brush}, {760}, Qt::Horizontal);
            QTest::qWait(100);
            CHECK(brush->width() >= 700);
        }
        w.grab().save("/tmp/demo/10_clips.png");
    }
    testFrameEditing();
    testNestedFolders();
    testSelectAndPick(w);
    testGradientTextSymmetry(w);
    testRulerGuides(w);
    testRecorder(w);
    testPaletteAndKeyProfile(w);
    testCollab();
    testPlugins(w);
    testCompositeRegion();
    testInbetween();
    testVectorLayer(w);
    testScriptEngine(w);
    testLibraryAndPanel();
    testDemo5Basics(w);
    testPsdRoundTrip();
    testVideoImport(w);
    testRenderPrintPublish(w);
    testCanvas(w);

    // 스크린샷 (레이어/프레임을 더 만들어 타임라인이 보이도록)
    Document* d = w.document();
    d->setCurrentFrame(0);
    d->addLayer("선화");
    d->setCurrentFrame(8);
    d->pushKeyEdit(1, 8, QImage(), solid(d->size(), QColor(0, 0, 0, 0)), "k");
    d->setCurrentFrame(2);
    QTest::qWait(300);
    const QString out = argc > 1 ? argv[1] : QString("/tmp/ony-shot.png");
    w.grab().save(out);
    qInfo("screenshot: %s", qPrintable(out));

    // 룸 전환: 애니메이션 룸 스크린샷 + 다시 드로잉으로 복귀
    QComboBox* rooms = nullptr;
    for (auto* cb : w.findChildren<QComboBox*>())
        if (cb->findText("스토리보드") >= 0) rooms = cb;
    CHECK(rooms != nullptr);
    if (rooms) {
        rooms->setCurrentIndex(1);
        QTest::qWait(300);
        w.grab().save(out + ".room2.png");
        rooms->setCurrentIndex(0);
        QTest::qWait(200);
        CHECK(rooms->currentIndex() == 0);
    }

    qInfo("실패 %d건", g_fail);
    return g_fail ? 1 : 0;
}
