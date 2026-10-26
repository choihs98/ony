#pragma once
#include <QWidget>

class Document;

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int kCell = 18;
    static constexpr int kHeader = 22;
    static constexpr int kRowH = 24;
    static constexpr int kNameW = 170;

    explicit TimelineWidget(Document* doc, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void clearRange();
    int rangeStart() const { return rangeA_; }
    int rangeEnd() const { return rangeB_; }

signals:
    void rangeChanged(int a, int b); // a < 0 이면 구간 없음

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;

private:
    int rowToLayer(int row) const;
    int layerToRow(int layer) const;
    void pick(const QPoint& pos, bool pressed);

    Document* doc_;
    int rangeA_ = -1, rangeB_ = -1, anchor_ = -1;
    bool rangeDrag_ = false;
};
