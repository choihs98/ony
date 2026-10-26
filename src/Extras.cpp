#include "Extras.h"
#include "Document.h"
#include "Warp.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QtMath>
#include <queue>

namespace Extras {

QImage scanClean(const QImage& scan, int threshold, int despeckle, QColor ink) {
    const QImage g = scan.convertToFormat(QImage::Format_Grayscale8);
    const int w = g.width(), h = g.height();
    threshold = std::clamp(threshold, 1, 255);
    QVector<quint8> mask(w * h, 0);
    for (int y = 0; y < h; ++y) {
        const uchar* row = g.constScanLine(y);
        for (int x = 0; x < w; ++x) mask[y * w + x] = row[x] < threshold ? 1 : 0;
    }
    if (despeckle > 0) {
        QVector<quint8> seen(w * h, 0);
        for (int i = 0; i < w * h; ++i) {
            if (!mask[i] || seen[i]) continue;
            QVector<int> comp;
            std::queue<int> q;
            q.push(i);
            seen[i] = 1;
            while (!q.empty()) {
                const int cur = q.front();
                q.pop();
                comp.push_back(cur);
                const int cx = cur % w, cy = cur / w;
                const int nb[4][2] = {{cx - 1, cy}, {cx + 1, cy}, {cx, cy - 1}, {cx, cy + 1}};
                for (const auto& n : nb) {
                    if (n[0] < 0 || n[1] < 0 || n[0] >= w || n[1] >= h) continue;
                    const int ni = n[1] * w + n[0];
                    if (mask[ni] && !seen[ni]) {
                        seen[ni] = 1;
                        q.push(ni);
                    }
                }
            }
            if (comp.size() <= despeckle)
                for (int c : comp) mask[c] = 0;
        }
    }
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    for (int y = 0; y < h; ++y) {
        const uchar* row = g.constScanLine(y);
        QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (!mask[y * w + x]) continue;
            // 어두울수록 진하게
            const int a = std::clamp(255 - row[x] * 255 / threshold + 80, 80, 255);
            QColor c = ink;
            c.setAlpha(a);
            dst[x] = qPremultiply(c.rgba());
        }
    }
    return out;
}

bool exportXdts(const Document* doc, const QString& path, QString* error) {
    QJsonArray tracks, names;
    const int n = doc->layerCount();
    for (int ci = 0; ci < n; ++ci) {
        const int l = n - 1 - ci; // 위 레이어가 왼쪽 열
        QJsonArray frames;
        int keyNo = 0;
        bool any = false;
        int last = -2;
        for (int f = 0; f < doc->frameCount(); ++f) {
            if (!doc->layer(l).frames[f].isNull()) {
                ++keyNo;
                any = true;
                last = f;
                QJsonObject cell{{"id", 0}, {"values", QJsonArray{QString::number(keyNo)}}};
                frames.append(QJsonObject{{"data", QJsonArray{cell}}, {"frame", f}});
            }
        }
        if (any) {
            QJsonObject end{{"id", 0}, {"values", QJsonArray{QStringLiteral("SYMBOL_NULL_CELL")}}};
            if (last + 1 >= 0) {
                // 마지막 키 노출이 끝나는 지점에 빈 셀 기록 (전체 길이보다 작을 때)
                int endFrame = doc->frameCount();
                if (endFrame > last) frames.append(QJsonObject{{"data", QJsonArray{end}}, {"frame", endFrame}});
            }
        }
        tracks.append(QJsonObject{{"trackNo", ci}, {"frames", frames}});
        names.append(doc->layer(l).name);
    }
    QJsonObject field{{"fieldId", 0}, {"tracks", tracks}};
    QJsonObject table{{"duration", doc->frameCount()},
                      {"name", QStringLiteral("TS1")},
                      {"fields", QJsonArray{field}},
                      {"timeTableHeaders", QJsonArray{QJsonObject{{"fieldId", 0}, {"names", names}}}}};
    QJsonObject root{{"version", 5},
                     {"header", QJsonObject{{"cut", QStringLiteral("1")}, {"scene", QStringLiteral("1")}}},
                     {"timeTables", QJsonArray{table}}};
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write("exchangeDigitalTimeSheet Save Data\n");
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

} // namespace Extras

