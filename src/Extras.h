#pragma once
#include <functional>
#include "Document.h"
#include "Warp.h"
#include <QDialog>
#include <QImage>
#include <QListWidget>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <optional>
#include <QWidget>

class Document;
class QPlainTextEdit;
class QLineEdit;

namespace Extras {

// 스캔 클리너: 스캔한 종이 그림에서 선만 뽑아 투명 배경의 선화로 만든다.
// threshold: 0-255 (이보다 어두운 부분이 선), despeckle: 이 픽셀 수 이하의 작은 얼룩은 지운다.
QImage scanClean(const QImage& scan, int threshold, int despeckle, QColor ink);

// 타임시트 교환 형식(XDTS, 근사 구현) 내보내기
bool exportXdts(const Document* doc, const QString& path, QString* error = nullptr);

}

// 간단한 스크립트 실행기 (TVPaint George 와 호환되지 않는 자체 명령어)
class ScriptEngine {
public:
    explicit ScriptEngine(Document* doc) : doc_(doc) {}
    // 여러 줄 스크립트를 실행하고 출력 로그를 돌려준다.
    QString run(const QString& script);
    static QString help();

    // 문서 밖의 일(선택 영역·색·도구·내보내기 등)을 처리할 함수. 창 쪽에서 붙여 준다.
    // 토큰을 받아 처리했으면 결과 문자열(빈 문자열도 처리한 것으로 봄), 모르는 명령이면 std::nullopt.
    using Host = std::function<std::optional<QString>(const QStringList& tokens)>;
    void setHost(Host h) { host_ = std::move(h); }
    // 미리 넣어 둘 변수 (플러그인 인자 등)
    void setVar(const QString& name, double v) { vars_[name] = v; }
    void setStr(const QString& name, const QString& v) { svars_[name] = v; }
    double var(const QString& name) const { return vars_.value(name, 0.0); }

private:
    // lines[from..to) 를 실행한다. 블록(if/while/for)은 end 까지 묶어서 처리.
    QString runBlock(const QStringList& lines, int from, int to, int depth);
    QString runLine(const QString& line, int depth);
    bool drawOnCurrent(const std::function<void(QImage&)>& fn);
    QString expand(const QString& s) const;      // $이름 을 값으로 바꾼다
    double eval(const QString& expr) const;      // 사칙연산·비교·and/or
    int matchEnd(const QStringList& lines, int start, int to) const;   // 짝이 맞는 end 의 줄 번호
    Document* doc_;
    Host host_;
    QMap<QString, double> vars_;
    QMap<QString, QString> svars_;
    int steps_ = 0;    // 무한 반복 보호
    FrameClip frameClip_;
};

// 스크립트 콘솔 패널
class ScriptConsole : public QWidget {
    Q_OBJECT
public:
    explicit ScriptConsole(Document* doc, QWidget* parent = nullptr);
    void setHost(ScriptEngine::Host h) { host_ = std::move(h); }
private:
    ScriptEngine::Host host_;
    Document* doc_;
    QPlainTextEdit* input_;
    QPlainTextEdit* output_;
};

// 사용자 패널: 이름 + 버튼(라벨 → 스크립트) 목록. 버튼을 누르면 스크립트를 실행한다.
struct CustomPanelDef {
    QString name;
    QStringList labels;
    QStringList scripts;
    QStringList geoms;     // 버튼마다 "x,y,w,h" (비어 있으면 세로로 자동 배치)
};
// 버튼을 마음대로 옮기고 크기를 바꿀 수 있는 사용자 패널
class CustomPanel : public QWidget {
    Q_OBJECT
public:
    CustomPanel(Document* doc, const CustomPanelDef& def, QWidget* parent = nullptr);
    void setHost(ScriptEngine::Host h) { host_ = std::move(h); }
    CustomPanelDef def() const { return def_; }      // 지금 배치가 반영된 설정
    void setEditing(bool on);
    bool editing() const { return editing_; }
    void autoArrange();                              // 세로로 가지런히
signals:
    void layoutChanged(const CustomPanelDef& def);   // 배치를 바꿔 저장해야 할 때
protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void paintEvent(QPaintEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    void applyGeometry();
    void storeGeometry();
    ScriptEngine::Host host_;
    Document* doc_ = nullptr;
    CustomPanelDef def_;
    QVector<class QPushButton*> buttons_;
    bool editing_ = false;
    int dragIdx_ = -1;
    bool resizing_ = false;
    QPoint dragStart_;
    QRect dragOrig_;
    class QCheckBox* editBox_ = nullptr;
    class QPushButton* arrangeBtn_ = nullptr;
};
bool editCustomPanel(QWidget* parent, CustomPanelDef* def);
QVector<CustomPanelDef> loadCustomPanels();
void saveCustomPanels(const QVector<CustomPanelDef>& defs);

// 뼈대(본) 변형 대화상자: 뼈를 놓고 끝점을 끌어 그림을 휘게 한다
class BoneWarpDialog : public QDialog {
    Q_OBJECT
public:
    BoneWarpDialog(const QImage& img, QWidget* parent = nullptr);
    QImage result() const;
    QVector<Warp::Bone> bones() const { return bones_; }
    void setBones(const QVector<Warp::Bone>& b) { bones_ = b; preview_ = QImage(); update(); }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QRectF imageRect() const;
    QPointF toImage(const QPointF& w) const;
    QPointF toWidget(const QPointF& i) const;
    int pickHandle(const QPointF& widgetPos, bool* isEnd) const;
    QImage img_;
    QVector<Warp::Bone> bones_;
    int drag_ = -1;
    bool dragEnd_ = false;
    bool placing_ = false;
    QPointF newStart_;
    QPointF hover_;
    double radius_ = 60;
    bool showOriginal_ = false;
    bool followNearest_ = true;
    QImage preview_;
    class QSpinBox* radiusBox_ = nullptr;
};

// 스토리보드 패널: 이미지가 바뀌는 프레임마다 썸네일 + 메모
class StoryboardPanel : public QWidget {
    Q_OBJECT
public:
    explicit StoryboardPanel(Document* doc, QWidget* parent = nullptr);
private:
    void rebuild();
    Document* doc_;
    QListWidget* list_;
};

// 메시 워프 대화상자: 격자점을 끌어 이미지를 변형한다.
class MeshWarpDialog : public QDialog {
    Q_OBJECT
public:
    MeshWarpDialog(const QImage& img, QWidget* parent = nullptr);
    QImage result() const;
    void setGrid(int cols, int rows);      // 격자 칸 수를 바꾸고 점을 다시 놓는다
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    void resetPoints();
    QVector<QPointF> points() const { return pts_; }
    void setPoints(const QVector<QPointF>& p) { if (p.size() == pts_.size()) { pts_ = p; update(); } }
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
private:
    QRectF imageRect() const;
    QPointF toImage(const QPointF& w) const;
    QPointF toWidget(const QPointF& i) const;
    QImage img_;
    int cols_ = 4, rows_ = 4;
    QVector<QPointF> pts_;
    int drag_ = -1;
    QImage preview_;
    bool showOriginal_ = false;
    class QSpinBox *colBox_ = nullptr, *rowBox_ = nullptr;
};
