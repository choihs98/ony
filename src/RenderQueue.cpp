#include "RenderQueue.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include "Document.h"

QString RenderJob::stateText() const {
    switch (state) {
    case 1: return QStringLiteral("하는 중");
    case 2: return QStringLiteral("끝남");
    case 3: return QStringLiteral("실패: ") + error;
    case 4: return QStringLiteral("건너뜀");
    default: return QStringLiteral("기다림");
    }
}

QVector<RenderJob>& RenderQueueDialog::jobs() {
    static QVector<RenderJob> g;
    return g;
}

namespace {
const char* kFormats[] = {"mp4", "mov", "webm", "gif", "png", "jpg", "tiff"};
QString formatLabel(const QString& f) {
    if (f == "mp4") return QStringLiteral("MP4 (H.264)");
    if (f == "mov") return QStringLiteral("MOV (ProRes)");
    if (f == "webm") return QStringLiteral("WebM (VP9)");
    if (f == "gif") return QStringLiteral("GIF");
    if (f == "png") return QStringLiteral("PNG 시퀀스");
    if (f == "jpg") return QStringLiteral("JPG 시퀀스");
    return QStringLiteral("TIFF 시퀀스");
}
bool isSequence(const QString& f) { return f == "png" || f == "jpg" || f == "tiff"; }
}   // namespace

RenderQueueDialog::RenderQueueDialog(Document* doc, QWidget* parent) : QDialog(parent), doc_(doc) {
    setWindowTitle(QStringLiteral("배치 렌더 (여러 개 걸어 두고 한 번에)"));
    setMinimumSize(760, 420);
    auto* lay = new QVBoxLayout(this);

    auto* note = new QLabel(QStringLiteral(
        "내보내기 작업을 걸어 두면 위에서부터 차례로 처리합니다.\n"
        "같은 프로젝트를 형식·범위·크기만 바꿔 여러 벌 뽑을 때 씁니다."));
    note->setWordWrap(true);
    lay->addWidget(note);

    table_ = new QTableWidget(0, 6);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("이름"), QStringLiteral("형식"), QStringLiteral("범위"), QStringLiteral("크기"),
         QStringLiteral("저장 위치"), QStringLiteral("상태")});
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table_, 1);

    auto* row = new QHBoxLayout;
    auto* add = new QPushButton(QStringLiteral("작업 추가..."));
    auto* del = new QPushButton(QStringLiteral("지우기"));
    auto* up = new QPushButton(QStringLiteral("위로"));
    auto* down = new QPushButton(QStringLiteral("아래로"));
    auto* clearDone = new QPushButton(QStringLiteral("끝난 것 비우기"));
    row->addWidget(add);
    row->addWidget(del);
    row->addWidget(up);
    row->addWidget(down);
    row->addWidget(clearDone);
    row->addStretch();
    lay->addLayout(row);

    bar_ = new QProgressBar;
    bar_->setVisible(false);
    lay->addWidget(bar_);
    status_ = new QLabel;
    status_->setStyleSheet("color:#a0a0a0;");
    lay->addWidget(status_);

    auto* bb = new QDialogButtonBox;
    runBtn_ = bb->addButton(QStringLiteral("모두 실행"), QDialogButtonBox::AcceptRole);
    auto* close = bb->addButton(QStringLiteral("닫기"), QDialogButtonBox::RejectRole);
    lay->addWidget(bb);

    connect(add, &QPushButton::clicked, this, &RenderQueueDialog::addJob);
    connect(del, &QPushButton::clicked, this, &RenderQueueDialog::removeSel);
    connect(up, &QPushButton::clicked, this, [this] { moveSel(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveSel(1); });
    connect(clearDone, &QPushButton::clicked, this, [this] {
        auto& js = jobs();
        for (int i = js.size() - 1; i >= 0; --i)
            if (js[i].state == 2) js.remove(i);
        rebuild();
    });
    connect(runBtn_, &QPushButton::clicked, this, &RenderQueueDialog::runAll);
    connect(close, &QPushButton::clicked, this, [this] {
        if (running_) {
            cancel_ = true;      // 지금 작업이 끝나면 멈춘다
            status_->setText(QStringLiteral("멈추는 중… 지금 작업이 끝나면 멈춥니다."));
            return;
        }
        reject();
    });
    rebuild();
}