// ---------------------------------------------------------------- 스크립트
QString ScriptEngine::help() {
    return QStringLiteral(
        "명령어 (한 줄에 하나, # 은 주석)\n"
        "  frame N | frames N | fps N\n"
        "  layer.new 이름 | layer.select N | layer.delete | layer.rename 이름\n"
        "  layer.visible 0/1 | layer.opacity 0-100\n"
        "  key.new | key.clear | clear\n"
        "  rect X Y W H #rrggbb | line X1 Y1 X2 Y2 #rrggbb 굵기 | ellipse X Y W H #rrggbb\n"
        "  fx.invert | fx.gray | fx.bright N | fx.blur N | flip.h | flip.v\n"
        "  repeat A B 명령  (A~B 반복, 명령 안의 {i} 가 값으로 바뀜)\n"
        "  echo 문장");
}

bool ScriptEngine::drawOnCurrent(const std::function<void(QImage&)>& fn) {
    const int l = doc_->currentLayer(), f = doc_->currentFrame();
    if (doc_->layer(l).locked) return false;
    const int k = doc_->keyIndexAt(l, f);
    QImage img = k >= 0 ? doc_->keyImage(l, k) : QImage(doc_->size(), QImage::Format_ARGB32_Premultiplied);
    if (k < 0) img.fill(Qt::transparent);
    fn(img);
    doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], img, QStringLiteral("스크립트"));
    return true;
}

QString ScriptEngine::run(const QString& script) {
    QString log;
    const QStringList lines = script.split('\n');
    int n = 0;
    for (const QString& raw : lines) {
        ++n;
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QString r = runLine(line, 0);
        if (!r.isEmpty()) log += QString("%1: %2\n").arg(n).arg(r);
    }
    if (log.isEmpty()) log = QStringLiteral("완료\n");
    return log;
}

