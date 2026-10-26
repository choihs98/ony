#include "Style.h"
#include <QDialogButtonBox>
#include <QPushButton>

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QApplication>
#include <QDialog>
#include <QEasingCurve>
#include <QEvent>
#include <QMenu>
#include <QProxyStyle>
#include <QPropertyAnimation>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleOption>
#include <QToolBar>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <algorithm>

namespace Style {

static const Scheme kSchemes[] = {
    // window, dark, base, text, muted, button, accent, canvasBg
    {QColor(0x48, 0x48, 0x48), QColor(0x3a, 0x3a, 0x3a), QColor(0x35, 0x3b, 0x3b), QColor(0xd2, 0xd2, 0xd2),
     QColor(0xa0, 0xa0, 0xa0), QColor(0x55, 0x55, 0x55), QColor(0x4f, 0x86, 0xa3), QColor(0x2d, 0x31, 0x36)}, // 회색 (TVPaint 풍)
    {QColor(0x2a, 0x2d, 0x35), QColor(0x1f, 0x22, 0x2a), QColor(0x1f, 0x22, 0x2a), QColor(0xea, 0xec, 0xef),
     QColor(0x9a, 0xa0, 0xa6), QColor(0x32, 0x36, 0x42), QColor(0x4a, 0x90, 0xe2), QColor(0x25, 0x27, 0x2c)}, // 밤
    {QColor(0x3a, 0x3d, 0x46), QColor(0x2c, 0x2f, 0x38), QColor(0x2c, 0x2f, 0x38), QColor(0xea, 0xec, 0xef),
     QColor(0xa8, 0xad, 0xb5), QColor(0x45, 0x49, 0x56), QColor(0x4a, 0x90, 0xe2), QColor(0x30, 0x33, 0x3b)}, // 황혼
    {QColor(0xe8, 0xe9, 0xed), QColor(0xd4, 0xd6, 0xdc), QColor(0xff, 0xff, 0xff), QColor(0x1b, 0x1d, 0x22),
     QColor(0x5f, 0x64, 0x6d), QColor(0xf0, 0xf1, 0xf4), QColor(0x3d, 0x7f, 0xd0), QColor(0xc9, 0xcc, 0xd3)}, // 낮
    {QColor(0xc9, 0xcb, 0xd2), QColor(0xb6, 0xb9, 0xc1), QColor(0xe1, 0xe3, 0xe8), QColor(0x1b, 0x1d, 0x22),
     QColor(0x55, 0x5a, 0x63), QColor(0xd3, 0xd5, 0xdb), QColor(0x3d, 0x7f, 0xd0), QColor(0xb0, 0xb3, 0xbb)}, // 새벽
};

int schemeCount() { return int(std::size(kSchemes)); }

const Scheme& scheme(int index) { return kSchemes[std::clamp(index, 0, schemeCount() - 1)]; }

QString schemeName(int index) {
    static const char* names[] = {"회색 (기본)", "밤", "황혼", "낮", "새벽"};
    return QString::fromUtf8(names[std::clamp(index, 0, schemeCount() - 1)]);
}

QString styleSheet(const Scheme& s) {
    const bool dark = s.window.lightness() < 128;
    const QString hover = (dark ? s.button.lighter(118) : s.button.darker(106)).name();
    const QString pressed = (dark ? s.button.darker(120) : s.button.darker(118)).name();
    const QString border = (dark ? s.dark.darker(115) : s.dark.darker(108)).name();
    const QString hl = s.accent.name();
    QString css = QStringLiteral(R"(
QMainWindow, QDialog { background: %1; }
QMenuBar { background: %2; color: %4; spacing: 2px; }
QMenuBar::item { padding: 3px 9px; background: transparent; }
QMenuBar::item:selected { background: %7; color: white; }
QMenu { background: %2; color: %4; border: 1px solid %9; }
QMenu::item { padding: 4px 26px 4px 22px; }
QMenu::item:selected { background: %7; color: white; }
QMenu::separator { height: 1px; background: %9; margin: 3px 6px; }
QToolBar { background: %1; border: 0; spacing: 2px; padding: 2px; }
QToolButton::menu-indicator { image: none; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: %3; color: %4; border: 1px solid %9; border-radius: 2px; padding: 1px 3px; selection-background-color: %7; }
QComboBox QAbstractItemView { background: %2; color: %4; selection-background-color: %7; }
QListWidget, QTableWidget, QTreeWidget, QPlainTextEdit, QTextEdit { background: %3; color: %4; border: 1px solid %9; selection-background-color: %7; selection-color: white; }
QHeaderView::section { background: %2; color: %5; border: 0; border-right: 1px solid %9; padding: 2px 4px; }
QLabel { background: transparent; color: %4; }
QCheckBox { color: %4; spacing: 5px; }
QStatusBar { background: %2; color: %4; }
QStatusBar::item { border: 0; }
QToolTip { background: %2; color: %4; border: 1px solid %9; padding: 2px 4px; }
QScrollArea { border: 0; }
QScrollBar:horizontal { background: %2; height: 12px; margin: 0; }
QScrollBar:vertical { background: %2; width: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: %6; min-width: 24px; border-radius: 3px; margin: 2px; }
QScrollBar::handle:vertical { background: %6; min-height: 24px; border-radius: 3px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QSlider::groove:horizontal { height: 4px; background: %2; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %7; border-radius: 2px; }
QSlider::handle:horizontal { width: 10px; margin: -5px 0; background: %12; border: 1px solid %9; border-radius: 2px; }
QTabBar::tab { background: %6; color: %4; padding: 3px 14px; border: 1px solid %9; border-bottom: 0; min-width: 90px; }
QTabBar::tab:selected { background: %7; color: white; }
QTabBar::tab:!selected:hover { background: %10; }
QTabWidget::pane { border: 1px solid %9; }
QSplitter::handle { background: %2; }
QSplitter::handle:horizontal { width: 3px; }
QSplitter::handle:vertical { height: 3px; }
QMainWindow::separator { background: %2; width: 3px; height: 3px; }
)");
    return css.arg(s.window.name(), s.dark.name(), s.base.name(), s.text.name(), s.muted.name(), s.button.name(), hl)
        .arg(border, hover, pressed) // %9 %10 %11
        .arg(dark ? s.text.darker(120).name() : s.button.lighter(110).name()); // %12
}

void apply(int index) {
    const Scheme& s = scheme(index);
    QPalette p;
    p.setColor(QPalette::Window, s.window);
    p.setColor(QPalette::WindowText, s.text);
    p.setColor(QPalette::Base, s.base);
    p.setColor(QPalette::AlternateBase, s.base.darker(108));
    p.setColor(QPalette::Text, s.text);
    p.setColor(QPalette::Button, s.button);
    p.setColor(QPalette::ButtonText, s.text);
    p.setColor(QPalette::Highlight, s.accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, s.dark);
    p.setColor(QPalette::ToolTipText, s.text);
    p.setColor(QPalette::PlaceholderText, s.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, s.muted);
    p.setColor(QPalette::Disabled, QPalette::Text, s.muted);
    qApp->setPalette(p);
    qApp->setStyleSheet(styleSheet(s));
}

// ----------------------------------------------------------------- 아이콘
namespace {
using P = QPainterPath;

void line(QPainterPath& p, double x1, double y1, double x2, double y2) {
    p.moveTo(x1, y1);
    p.lineTo(x2, y2);
}

// 24x24 좌표계에서 윤곽선(stroke) / 채움(fill) 경로를 만든다.
struct Glyph {
    QPainterPath stroke, fill;
};

Glyph makeGlyph(const QString& n) {
    Glyph g;
    P& s = g.stroke;
    P& f = g.fill;
    if (n == "pen") { // 곡선 위에 펜촉
        s.moveTo(3, 19);
        s.cubicTo(7, 9, 10, 20, 15, 10);
        f.addPolygon(QPolygonF({{15, 10}, {19, 3}, {21.5, 5}, {17, 12}}));
    } else if (n == "line") {
        line(s, 4, 20, 20, 4);
        s.addEllipse(QPointF(4, 20), 1.6, 1.6);
        s.addEllipse(QPointF(20, 4), 1.6, 1.6);
    } else if (n == "rect") {
        s.addRect(4, 6, 16, 12);
    } else if (n == "ellipse") {
        s.addEllipse(QPointF(12, 12), 8, 6.5);
    } else if (n == "spline") {
        s.moveTo(3, 18);
        s.cubicTo(5, 4, 12, 4, 12, 12);
        s.cubicTo(12, 20, 19, 20, 21, 6);
        s.addRect(2, 17, 2.4, 2.4);
        s.addRect(19.8, 4.8, 2.4, 2.4);
    } else if (n == "fill") { // 페인트통
        f.addPolygon(QPolygonF({{6, 11}, {12, 5}, {18, 11}, {12, 17}}));
        s.addPolygon(QPolygonF({{6, 11}, {12, 5}, {18, 11}, {12, 17}}));
        s.moveTo(19.5, 14);
        s.cubicTo(21, 17, 21, 19, 19.5, 19.5);
        s.cubicTo(18, 19, 18, 17, 19.5, 14);
    } else if (n == "select") {
        s.addRect(4, 4, 16, 16);
        s.moveTo(9, 9);
        s.lineTo(9, 19);
        s.lineTo(12, 16);
        s.lineTo(16, 20);
    } else if (n == "rig") { // 핀
        s.addEllipse(QPointF(12, 9), 4.5, 4.5);
        line(s, 12, 13.5, 12, 21);
        f.addEllipse(QPointF(12, 9), 1.8, 1.8);
    } else if (n == "crop") {
        line(s, 7, 3, 7, 17);
        line(s, 7, 17, 21, 17);
        line(s, 3, 7, 17, 7);
        line(s, 17, 7, 17, 21);
    } else if (n == "transform") {
        s.addRect(6, 6, 12, 12);
        for (double x : {6.0, 12.0, 18.0})
            for (double y : {6.0, 12.0, 18.0})
                if (x != 12.0 || y != 12.0) f.addRect(x - 1.6, y - 1.6, 3.2, 3.2);
    } else if (n == "camera") {
        s.addRoundedRect(QRectF(3, 7, 13, 11), 2, 2);
        s.moveTo(16, 11);
        s.lineTo(21, 8);
        s.lineTo(21, 17);
        s.lineTo(16, 14);
    } else if (n == "clear") { // 지우개
        f.addPolygon(QPolygonF({{4, 15}, {12, 6}, {20, 12}, {13, 20}, {7, 20}}));
        s.addPolygon(QPolygonF({{4, 15}, {12, 6}, {20, 12}, {13, 20}, {7, 20}}));
    } else if (n == "smooth") {
        s.moveTo(3, 16);
        s.cubicTo(7, 4, 10, 22, 14, 11);
        s.cubicTo(16, 6, 19, 8, 21, 12);
    } else if (n == "history") { // 시계 화살표
        s.addEllipse(QPointF(12, 12), 8, 8);
        s.moveTo(12, 7);
        s.lineTo(12, 12);
        s.lineTo(16, 14);
    } else if (n == "paper") {
        s.moveTo(6, 3);
        s.lineTo(15, 3);
        s.lineTo(19, 7);
        s.lineTo(19, 21);
        s.lineTo(6, 21);
        s.closeSubpath();
        s.moveTo(15, 3);
        s.lineTo(15, 7);
        s.lineTo(19, 7);
    } else if (n == "stencil") {
        s.addRect(4, 4, 16, 16);
        s.setFillRule(Qt::OddEvenFill);
        P hole;
        hole.addEllipse(QPointF(12, 12), 4.5, 4.5);
        s.addPath(hole);
    } else if (n == "new") {
        s.moveTo(6, 3);
        s.lineTo(14, 3);
        s.lineTo(19, 8);
        s.lineTo(19, 21);
        s.lineTo(6, 21);
        s.closeSubpath();
        line(s, 12.5, 11, 12.5, 18);
        line(s, 9, 14.5, 16, 14.5);
    } else if (n == "open") {
        s.moveTo(3, 19);
        s.lineTo(3, 6);
        s.lineTo(9, 6);
        s.lineTo(11, 8.5);
        s.lineTo(19, 8.5);
        s.lineTo(19, 11);
        s.moveTo(3, 19);
        s.lineTo(6, 11);
        s.lineTo(22, 11);
        s.lineTo(19, 19);
        s.closeSubpath();
    } else if (n == "save") {
        s.moveTo(4, 4);
        s.lineTo(17, 4);
        s.lineTo(20, 7);
        s.lineTo(20, 20);
        s.lineTo(4, 20);
        s.closeSubpath();
        s.addRect(7, 4, 8, 5);
        s.addRect(7, 13, 10, 7);
    } else if (n == "undo" || n == "redo") {
        const bool r = n == "redo";
        const double m = r ? -1 : 1;
        auto X = [&](double x) { return r ? 24 - x : x; };
        Q_UNUSED(m);
        s.moveTo(X(6), 10);
        s.lineTo(X(10), 6);
        s.moveTo(X(6), 10);
        s.lineTo(X(10), 14);
        s.moveTo(X(6), 10);
        s.lineTo(X(15), 10);
        s.cubicTo(X(20), 10, X(20), 19, X(14), 19);
        s.lineTo(X(9), 19);
    } else if (n == "help") {
        s.addEllipse(QPointF(12, 12), 9, 9);
        s.moveTo(9.3, 9.5);
        s.cubicTo(9.3, 5.8, 15, 5.8, 14.6, 9.6);
        s.cubicTo(14.4, 11.5, 12, 11.6, 12, 14);
        f.addEllipse(QPointF(12, 17.4), 1.2, 1.2);
    } else if (n == "play") {
        f.addPolygon(QPolygonF({{8, 5}, {19, 12}, {8, 19}}));
    } else if (n == "stop") {
        f.addRect(6, 6, 12, 12);
    } else if (n == "first") {
        f.addRect(5, 5, 2.5, 14);
        f.addPolygon(QPolygonF({{19, 5}, {19, 19}, {9, 12}}));
    } else if (n == "last") {
        f.addRect(16.5, 5, 2.5, 14);
        f.addPolygon(QPolygonF({{5, 5}, {5, 19}, {15, 12}}));
    } else if (n == "prev") {
        f.addPolygon(QPolygonF({{16, 5}, {16, 19}, {6, 12}}));
    } else if (n == "next") {
        f.addPolygon(QPolygonF({{8, 5}, {8, 19}, {18, 12}}));
    } else if (n == "prevkey") {
        f.addPolygon(QPolygonF({{11, 6}, {11, 18}, {3, 12}}));
        f.addPolygon(QPolygonF({{17, 6}, {21, 12}, {17, 18}, {13, 12}}));
    } else if (n == "nextkey") {
        f.addPolygon(QPolygonF({{13, 6}, {13, 18}, {21, 12}}));
        f.addPolygon(QPolygonF({{7, 6}, {11, 12}, {7, 18}, {3, 12}}));
    } else if (n == "sound") {
        f.addPolygon(QPolygonF({{4, 9}, {8, 9}, {13, 5}, {13, 19}, {8, 15}, {4, 15}}));
        s.moveTo(16, 9);
        s.cubicTo(18, 11, 18, 13, 16, 15);
        s.moveTo(18.5, 6.5);
        s.cubicTo(22, 10, 22, 14, 18.5, 17.5);
    } else if (n == "loop") {
        s.moveTo(5, 11);
        s.cubicTo(5, 6, 15, 5, 19, 9);
        s.moveTo(19, 13);
        s.cubicTo(19, 18, 9, 19, 5, 15);
        f.addPolygon(QPolygonF({{19, 5}, {19, 10}, {14, 9.5}}));
        f.addPolygon(QPolygonF({{5, 19}, {5, 14}, {10, 14.5}}));
    } else if (n == "hand") {
        s.moveTo(8, 12);
        s.lineTo(8, 5);
        s.moveTo(11, 11);
        s.lineTo(11, 3.5);
        s.moveTo(14, 11);
        s.lineTo(14, 4.5);
        s.moveTo(17, 12);
        s.lineTo(17, 7);
        s.moveTo(8, 12);
        s.cubicTo(5, 11, 4, 13, 6, 16);
        s.lineTo(9, 20);
        s.lineTo(16, 20);
        s.cubicTo(19, 17, 17, 12, 17, 12);
    } else if (n == "eye") {
        s.moveTo(2.5, 12);
        s.cubicTo(7, 4, 17, 4, 21.5, 12);
        s.cubicTo(17, 20, 7, 20, 2.5, 12);
        f.addEllipse(QPointF(12, 12), 3, 3);
    } else if (n == "lock") {
        s.addRect(6, 11, 12, 9);
        s.moveTo(8.5, 11);
        s.lineTo(8.5, 8);
        s.cubicTo(8.5, 3, 15.5, 3, 15.5, 8);
        s.lineTo(15.5, 11);
    } else if (n == "light") { // 전구
        s.addEllipse(QPointF(12, 10), 6, 6);
        line(s, 9.5, 18, 14.5, 18);
        line(s, 10, 21, 14, 21);
    } else if (n == "layers") {
        s.addPolygon(QPolygonF({{12, 4}, {21, 9}, {12, 14}, {3, 9}}));
        s.moveTo(3, 13);
        s.lineTo(12, 18);
        s.lineTo(21, 13);
    } else if (n == "palette") {
        s.addEllipse(QPointF(12, 12), 9, 8);
        f.addEllipse(QPointF(8, 11), 1.5, 1.5);
        f.addEllipse(QPointF(12, 7.5), 1.5, 1.5);
        f.addEllipse(QPointF(16, 11), 1.5, 1.5);
    } else if (n == "brush") {
        s.moveTo(4, 20);
        s.cubicTo(5, 15, 9, 14, 10, 15);
        s.cubicTo(11, 17, 8, 21, 4, 20);
        line(s, 11, 14, 20, 4);
    } else if (n == "timeline") {
        s.addRect(3, 5, 18, 14);
        line(s, 3, 10, 21, 10);
        line(s, 9, 5, 9, 19);
    } else if (n == "library") {
        s.addRect(4, 4, 7, 7);
        s.addRect(13, 4, 7, 7);
        s.addRect(4, 13, 7, 7);
        s.addRect(13, 13, 7, 7);
    } else if (n == "fx") {
        s.moveTo(12, 3);
        s.lineTo(14, 10);
        s.lineTo(21, 12);
        s.lineTo(14, 14);
        s.lineTo(12, 21);
        s.lineTo(10, 14);
        s.lineTo(3, 12);
        s.lineTo(10, 10);
        s.closeSubpath();
    } else if (n == "navigator") {
        s.addEllipse(QPointF(12, 12), 9, 9);
        f.addPolygon(QPolygonF({{12, 5}, {15, 12}, {12, 19}, {9, 12}}));
    } else if (n == "guides") {
        for (double v : {8.0, 16.0}) {
            line(s, v, 3, v, 21);
            line(s, 3, v, 21, v);
        }
    } else if (n == "remote") {
        s.addRoundedRect(QRectF(8, 3, 8, 18), 2, 2);
        f.addEllipse(QPointF(12, 8), 1.8, 1.8);
        f.addEllipse(QPointF(12, 14), 1.8, 1.8);
    } else if (n == "share") {
        s.addEllipse(QPointF(6, 12), 2.6, 2.6);
        s.addEllipse(QPointF(18, 6), 2.6, 2.6);
        s.addEllipse(QPointF(18, 18), 2.6, 2.6);
        line(s, 8.3, 10.8, 15.7, 7.2);
        line(s, 8.3, 13.2, 15.7, 16.8);
    } else if (n == "center") {
        s.addEllipse(QPointF(12, 12), 6, 6);
        line(s, 12, 2, 12, 6);
        line(s, 12, 18, 12, 22);
        line(s, 2, 12, 6, 12);
        line(s, 18, 12, 22, 12);
    } else if (n == "zoom") {
        s.addEllipse(QPointF(10, 10), 6, 6);
        line(s, 14.5, 14.5, 20, 20);
    } else if (n == "plus") {
        line(s, 12, 5, 12, 19);
        line(s, 5, 12, 19, 12);
    } else if (n == "folder") {
        s.moveTo(3, 18);
        s.lineTo(3, 6);
        s.lineTo(9, 6);
        s.lineTo(11, 8);
        s.lineTo(21, 8);
        s.lineTo(21, 18);
        s.closeSubpath();
    } else if (n == "script") {
        s.addRect(3, 4, 18, 16);
        s.moveTo(7, 9);
        s.lineTo(10, 12);
        s.lineTo(7, 15);
        line(s, 12, 15, 17, 15);
    } else if (n == "storyboard") {
        s.addRect(3, 4, 8, 7);
        s.addRect(13, 4, 8, 7);
        s.addRect(3, 13, 8, 7);
        s.addRect(13, 13, 8, 7);
    } else if (n == "timesheet") {
        s.addRect(4, 3, 16, 18);
        for (double y : {8.0, 13.0, 18.0}) line(s, 4, y, 20, y);
        line(s, 12, 3, 12, 21);
    } else if (n == "camera2") {
        s.addRect(3, 6, 18, 12);
        s.addRect(7, 9, 10, 6);
    }
    return g;
}
} // namespace

QIcon icon(const QString& name, int px, const QColor& color) {
    static QHash<QString, QIcon> cache;
    const QString key = QString("%1|%2|%3").arg(name).arg(px).arg(color.rgba(), 0, 16);
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();
    const Glyph g = makeGlyph(name);
    QIcon ic;
    if (!g.stroke.isEmpty() || !g.fill.isEmpty()) {
        const qreal dpr = 2.0;
        QPixmap pm(int(px * dpr), int(px * dpr));
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(px * dpr / 24.0, px * dpr / 24.0);
        QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(g.stroke);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawPath(g.fill);
        p.end();
        pm.setDevicePixelRatio(dpr);
        ic.addPixmap(pm);
        // 비활성/체크 상태는 Qt 가 알아서 처리
    }
    cache.insert(key, ic);
    return ic;
}


// ----------------------------------------------------------------- 모션
namespace {
QColor mix(const QColor& a, const QColor& b, qreal t) {
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

// 버튼 하나의 호버/눌림/체크 전환값(0~1)을 부드럽게 움직인다.
class ButtonMotion : public QObject {
public:
    explicit ButtonMotion(QAbstractButton* b) : QObject(b), btn_(b) {
        b->installEventFilter(this);
        connect(b, &QAbstractButton::toggled, this, [this](bool on) { go(2, on ? 1.0 : 0.0, 160); });
        t_[2] = b->isChecked() ? 1.0 : 0.0;
        b->setAttribute(Qt::WA_Hover, true);
    }
    qreal hover() const { return t_[0]; }
    qreal press() const { return t_[1]; }
    qreal check() const { return t_[2]; }
    bool eventFilter(QObject* o, QEvent* e) override {
        switch (e->type()) {
        case QEvent::HoverEnter:
        case QEvent::Enter: go(0, 1.0, 120); break;
        case QEvent::HoverLeave:
        case QEvent::Leave: go(0, 0.0, 200); go(1, 0.0, 120); break;
        case QEvent::MouseButtonPress: go(1, 1.0, 70); break;
        case QEvent::MouseButtonRelease: go(1, 0.0, 160); break;
        case QEvent::EnabledChange: btn_->update(); break;
        default: break;
        }
        return QObject::eventFilter(o, e);
    }
private:
    void go(int i, qreal to, int ms) {
        if (anim_[i]) anim_[i]->stop();
        if (!anim_[i]) {
            anim_[i] = new QVariantAnimation(this);
            connect(anim_[i], &QVariantAnimation::valueChanged, this, [this, i](const QVariant& v) {
                t_[i] = v.toReal();
                btn_->update();
            });
        }
        anim_[i]->setDuration(ms);
        anim_[i]->setEasingCurve(QEasingCurve::OutCubic);
        anim_[i]->setStartValue(t_[i]);
        anim_[i]->setEndValue(to);
        anim_[i]->start();
    }
    QAbstractButton* btn_;
    qreal t_[3] = {0, 0, 0};
    QVariantAnimation* anim_[3] = {nullptr, nullptr, nullptr};
};

ButtonMotion* motionOf(const QWidget* w) {
    if (!w) return nullptr;
    return w->findChild<ButtonMotion*>(QString(), Qt::FindDirectChildrenOnly);
}

// 앱 전체 이벤트 필터: 부드러운 휠 스크롤, 대화상자/메뉴 페이드 인
// Save / Cancel 같은 표준 버튼의 영어 글자를 한국어로 바꾼다.
static void koreanize(QDialogButtonBox* bb) {
    static const QList<QPair<QDialogButtonBox::StandardButton, const char*>> map = {
        {QDialogButtonBox::Ok, "확인"},          {QDialogButtonBox::Cancel, "취소"},
        {QDialogButtonBox::Save, "저장"},        {QDialogButtonBox::Discard, "저장 안 함"},
        {QDialogButtonBox::Open, "열기"},        {QDialogButtonBox::Close, "닫기"},
        {QDialogButtonBox::Yes, "예"},           {QDialogButtonBox::No, "아니요"},
        {QDialogButtonBox::YesToAll, "모두 예"}, {QDialogButtonBox::NoToAll, "모두 아니요"},
        {QDialogButtonBox::Apply, "적용"},       {QDialogButtonBox::Reset, "초기화"},
        {QDialogButtonBox::RestoreDefaults, "기본값으로"}, {QDialogButtonBox::Help, "도움말"},
        {QDialogButtonBox::SaveAll, "모두 저장"}, {QDialogButtonBox::Abort, "중단"},
        {QDialogButtonBox::Retry, "다시 시도"},  {QDialogButtonBox::Ignore, "무시"},
    };
    for (const auto& m : map)
        if (QPushButton* b = bb->button(m.first)) b->setText(QString::fromUtf8(m.second));
}

class MotionFilter : public QObject {
public:
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() == QEvent::Wheel) {
            auto* vp = qobject_cast<QWidget*>(o);
            auto* area = vp ? qobject_cast<QScrollArea*>(vp->parentWidget()) : nullptr;
            if (area && area->viewport() == vp) {
                auto* we = static_cast<QWheelEvent*>(e);
                if (we->modifiers() & Qt::ControlModifier) return false;
                QScrollBar* bar = area->verticalScrollBar()->maximum() > 0 ? area->verticalScrollBar() : area->horizontalScrollBar();
                if (bar->maximum() <= 0) return false;
                int delta = we->pixelDelta().isNull() ? -we->angleDelta().y() / 120 * 70 : -we->pixelDelta().y();
                if (we->angleDelta().y() == 0 && we->angleDelta().x() != 0) delta = -we->angleDelta().x() / 120 * 70;
                auto* anim = bar->findChild<QPropertyAnimation*>("onyScroll");
                if (!anim) {
                    anim = new QPropertyAnimation(bar, "value", bar);
                    anim->setObjectName("onyScroll");
                    anim->setEasingCurve(QEasingCurve::OutCubic);
                }
                const int base = anim->state() == QAbstractAnimation::Running ? anim->endValue().toInt() : bar->value();
                const int target = std::clamp(base + delta, bar->minimum(), bar->maximum());
                anim->stop();
                anim->setDuration(170);
                anim->setStartValue(bar->value());
                anim->setEndValue(target);
                anim->start();
                return true;
            }
        } else if (e->type() == QEvent::Show) {
            if (auto* bb = qobject_cast<QDialogButtonBox*>(o)) koreanize(bb);
            if (auto* w = qobject_cast<QWidget*>(o)) {
                if ((qobject_cast<QDialog*>(w) || qobject_cast<QMenu*>(w)) && w->isWindow() && !w->property("onyFaded").toBool()) {
                    w->setProperty("onyFaded", true);
                    w->setWindowOpacity(0.0);
                    auto* a = new QPropertyAnimation(w, "windowOpacity", w);
                    a->setDuration(qobject_cast<QMenu*>(w) ? 110 : 170);
                    a->setStartValue(0.0);
                    a->setEndValue(1.0);
                    a->setEasingCurve(QEasingCurve::OutCubic);
                    a->start(QAbstractAnimation::DeleteWhenStopped);
                    connect(a, &QPropertyAnimation::finished, w, [w] { w->setProperty("onyFaded", false); });
                }
            }
        }
        return false;
    }
};

class OnyStyle : public QProxyStyle {
public:
    OnyStyle() : QProxyStyle("Fusion") {}

    void polish(QWidget* w) override {
        QProxyStyle::polish(w);
        if (auto* b = qobject_cast<QAbstractButton*>(w))
            if (!motionOf(b)) new ButtonMotion(b);
    }
    void unpolish(QWidget* w) override {
        if (auto* b = qobject_cast<QAbstractButton*>(w))
            if (auto* m = motionOf(b)) delete m;
        QProxyStyle::unpolish(w);
    }

    void drawPrimitive(PrimitiveElement pe, const QStyleOption* opt, QPainter* p, const QWidget* w) const override {
        if (pe == PE_PanelButtonTool || pe == PE_PanelButtonCommand) {
            if (qobject_cast<const QAbstractButton*>(w) && !(opt->state & State_Sunken && false)) {
                drawButton(opt, p, w, pe == PE_PanelButtonTool);
                return;
            }
        }
        QProxyStyle::drawPrimitive(pe, opt, p, w);
    }

    void drawControl(ControlElement ce, const QStyleOption* opt, QPainter* p, const QWidget* w) const override {
        if (ce == CE_ToolButtonLabel || ce == CE_PushButtonLabel) {
            // 누르는 동안 내용이 살짝 눌려 들어가는 느낌
            if (auto* m = motionOf(w)) {
                if (m->press() > 0.01) {
                    p->save();
                    const qreal t = m->press();
                    const QPointF c = QRectF(opt->rect).center();
                    p->translate(c.x(), c.y() + 0.8 * t);
                    p->scale(1.0 - 0.06 * t, 1.0 - 0.06 * t);
                    p->translate(-c.x(), -c.y());
                    QProxyStyle::drawControl(ce, opt, p, w);
                    p->restore();
                    return;
                }
            }
        }
        QProxyStyle::drawControl(ce, opt, p, w);
    }

private:
    void drawButton(const QStyleOption* opt, QPainter* p, const QWidget* w, bool tool) const {
        auto* m = motionOf(w);
        const qreal hov = m ? m->hover() : (opt->state & State_MouseOver ? 1 : 0);
        const qreal prs = m ? m->press() : (opt->state & State_Sunken ? 1 : 0);
        const qreal chk = m ? m->check() : (opt->state & State_On ? 1 : 0);
        const QPalette& pal = opt->palette;
        const QColor win = pal.color(QPalette::Window);
        const bool dark = win.lightness() < 128;
        const QColor base = pal.color(QPalette::Button);
        const QColor accent = pal.color(QPalette::Highlight);
        const bool flat = tool && w && (qobject_cast<QToolBar*>(w->parentWidget()) || static_cast<const QToolButton*>(w)->autoRaise());
        const bool enabled = opt->state & State_Enabled;

        QColor fill = flat ? mix(Qt::transparent, dark ? base.lighter(120) : base.darker(106), hov) : mix(base, dark ? base.lighter(122) : base.darker(106), hov);
        if (chk > 0.001) {
            const QColor on = flat ? (dark ? base.darker(115) : base.darker(108)) : accent;
            fill = mix(fill, on, chk);
        }
        fill = mix(fill, dark ? fill.darker(125) : fill.darker(112), prs);
        QColor border = mix(flat ? QColor(Qt::transparent) : (dark ? base.darker(160) : base.darker(130)),
                            dark ? base.darker(160) : base.darker(130), flat ? std::max(hov, chk) : 1.0);
        if (chk > 0.001 && !flat) border = mix(border, accent.darker(115), chk);
        if (!enabled) {
            fill.setAlphaF(fill.alphaF() * 0.55);
            border.setAlphaF(border.alphaF() * 0.55);
        }
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(opt->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->setBrush(fill);
        p->setPen(QPen(border, 1));
        p->drawRoundedRect(r, 3, 3);
        if (flat && chk > 0.001) { // 체크된 툴바 버튼: 아래쪽 강조선
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(accent.red(), accent.green(), accent.blue(), int(255 * chk)));
            p->drawRoundedRect(QRectF(r.left() + 3, r.bottom() - 1.8, r.width() - 6, 2.2), 1, 1);
        }
        if (!(prs > 0.02) && hov > 0.001 && !flat) { // 위쪽 하이라이트
            p->setPen(QColor(255, 255, 255, int((dark ? 22 : 40) * hov)));
            p->drawLine(QPointF(r.left() + 3, r.top() + 1), QPointF(r.right() - 3, r.top() + 1));
        }
        if (opt->state & State_HasFocus && opt->state & State_KeyboardFocusChange) {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(accent, 1.2));
            p->drawRoundedRect(r.adjusted(1.5, 1.5, -1.5, -1.5), 2, 2);
        }
        p->restore();
    }
};
} // namespace

QStyle* createStyle() { return new OnyStyle; }

void installMotion() {
    static MotionFilter* f = nullptr;
    if (!f) {
        f = new MotionFilter;
        qApp->installEventFilter(f);
    }
}

} // namespace Style