int RenderQueueDialog::selectedRow() const {
    const auto sel = table_->selectionModel()->selectedRows();
    return sel.isEmpty() ? -1 : sel.first().row();
}

void RenderQueueDialog::rebuild() {
    const auto& js = jobs();
    const int keep = selectedRow();
    table_->setRowCount(js.size());
    for (int i = 0; i < js.size(); ++i) {
        const RenderJob& j = js[i];
        const int to = j.opt.to < 0 ? doc_->frameCount() - 1 : j.opt.to;
        const QString range = QString("%1 – %2").arg(j.opt.from + 1).arg(to + 1);
        const QStringList cols{j.name, formatLabel(j.opt.format), range,
                               QString("%1%").arg(j.opt.scalePercent),
                               QDir::toNativeSeparators(j.outPath), j.stateText()};
        for (int c = 0; c < cols.size(); ++c) {
            auto* it = new QTableWidgetItem(cols[c]);
            if (j.state == 3) it->setForeground(QColor(0xe2, 0x6a, 0x4a));
            else if (j.state == 2) it->setForeground(QColor(0x5a, 0xa8, 0x6a));
            table_->setItem(i, c, it);
        }
    }
    table_->resizeColumnsToContents();
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    if (keep >= 0 && keep < js.size()) table_->selectRow(keep);
    runBtn_->setEnabled(!js.isEmpty() && !running_);
    if (!running_)
        status_->setText(js.isEmpty() ? QStringLiteral("걸어 둔 작업이 없습니다.")
                                      : QStringLiteral("작업 %1개").arg(js.size()));
}

void RenderQueueDialog::addJob() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("작업 추가"));
    dlg.setMinimumWidth(460);
    auto* form = new QFormLayout(&dlg);

    auto* name = new QLineEdit(QStringLiteral("작업 %1").arg(jobs().size() + 1));
    auto* fmt = new QComboBox;
    for (const char* f : kFormats) fmt->addItem(formatLabel(QString::fromLatin1(f)), QString::fromLatin1(f));
    auto* from = new QSpinBox;
    auto* to = new QSpinBox;
    from->setRange(1, doc_->frameCount());
    to->setRange(1, doc_->frameCount());
    from->setValue(1);
    to->setValue(doc_->frameCount());
    auto* scale = new QSpinBox;
    scale->setRange(10, 400);
    scale->setValue(100);
    scale->setSuffix(QStringLiteral(" %"));
    auto* cam = new QCheckBox(QStringLiteral("카메라 시점으로"));
    cam->setChecked(true);
    auto* snd = new QCheckBox(QStringLiteral("소리 함께 넣기"));
    snd->setChecked(true);
    auto* trans = new QCheckBox(QStringLiteral("배경 투명 (MOV·WebM·PNG 에서만)"));

    auto* pathEdit = new QLineEdit;
    auto* pick = new QPushButton(QStringLiteral("찾기..."));
    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(pick);

    form->addRow(QStringLiteral("이름"), name);
    form->addRow(QStringLiteral("형식"), fmt);
    form->addRow(QStringLiteral("시작 프레임"), from);
    form->addRow(QStringLiteral("끝 프레임"), to);
    form->addRow(QStringLiteral("크기"), scale);
    form->addRow(QString(), cam);
    form->addRow(QString(), snd);
    form->addRow(QString(), trans);
    form->addRow(QStringLiteral("저장 위치"), pathRow);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("추가"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("취소"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);

    connect(pick, &QPushButton::clicked, &dlg, [&] {
        const QString f = fmt->currentData().toString();
        if (isSequence(f)) {
            const QString d = QFileDialog::getExistingDirectory(&dlg, QStringLiteral("이미지 시퀀스를 넣을 폴더"));
            if (!d.isEmpty()) pathEdit->setText(d);
        } else {
            const QString base = doc_->path().isEmpty() ? QStringLiteral("ony") : QFileInfo(doc_->path()).completeBaseName();
            const QString p = QFileDialog::getSaveFileName(&dlg, QStringLiteral("저장 위치"), base + "." + f,
                                                           QString("%1 (*.%2)").arg(formatLabel(f), f));
            if (!p.isEmpty()) pathEdit->setText(p);
        }
    });

    if (dlg.exec() != QDialog::Accepted) return;
    if (pathEdit->text().trimmed().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("작업 추가"), QStringLiteral("저장 위치를 정해 주세요."));
        return;
    }
    RenderJob j;
    j.name = name->text().trimmed().isEmpty() ? QStringLiteral("작업") : name->text().trimmed();
    j.outPath = pathEdit->text().trimmed();
    j.opt.format = fmt->currentData().toString();
    j.opt.from = std::min(from->value(), to->value()) - 1;
    j.opt.to = std::max(from->value(), to->value()) - 1;
    j.opt.scalePercent = scale->value();
    j.opt.useCamera = cam->isChecked();
    j.opt.audio = snd->isChecked();
    j.opt.background = trans->isChecked() ? QColor(0, 0, 0, 0) : QColor(Qt::white);
    j.opt.baseName = QFileInfo(j.outPath).completeBaseName().isEmpty() ? QStringLiteral("frame")
                                                                       : QFileInfo(j.outPath).completeBaseName();
    jobs().push_back(j);
    rebuild();
}