QString ScriptEngine::runLine(const QString& line, int depth) {
    if (depth > 4) return QStringLiteral("repeat 가 너무 깊습니다");
    const QStringList t = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (t.isEmpty()) return {};
    const QString cmd = t[0].toLower();
    auto num = [&](int i, double def = 0) { return i < t.size() ? t[i].toDouble() : def; };
    auto col = [&](int i) { return i < t.size() ? QColor(t[i]) : QColor(Qt::black); };
    const QString rest = line.section(QRegularExpression("\\s+"), 1);

    if (cmd == "echo") return rest;
    if (cmd == "repeat") {
        if (t.size() < 4) return QStringLiteral("사용법: repeat A B 명령");
        const int a = int(num(1)), b = int(num(2));
        if (b - a > 10000) return QStringLiteral("반복 횟수가 너무 많습니다");
        const QString body = line.section(QRegularExpression("\\s+"), 3);
        QString log;
        for (int i = a; i <= b; ++i) {
            QString cur = body;
            cur.replace("{i}", QString::number(i));
            const QString r = runLine(cur, depth + 1);
            if (!r.isEmpty()) log += r + "; ";
        }
        return log;
    }
    if (cmd == "frame") { doc_->setCurrentFrame(int(num(1)) - 1); return {}; }
    if (cmd == "frames") { doc_->setFrameCount(int(num(1))); return {}; }
    if (cmd == "fps") { doc_->setFps(int(num(1, 24))); return {}; }
    if (cmd == "layer.new") { doc_->addLayer(rest.isEmpty() ? QStringLiteral("레이어") : rest); return {}; }
    if (cmd == "layer.select") {
        const int i = int(num(1)) - 1;
        if (i < 0 || i >= doc_->layerCount()) return QStringLiteral("없는 레이어");
        doc_->setCurrentLayer(i);
        return {};
    }
    if (cmd == "layer.delete") { doc_->removeLayer(doc_->currentLayer()); return {}; }
    if (cmd == "layer.rename") { doc_->layer(doc_->currentLayer()).name = rest; doc_->notifyStructure(); return {}; }
    if (cmd == "layer.visible") { doc_->layer(doc_->currentLayer()).visible = num(1, 1) != 0; doc_->notifyStructure(); return {}; }
    if (cmd == "layer.opacity") { doc_->layer(doc_->currentLayer()).opacity = std::clamp(num(1, 100), 0.0, 100.0) / 100.0; doc_->notifyChanged(); return {}; }
    if (cmd == "key.new" || cmd == "clear") {
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        if (doc_->layer(l).locked) return QStringLiteral("잠긴 레이어");
        QImage blank(doc_->size(), QImage::Format_ARGB32_Premultiplied);
        blank.fill(Qt::transparent);
        doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], blank, QStringLiteral("스크립트"));
        return {};
    }
    if (cmd == "key.clear") {
        const int l = doc_->currentLayer(), f = doc_->currentFrame();
        if (doc_->layer(l).locked || doc_->layer(l).frames[f].isNull()) return {};
        doc_->pushKeyEdit(l, f, doc_->layer(l).frames[f], QImage(), QStringLiteral("스크립트"));
        return {};
    }
    if (cmd == "rect" || cmd == "ellipse") {
        if (t.size() < 5) return QStringLiteral("사용법: %1 X Y W H 색").arg(cmd);
        const QRectF r(num(1), num(2), num(3), num(4));
        const QColor c = col(5);
        if (!drawOnCurrent([&](QImage& im) {
                QPainter p(&im);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(Qt::NoPen);
                p.setBrush(c);
                if (cmd == "rect") p.drawRect(r);
                else p.drawEllipse(r);
            }))
            return QStringLiteral("잠긴 레이어");
        return {};
    }
    if (cmd == "line") {
        if (t.size() < 5) return QStringLiteral("사용법: line X1 Y1 X2 Y2 색 굵기");
        const QColor c = col(5);
        const double w = num(6, 3);
        if (!drawOnCurrent([&](QImage& im) {
                QPainter p(&im);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(QPointF(num(1), num(2)), QPointF(num(3), num(4)));
            }))
            return QStringLiteral("잠긴 레이어");
        return {};
    }
    if (cmd == "fx.invert") { doc_->transformCurrent(Effects::invert, "스크립트"); return {}; }
    if (cmd == "fx.gray") { doc_->transformCurrent(Effects::grayscale, "스크립트"); return {}; }
    if (cmd == "fx.bright") { const int v = int(num(1)); doc_->transformCurrent([v](const QImage& i) { return Effects::brightness(i, v); }, "스크립트"); return {}; }
    if (cmd == "fx.blur") { const int v = int(num(1, 3)); doc_->transformCurrent([v](const QImage& i) { return Effects::boxBlur(i, v); }, "스크립트"); return {}; }
    if (cmd == "flip.h") { doc_->transformCurrent(Effects::flipH, "스크립트"); return {}; }
    if (cmd == "flip.v") { doc_->transformCurrent(Effects::flipV, "스크립트"); return {}; }
    return QStringLiteral("알 수 없는 명령: %1").arg(t[0]);
}

ScriptConsole::ScriptConsole(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    input_ = new QPlainTextEdit;
    input_->setPlaceholderText(QStringLiteral("예)\nlayer.new 배경\nrect 0 0 320 200 #88ccff\nrepeat 1 12 frame {i}"));
    output_ = new QPlainTextEdit;
    output_->setReadOnly(true);
    output_->setPlainText(ScriptEngine::help());
    auto* row = new QHBoxLayout;
    auto* run = new QPushButton(QStringLiteral("실행"));
    auto* help = new QPushButton(QStringLiteral("도움말"));
    row->addWidget(run);
    row->addWidget(help);
    row->addStretch();
    lay->addWidget(input_, 2);
    lay->addLayout(row);
    lay->addWidget(output_, 3);
    connect(run, &QPushButton::clicked, this, [this] {
        ScriptEngine eng(doc_);
        output_->setPlainText(eng.run(input_->toPlainText()));
    });
    connect(help, &QPushButton::clicked, this, [this] { output_->setPlainText(ScriptEngine::help()); });
}

