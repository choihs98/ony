#pragma once
#include <QHash>
#include <QPixmap>
#include <QWidget>

class Document;

// TVPaint 식 타임라인: 왼쪽 레이어 머리글(눈/잠금/썸네일/이름) + 프레임 칸(키 이미지 썸네일)
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int kCell = 40;
    static constexpr int kHeader = 22;
    static constexpr int kRowH = 54;
    static constexpr int kNameW = 280;

    explicit TimelineWidget(Document* doc, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void clearRange();
    void setRange(int a, int b) { rangeA_ = a; rangeB_ = b; update(); emit rangeChanged(a, b); }
    int rangeStart() const { return rangeA_; }
    int rangeEnd() const { return rangeB_; }

signals:
    void rangeChanged(int a, int b); // a < 0 이면 구간 없음

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;

private:
    int rowToLayer(int row) const;
    int layerToRow(int layer) const;
    void pick(const QPoint& pos, bool pressed);
    const QPixmap& thumb(const QImage& img, QSize box);

    Document* doc_;
    int rangeA_ = -1, rangeB_ = -1, anchor_ = -1;
    bool rangeDrag_ = false;
    QHash<qint64, QPixmap> thumbs_;
};

// 캔버스 아래의 프레임 눈금 막대: 클릭/드래그로 재생 헤드 이동, 루프 구간 표시
class ScrubBar : public QWidget {
    Q_OBJECT
public:
    explicit ScrubBar(Document* doc, QWidget* parent = nullptr);
    void setRange(int a, int b) { rangeA_ = a; rangeB_ = b; update(); }
    QSize sizeHint() const override { return QSize(200, 22); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
private:
    int frameAt(int x) const;
    Document* doc_;
    int rangeA_ = -1, rangeB_ = -1;
};
