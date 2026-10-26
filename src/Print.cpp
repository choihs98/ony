#include "Print.h"
#include "Document.h"

#include <QFont>
#include <QPageLayout>
#include <QPageSize>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QPdfWriter>
#include <algorithm>
#include <cmath>

namespace Printing {

static void clampRange(const Document* doc, const PrintOptions& o, int& a, int& b) {
    const int n = doc->frameCount();
    a = std::clamp(o.from, 0, n - 1);
    b = std::clamp(o.to < 0 ? n - 1 : o.to, a, n - 1);
}

int pageCount(const Document* doc, const PrintOptions& o) {
    int a, b;
    clampRange(doc, o, a, b);
    const int per = std::max(1, o.cols) * std::max(1, o.rows);
    return (b - a + 1 + per - 1) / per;
}

bool paintAll(QPagedPaintDevice* dev, Document* doc, const PrintOptions& o) {
    int a, b;
    clampRange(doc, o, a, b);
    const int cols = std::clamp(o.cols, 1, 8), rows = std::clamp(o.rows, 1, 12);
    const int per = cols * rows;
    const int pages = pageCount(doc, o);
    QPainter p;
    if (!p.begin(dev)) return false;
    const int W = p.device()->width(), H = p.device()->height();
    const int margin = std::max(8, W / 20);
    QFont f = p.font();
    f.setPixelSize(std::max(8, W / 60));
    p.setFont(f);
    const int head = o.title.isEmpty() ? 0 : f.pixelSize() * 2;
    const int labelH = o.numbers ? int(f.pixelSize() * 1.8) : 0;
    int frame = a;
    for (int pg = 0; pg < pages; ++pg) {
        if (pg > 0) dev->newPage();
        if (!o.title.isEmpty()) {
            p.setPen(Qt::black);
            p.drawText(QRect(margin, margin / 2, W - 2 * margin, head), Qt::AlignVCenter | Qt::AlignLeft,
                       QString("%1   (%2 / %3)").arg(o.title).arg(pg + 1).arg(pages));
        }
        const int gx = margin, gy = margin + head, gw = W - 2 * margin, gh = H - gy - margin;
        const int cw = gw / cols, ch = gh / rows;
        for (int i = 0; i < per && frame <= b; ++i, ++frame) {
            const int cx = gx + (i % cols) * cw, cy = gy + (i / cols) * ch;
            const QRect cell(cx + 4, cy + 4, cw - 8, ch - 8 - labelH);
            const QImage img = doc->renderFrame(frame, Qt::white, o.useCamera);
            const QSize fit = img.size().scaled(cell.size(), Qt::KeepAspectRatio);
            const QRect dst(cell.x() + (cell.width() - fit.width()) / 2, cell.y() + (cell.height() - fit.height()) / 2, fit.width(), fit.height());
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(dst, img);
            p.setPen(QPen(QColor(120, 120, 120), std::max(1, W / 1200)));
            p.setBrush(Qt::NoBrush);
            p.drawRect(dst);
            if (o.numbers) {
                p.setPen(Qt::black);
                p.drawText(QRect(cell.x(), cell.bottom() + 2, cell.width(), labelH), Qt::AlignCenter, QString("%1").arg(frame + 1));
            }
        }
    }
    p.end();
    return true;
}

bool exportPdf(Document* doc, const QString& path, const PrintOptions& o) {
    QPdfWriter w(path);
    w.setPageSize(QPageSize(QPageSize::A4));
    w.setPageOrientation(doc->renderSize().width() > doc->renderSize().height() && o.cols * o.rows <= 2 ? QPageLayout::Landscape : QPageLayout::Portrait);
    w.setResolution(150);
    w.setTitle(o.title);
    return paintAll(&w, doc, o);
}

} // namespace Printing