// ---------------------------------------------------------------- 사용자 패널
CustomPanel::CustomPanel(Document* doc, const CustomPanelDef& def, QWidget* parent) : QWidget(parent) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    for (int i = 0; i < def.labels.size(); ++i) {
        auto* b = new QPushButton(def.labels[i]);
        const QString script = def.scripts.value(i);
        connect(b, &QPushButton::clicked, this, [doc, script] {
            ScriptEngine eng(doc);
            eng.run(script);
        });
        lay->addWidget(b);
    }
    lay->addStretch();
}

bool editCustomPanel(QWidget* parent, CustomPanelDef* def) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("사용자 패널 만들기"));
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(def->name);
    auto* text = new QPlainTextEdit;
    text->setPlaceholderText(QStringLiteral("한 줄에 버튼 하나: 버튼이름 | 명령 (여러 명령은 ; 로 구분)\n예) 빨간 사각형 | rect 10 10 100 60 #ff0000"));
    QString init;
    for (int i = 0; i < def->labels.size(); ++i) init += def->labels[i] + " | " + def->scripts.value(i).replace('\n', ';') + "\n";
    text->setPlainText(init);
    text->setMinimumSize(420, 180);
    form->addRow(QStringLiteral("패널 이름"), name);
    form->addRow(QStringLiteral("버튼"), text);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) return false;
    def->name = name->text().trimmed();
    def->labels.clear();
    def->scripts.clear();
    for (const QString& ln : text->toPlainText().split('\n')) {
        const int bar = ln.indexOf('|');
        if (bar < 0) continue;
        const QString label = ln.left(bar).trimmed();
        if (label.isEmpty()) continue;
        def->labels << label;
        def->scripts << ln.mid(bar + 1).trimmed().replace(';', '\n');
    }
    return !def->labels.isEmpty();
}

QVector<CustomPanelDef> loadCustomPanels() {
    QSettings st("Ony", "Ony");
    QVector<CustomPanelDef> out;
    const int n = st.beginReadArray("customPanels");
    for (int i = 0; i < n; ++i) {
        st.setArrayIndex(i);
        CustomPanelDef d;
        d.name = st.value("name").toString();
        d.labels = st.value("labels").toStringList();
        d.scripts = st.value("scripts").toStringList();
        if (!d.name.isEmpty()) out.push_back(d);
    }
    st.endArray();
    return out;
}

void saveCustomPanels(const QVector<CustomPanelDef>& defs) {
    QSettings st("Ony", "Ony");
    st.beginWriteArray("customPanels", defs.size());
    for (int i = 0; i < defs.size(); ++i) {
        st.setArrayIndex(i);
        st.setValue("name", defs[i].name);
        st.setValue("labels", defs[i].labels);
        st.setValue("scripts", defs[i].scripts);
    }
    st.endArray();
}

// ---------------------------------------------------------------- 스토리보드
StoryboardPanel::StoryboardPanel(Document* doc, QWidget* parent) : QWidget(parent), doc_(doc) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    list_ = new QListWidget;
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(160, 90));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setSpacing(6);
    lay->addWidget(new QLabel(QStringLiteral("컷(이미지가 바뀌는 프레임). 더블클릭: 메모 편집")));
    lay->addWidget(list_, 1);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { doc_->setCurrentFrame(it->data(Qt::UserRole).toInt()); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const int f = it->data(Qt::UserRole).toInt();
        bool ok = false;
        const QString t = QInputDialog::getMultiLineText(this, QStringLiteral("메모"), QString("프레임 %1 메모:").arg(f + 1), doc_->note(f), &ok);
        if (ok) doc_->setNote(f, t);
    });
    connect(doc_, &Document::structureChanged, this, &StoryboardPanel::rebuild);
    connect(doc_, &Document::libraryChanged, this, &StoryboardPanel::rebuild);
    rebuild();
}

