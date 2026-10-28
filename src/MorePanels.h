#pragma once
#include <QDockWidget>
#include <QHash>
#include <QImage>
#include <QWidget>

class CanvasWidget;
class Document;

// 패널 제목줄 오른쪽의 세 단추: ● 위치 고정, ▭ 접기, ✕ 닫기
class DockTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit DockTitleBar(QDockWidget* dock);
    QSize sizeHint() const override { return QSize(120, 22); }
    QSize minimumSizeHint() const override { return QSize(60, 22); }
    // 오른쪽 단추 세 개(고정 / 접기 / 닫기)의 자리. 그리기와 누르기가 같은 값을 쓴다.
    static constexpr int kBtn = 18, kBtnCount = 3, kPad = 5;
    QRect buttonRect(int i) const;
    int buttonAt(const QPoint& pos) const;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    QDockWidget* dock_;
    bool locked_ = false;
    bool collapsed_ = false;
    int hot_ = -1;
};

// 내비게이터: 프로젝트 전체 미리보기 + 현재 보이는 영역. 클릭/드래그로 화면 이동, 슬라이더로 확대.
class NavigatorPanel : public QWidget {
    Q_OBJECT
public:
    NavigatorPanel(Document* doc, CanvasWidget* canvas, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
private:
    QRectF thumbRect() const;
    void moveTo(const QPoint& pos);
    Document* doc_;
    CanvasWidget* canvas_;
};

// 플립 패널: 프레임을 빠르게 넘겨 보며 움직임을 확인한다 (슬라이더 드래그 / 자동 플립)
class FlipPanel : public QWidget {
    Q_OBJECT
public:
    FlipPanel(Document* doc, QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
private:
    void scrubTo(const QPoint& pos);
    void tick();
    QImage frameImage(int f);
    Document* doc_;
    class QSpinBox* from_;
    class QSpinBox* to_;
    class QSpinBox* fps_;
    class QComboBox* mode_;
    class QPushButton* play_;
    class QTimer* timer_;
    int flipFrame_ = 0;
    int dir_ = 1;
    QRect previewRect() const;
    QHash<int, QImage> cache_;
    qint64 cacheStamp_ = -1;
};

// 가이드라인 패널
class GuidelinesPanel : public QWidget {
    Q_OBJECT
public:
    explicit GuidelinesPanel(CanvasWidget* canvas, QWidget* parent = nullptr);
};

// 라이트 테이블 패널
class LightTablePanel : public QWidget {
    Q_OBJECT
public:
    explicit LightTablePanel(CanvasWidget* canvas, QWidget* parent = nullptr);
signals:
    void enabledChanged(bool on);
};

// 리모컨 패널: 재생/이동 단축 버튼
class RemotePanel : public QWidget {
    Q_OBJECT
public:
    explicit RemotePanel(QWidget* parent = nullptr);
signals:
    void play();
    void stop();
    void first();
    void last();
    void prevFrame();
    void nextFrame();
    void prevImage();
    void nextImage();
};

// 공유 관리자: 내보내기 프리셋 목록
class ShareManagerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ShareManagerPanel(QWidget* parent = nullptr);
signals:
    void exportCurrentPng();
    void exportVideo();
    void exportPngSequence();
    void exportTimesheetPdf();
    void exportXdts();
};

#include "Brush.h"
class QListWidget;
// 브러시 패널: 프리셋마다 샘플 스트로크 미리보기를 보여준다.
class BrushPanel : public QWidget {
    Q_OBJECT
public:
    explicit BrushPanel(QWidget* parent = nullptr);
    void setCurrent(int row);
    void setColor(const QColor& c);
signals:
    void presetSelected(int row);
protected:
    void resizeEvent(QResizeEvent*) override;
private:
    void rebuild();
    void reflow();
    int cols_ = 3;
    QList<QPair<class QGridLayout*, QList<class QToolButton*>>> grids_;
    QList<class QToolButton*> buttons_;
    QColor color_{0x22, 0x22, 0x22};
};
