#pragma once
#include <QDialog>
#include <QImage>
#include <QListWidget>
#include <QStringList>
#include <QVector>
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

private:
    QString runLine(const QString& line, int depth);
    bool drawOnCurrent(const std::function<void(QImage&)>& fn);
    Document* doc_;
};

// 스크립트 콘솔 패널
class ScriptConsole : public QWidget {
    Q_OBJECT
public:
    explicit ScriptConsole(Document* doc, QWidget* parent = nullptr);
private:
    Document* doc_;
    QPlainTextEdit* input_;
    QPlainTextEdit* output_;
};

// 사용자 패널: 이름 + 버튼(라벨 → 스크립트) 목록. 버튼을 누르면 스크립트를 실행한다.
struct CustomPanelDef {
    QString name;
    QStringList labels;
    QStringList scripts;
};
class CustomPanel : public QWidget {
    Q_OBJECT
public:
    CustomPanel(Document* doc, const CustomPanelDef& def, QWidget* parent = nullptr);
};
bool editCustomPanel(QWidget* parent, CustomPanelDef* def);
QVector<CustomPanelDef> loadCustomPanels();
void saveCustomPanels(const QVector<CustomPanelDef>& defs);

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
};
