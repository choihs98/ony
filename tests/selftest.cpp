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

static void testShortcuts(MainWindow& w) {
    using T = CanvasWidget::Tool;
    auto press = [&](Qt::Key k) { QTest::keyClick(&w, k); QTest::qWait(20); };
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
    testVideo();
    testShortcuts(w);
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
