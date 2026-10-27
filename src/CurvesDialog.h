#pragma once
#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QVector>
#include <QWidget>

// 색 보정 커브 편집기: 가로 = 입력 밝기, 세로 = 출력 밝기. 점을 끌어 곡선을 만든다.
class CurveWidget : public QWidget {
    Q_OBJECT
public:
    explicit CurveWidget(QWidget* parent = nullptr);
    QVector<int> lut() const;                 // 256칸 보정표
    void setPoints(const QVector<QPointF>& p);
    QVector<QPointF> points() const { return pts_; }
    void reset();
    void setHistogram(const QVector<int>& h) { hist_ = h; update(); }
    QSize sizeHint() const override { return QSize(260, 260); }
signals:
    void curveChanged();
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;
private:
    QPointF toWidget(const QPointF& p) const;
    QPointF toCurve(const QPointF& p) const;
    int pickPoint(const QPointF& widgetPos) const;
    QVector<QPointF> pts_;     // 0~1 범위, x 오름차순
    QVector<int> hist_;
    int drag_ = -1;
};

class Document;
class CanvasWidget;

// 커브 / 레벨 대화상자. 미리보기를 보면서 조절한다.
class ColorAdjustDialog : public QDialog {
    Q_OBJECT
public:
    // src 를 받아 보정 결과를 돌려준다 (확인을 누르면 accepted)
    ColorAdjustDialog(const QImage& src, bool levelsMode, QWidget* parent = nullptr);
    QImage result() const { return result_; }
    // 적용 함수만 받아가고 싶을 때
    QVector<int> lutR() const;
    QVector<int> lutG() const;
    QVector<int> lutB() const;
private:
    void recompute();
    QImage src_, result_;
    bool levelsMode_;
    class QLabel* preview_ = nullptr;
    class QComboBox* channel_ = nullptr;
    CurveWidget* curve_ = nullptr;
    QVector<QPointF> ptsAll_, ptsR_, ptsG_, ptsB_;
    class QSpinBox *inLow_ = nullptr, *inHigh_ = nullptr, *outLow_ = nullptr, *outHigh_ = nullptr;
    class QDoubleSpinBox* gamma_ = nullptr;
};
