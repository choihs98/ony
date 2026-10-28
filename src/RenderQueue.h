#pragma once
#include <QDialog>
#include <QString>
#include <QVector>
#include "VideoExport.h"

class Document;
class QTableWidget;
class QLabel;
class QPushButton;
class QProgressBar;

// 내보내기 작업을 여러 개 걸어 두고 순서대로 처리한다.
struct RenderJob {
    QString name;        // 목록에 보일 이름
    QString outPath;     // 저장 위치
    VideoOptions opt;
    int state = 0;       // 0 기다림, 1 하는 중, 2 끝남, 3 실패, 4 건너뜀
    QString error;
    QString stateText() const;
};

class RenderQueueDialog : public QDialog {
    Q_OBJECT
public:
    RenderQueueDialog(Document* doc, QWidget* parent = nullptr);
    // 창을 닫아도 목록이 남도록, 걸어 둔 작업을 밖에서 들고 있는다
    static QVector<RenderJob>& jobs();

private:
    void rebuild();
    void addJob();
    void runAll();
    void moveSel(int delta);
    void removeSel();
    int selectedRow() const;

    Document* doc_;
    QTableWidget* table_;
    QLabel* status_;
    QProgressBar* bar_;
    QPushButton* runBtn_;
    bool running_ = false;
    bool cancel_ = false;
};
