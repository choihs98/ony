#pragma once
#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QVector>
#include <QWidget>

class Document;

// 타임라인: 왼쪽 레이어 머리글(눈/잠금/썸네일/이름) + 프레임 칸(키 이미지 썸네일)
class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int kCell = 40;
    static constexpr int kHeader = 22;
    static constexpr int kRowH = 54;
    static constexpr int kRowCompact = 24;
    static constexpr int kNameW = 280;

    explicit TimelineWidget(Document* doc, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void clearRange();
    void setRange(int a, int b) { rangeA_ = a; rangeB_ = b; update(); emit rangeChanged(a, b); }
    int cell() const { return cellW_; }
    int rowHeight() const { return rowH_; }
    bool compact() const { return compact_; }
    void setCellWidth(int w);
    void setCompact(bool on);
    int rangeStart() const { return rangeA_; }
    int rangeEnd() const { return rangeB_; }

signals:
    void rangeChanged(int a, int b); // a < 0 이면 구간 없음
    void zoomChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    int cellW_ = kCell;
    int rowH_ = kRowH;
    bool compact_ = false;
    int rowToLayer(int row) const;
    int layerToRow(int layer) const;
    // 프리/포스트 비헤이비어 단추 (which 0 = 첫 이미지 앞, 1 = 마지막 이미지 뒤). 자리가 없으면 빈 사각형.
    // 레이어의 (첫 이미지, 내용 끝). 한 번 그리는 동안 같은 값을 여러 번 쓰므로 모아 둔다.
    // 문서가 바뀌면(= 다시 그릴 때마다) 비운다.
    QPoint layerRange(int layer) const;
    mutable QHash<int, QPoint> rangeCache_;
    // 레이어 머리글의 눈·잠금 단추 자리 (그리기와 누르기가 같은 값을 쓴다)
    QRect eyeRect(int row) const;
    QRect lockRect(int row) const;
    int headerIconRight() const;
    QRect behaviorButton(int row, int which) const;
    bool behaviorMenuAt(const QPoint& pos);
    // 마우스가 얹힌 비헤이비어 단추 (없으면 -1). 바뀔 때만 다시 그린다.
    void updateHover(const QPoint& pos);
    int hoverRow_ = -1, hoverWhich_ = -1;
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

// 소리 파형 막대: 프로젝트 길이에 맞춰 소리의 크기를 그린다
class WaveformBar : public QWidget {
    Q_OBJECT
public:
    explicit WaveformBar(Document* doc, QWidget* parent = nullptr);
    void setPeaks(const QVector<float>& peaks, const QString& label);
    void clearPeaks();
    bool hasPeaks() const { return !peaks_.isEmpty(); }
    QSize sizeHint() const override { return QSize(200, 34); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    Document* doc_;
    QVector<float> peaks_;
    QString label_;
};