void StoryboardPanel::rebuild() {
    list_->clear();
    QSet<int> starts;
    for (int l = 0; l < doc_->layerCount(); ++l)
        for (int f = 0; f < doc_->frameCount(); ++f)
            if (!doc_->layer(l).frames[f].isNull()) starts.insert(f);
    for (auto it = doc_->notes().cbegin(); it != doc_->notes().cend(); ++it) starts.insert(it.key());
    QList<int> sorted = starts.values();
    std::sort(sorted.begin(), sorted.end());
    if (sorted.isEmpty()) sorted << 0;
    for (int f : sorted) {
        const QImage thumb = doc_->composite(f).scaled(160, 90, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumb)), QString("%1\n%2").arg(f + 1).arg(doc_->note(f)));
        item->setData(Qt::UserRole, f);
        list_->addItem(item);
    }
}

// ---------------------------------------------------------------- 메시 워프
MeshWarpDialog::MeshWarpDialog(const QImage& img, QWidget* parent) : QDialog(parent), img_(img) {
    setWindowTitle(QStringLiteral("메시 워프 — 격자점을 끌어 변형 (더블클릭: 초기화)"));
    resize(900, 620);
    for (int r = 0; r <= rows_; ++r)
        for (int c = 0; c <= cols_; ++c) pts_.push_back(QPointF(c * img_.width() / double(cols_), r * img_.height() / double(rows_)));
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bb->move(width() - 200, height() - 40);
    bb->setFixedWidth(190);
    bb->raise();
}

QRectF MeshWarpDialog::imageRect() const {
    const QRectF area(20, 20, width() - 40, height() - 80);
    const double s = std::min(area.width() / img_.width(), area.height() / img_.height());
    const QSizeF sz(img_.width() * s, img_.height() * s);
    return QRectF(area.center().x() - sz.width() / 2, area.center().y() - sz.height() / 2, sz.width(), sz.height());
}
QPointF MeshWarpDialog::toWidget(const QPointF& i) const {
    const QRectF r = imageRect();
    return QPointF(r.x() + i.x() * r.width() / img_.width(), r.y() + i.y() * r.height() / img_.height());
}
QPointF MeshWarpDialog::toImage(const QPointF& w) const {
    const QRectF r = imageRect();
    return QPointF((w.x() - r.x()) * img_.width() / r.width(), (w.y() - r.y()) * img_.height() / r.height());
}

QImage MeshWarpDialog::result() const { return Warp::meshWarp(img_, cols_, rows_, pts_); }

void MeshWarpDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().window());
    const QRectF r = imageRect();
    p.fillRect(r, Qt::white);
    if (preview_.isNull() || preview_.size() != img_.size()) preview_ = result();
    p.drawImage(r, preview_);
    p.setPen(QColor(0x4a, 0x90, 0xe2));
    for (int rr = 0; rr <= rows_; ++rr)
        for (int c = 0; c <= cols_; ++c) {
            const QPointF a = toWidget(pts_[rr * (cols_ + 1) + c]);
            if (c < cols_) p.drawLine(a, toWidget(pts_[rr * (cols_ + 1) + c + 1]));
            if (rr < rows_) p.drawLine(a, toWidget(pts_[(rr + 1) * (cols_ + 1) + c]));
        }
    p.setBrush(Qt::white);
    for (const QPointF& pt : pts_) p.drawEllipse(toWidget(pt), 5, 5);
}

void MeshWarpDialog::mousePressEvent(QMouseEvent* e) {
    drag_ = -1;
    double best = 144;
    for (int i = 0; i < pts_.size(); ++i) {
        const QPointF d = toWidget(pts_[i]) - e->position();
        const double dd = d.x() * d.x() + d.y() * d.y();
        if (dd < best) { best = dd; drag_ = i; }
    }
}
void MeshWarpDialog::mouseMoveEvent(QMouseEvent* e) {
    if (drag_ < 0) return;
    pts_[drag_] = toImage(e->position());
    update();
}
void MeshWarpDialog::mouseReleaseEvent(QMouseEvent*) {
    if (drag_ >= 0) preview_ = result();
    drag_ = -1;
    update();
}