void RenderQueueDialog::removeSel() {
    const int r = selectedRow();
    if (r < 0 || r >= jobs().size() || running_) return;
    jobs().remove(r);
    rebuild();
}

void RenderQueueDialog::moveSel(int delta) {
    const int r = selectedRow();
    auto& js = jobs();
    const int n = r + delta;
    if (r < 0 || n < 0 || n >= js.size() || running_) return;
    std::swap(js[r], js[n]);
    rebuild();
    table_->selectRow(n);
}

void RenderQueueDialog::runAll() {
    if (running_) return;
    auto& js = jobs();
    if (js.isEmpty()) return;
    running_ = true;
    cancel_ = false;
    runBtn_->setEnabled(false);
    bar_->setVisible(true);

    int done = 0, failed = 0;
    for (int i = 0; i < js.size(); ++i) {
        if (cancel_) {
            for (int k = i; k < js.size(); ++k)
                if (js[k].state == 0) js[k].state = 4;
            break;
        }
        RenderJob& j = js[i];
        if (j.state == 2) continue;          // 이미 끝난 것은 건너뛴다
        j.state = 1;
        j.error.clear();
        rebuild();
        table_->selectRow(i);
        status_->setText(QStringLiteral("%1 / %2  —  %3").arg(i + 1).arg(js.size()).arg(j.name));
        QApplication::processEvents();

        const QString err = VideoExport::run(doc_, j.outPath, j.opt, [&](int d, int t) {
            bar_->setMaximum(std::max(1, t));
            bar_->setValue(d);
            QApplication::processEvents();
            return !cancel_;
        });
        if (err.isEmpty()) {
            j.state = 2;
            ++done;
        } else {
            j.state = 3;
            j.error = err;
            ++failed;
        }
        rebuild();
        QApplication::processEvents();
    }

    running_ = false;
    bar_->setVisible(false);
    runBtn_->setEnabled(true);
    rebuild();
    status_->setText(QStringLiteral("끝남 %1개, 실패 %2개%3").arg(done).arg(failed).arg(
        cancel_ ? QStringLiteral(", 나머지는 멈췄습니다") : QString()));
}
