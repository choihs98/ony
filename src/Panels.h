#pragma once
#include <QColor>
#include <QTimer>
#include <QWidget>

#include "Brush.h"

class QToolButton;
class QSlider;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QTableWidget;
class Document;

// 메인 패널: 도구 선택(1구역) + 단축 아이콘(2구역)
class MainPanel : public QWidget {
    Q_OBJECT
public:
    explicit MainPanel(QWidget* parent = nullptr);
signals:
    void toolSelected(int toolId);
    void cameraToggled(bool on);
    void clearRequested();
    void smoothingToggled(bool on);
    void paperToggled(bool on);
    void stencilToggled(bool on);
    void historyRequested(const QPoint& globalPos);
};

// 색상 패널: A/B 색상 + 견본
class ColorPanel : public QWidget {
    Q_OBJECT
public:
    explicit ColorPanel(QWidget* parent = nullptr);
    QColor colorA() const { return a_; }
    QColor colorB() const { return b_; }
    void setColorA(const QColor& c);
    void swapColors();
signals:
    void colorAChanged(const QColor&);
private:
    void refresh();
    QColor a_{Qt::black}, b_{Qt::white};
    QToolButton* btnA_;
    QToolButton* btnB_;
};

// 도구 패널: 브러시 프리셋 + 크기/불투명도/경도/필압
class ToolPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolPanel(QWidget* parent = nullptr);
    BrushParams params() const;
    void setParams(const BrushParams& p);
    void setRow(int row);
    void selectPreset(int row);
signals:
    void brushSelected(int row);
    void paramsChanged(const BrushParams& p);
private:
    QListWidget* list_;
    QSlider* size_;
    QSlider* opacity_;
    QSlider* hardness_;
    QCheckBox* pressure_;
    QLabel* sizeL_;
    QLabel* opacityL_;
    QLabel* hardnessL_;
    void emitParams();
};

// 레이어 스택
class LayerStackPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayerStackPanel(Document* doc, QWidget* parent = nullptr);
private:
    void rebuild();
    void syncSelection();
    Document* doc_;
    QListWidget* list_;
    QSlider* opacity_;
};

// 타임시트: 행 = 프레임, 열 = 레이어
class TimesheetPanel : public QWidget {
    Q_OBJECT
public:
    explicit TimesheetPanel(Document* doc, QWidget* parent = nullptr);
private:
    void rebuild();
    Document* doc_;
    QTableWidget* table_;
    QTimer debounce_;
    bool syncing_ = false;
};

// FX 스택: 선택한 레이어에 비파괴 효과를 쌓는다
class FxPanel : public QWidget {
    Q_OBJECT
public:
    explicit FxPanel(Document* doc, QWidget* parent = nullptr);
private:
    void rebuild();
    Document* doc_;
    QLabel* title_;
    QListWidget* list_;
    QComboBox* type_;
    QSpinBox* param_;
};

// 카메라: 키 기반 2D 카메라
class CameraPanel : public QWidget {
    Q_OBJECT
public:
    explicit CameraPanel(Document* doc, QWidget* parent = nullptr);
    void setEnabledChecked(bool on);
private:
    void sync();
    void apply();
    Document* doc_;
    QCheckBox* enable_;
    QDoubleSpinBox *x_, *y_, *scale_, *rot_;
    QLabel* info_;
    bool syncing_ = false;
};

// 이미지 라이브러리
class LibraryPanel : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPanel(Document* doc, QWidget* parent = nullptr);
private:
    void rebuild();
    Document* doc_;
    QListWidget* list_;
};

class PlaceholderPanel : public QWidget {
    Q_OBJECT
public:
    explicit PlaceholderPanel(const QString& note, QWidget* parent = nullptr);
};
