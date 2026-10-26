#pragma once
#include <QString>

class Document;
class QPagedPaintDevice;

// 인쇄용 레이아웃: 프레임을 한 장에 하나씩, 또는 콘택트 시트(여러 프레임을 격자로)로 찍는다.
struct PrintOptions {
    int from = 0, to = -1;   // 프레임 범위 (to < 0 이면 끝까지)
    int cols = 3, rows = 4;  // 한 장에 들어갈 격자 (1x1 이면 프레임당 한 장)
    bool numbers = true;     // 프레임 번호 표시
    bool useCamera = false;  // 카메라 시점으로 인쇄
    QString title;           // 쪽 위쪽 제목 (비우면 생략)
};

namespace Printing {
int pageCount(const Document* doc, const PrintOptions& o);
// dev(QPrinter 또는 QPdfWriter)에 모든 쪽을 그린다. 성공하면 true.
bool paintAll(QPagedPaintDevice* dev, Document* doc, const PrintOptions& o);
// PDF 파일로 저장 (A4, 인쇄용)
bool exportPdf(Document* doc, const QString& path, const PrintOptions& o);
}
