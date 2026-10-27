#include <cmath>
#include <functional>
#include <algorithm>
#include <QCheckBox>
#include "Extras.h"
#include "Blend.h"
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
        "■ 기본 (한 줄에 하나, # 은 주석)\n"
        "  echo 문장          화면에 글 남기기 ($변수 를 쓸 수 있음)\n"
        "  set 이름 = 식       숫자 변수 (예: set n = 10 + 2*3)\n"
        "  sets 이름 문장      글자 변수\n"
        "  get 이름 항목       문서에서 값 읽기 (frame frames fps layer layers width height keys)\n"
        "  vars               변수 목록 보기 / eval 식  계산해 보기\n"
        "■ 흐름\n"
        "  if 조건 ... else ... end\n"
        "  while 조건 ... end\n"
        "  for i = 1 to 10 [step 2] ... end      (안에서 $i 로 씀)\n"
        "  repeat A B 명령                        (한 줄 반복, {i} 가 값으로 바뀜)\n"
        "  식에 쓸 수 있는 것: + - * / % ( ) < > <= >= == != and or not abs min max int sin cos\n"
        "■ 프로젝트 · 레이어\n"
        "  frame N | frames N | fps N\n"
        "  layer.new 이름 | layer.select N | layer.delete | layer.rename 이름\n"
        "  layer.visible 0/1 | layer.opacity 0-100 | layer.lock 0/1\n"
        "  layer.move ±1 | layer.duplicate | layer.mergedown | layer.blend N\n"
        "■ 프레임 · 키\n"
        "  frame.insert | frame.delete | frame.next | frame.prev\n"
        "  frame.copy A B 전체레이어0/1 | frame.paste [AT] [끼워넣기0/1]\n"
        "  key.new | key.clear | key.exposure N | key.reverse | key.repeat A B 횟수 왕복0/1\n"
        "■ 그리기\n"
        "  clear | rect X Y W H #rrggbb | ellipse X Y W H #rrggbb | line X1 Y1 X2 Y2 #rrggbb 굵기\n"
        "■ 효과\n"
        "  fx.invert fx.gray fx.bright N fx.blur N fx.gauss N fx.contrast N fx.saturation N\n"
        "  fx.hue N fx.sepia fx.posterize N fx.threshold N fx.wave N fx.swirl N fx.bulge N\n"
        "  fx.keywhite N fx.keyblack N fx.keygreen N fx.motionblur N fx.echo N fx.noise N\n"
        "  fx.vignette N fx.shadow N fx.pixelate N fx.emboss fx.edge fx.outline N fx.sharpen N\n"
        "  fx.curve N fx.levels N | flip.h | flip.v\n"
        "■ 카메라 · 그 밖\n"
        "  camera.on | camera.off | camera.key 프레임 X Y 배율% 회전 이징\n"
        "  bookmark [프레임] | lib.add 이름 [분류]\n"
        "  save 경로 | savecopy 경로 | export.png 경로\n"
        "■ 창에서 처리하는 명령 (스크립트 콘솔에서 쓸 때)\n"
        "  select.all | select.none | select.invert | select.rect X Y W H\n"
        "  select.grow N | select.feather N | color A #rrggbb | tool 이름 | zoom N | msg 문장");
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
    QStringList lines = script.split('\n');
    steps_ = 0;
    const QString log = runBlock(lines, 0, lines.size(), 0);
    return log.isEmpty() ? QStringLiteral("완료\n") : log;
}

// if / while / for 의 짝이 맞는 end 줄 번호 (없으면 -1)
int ScriptEngine::matchEnd(const QStringList& lines, int start, int to) const {
    int depth = 0;
    for (int i = start; i < to; ++i) {
        const QString t = lines[i].trimmed().section(QRegularExpression("\\s+"), 0, 0).toLower();
        if (t == "if" || t == "while" || t == "for") ++depth;
        else if (t == "end") {
            --depth;
            if (depth == 0) return i;
        }
    }
    return -1;
}

QString ScriptEngine::runBlock(const QStringList& lines, int from, int to, int depth) {
    if (depth > 16) return QStringLiteral("블록이 너무 깊습니다\n");
    QString log;
    for (int i = from; i < to; ++i) {
        if (++steps_ > 500000) return log + QStringLiteral("명령이 너무 많습니다 (무한 반복?)\n");
        const QString line = lines[i].trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        const QStringList t = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const QString head = t[0].toLower();

        if (head == "if") {
            const int endIdx = matchEnd(lines, i, to);
            if (endIdx < 0) return log + QString("%1: if 에 맞는 end 가 없습니다\n").arg(i + 1);
            // else 찾기 (같은 깊이)
            int elseIdx = -1, d = 0;
            for (int k = i; k < endIdx; ++k) {
                const QString kt = lines[k].trimmed().section(QRegularExpression("\\s+"), 0, 0).toLower();
                if (kt == "if" || kt == "while" || kt == "for") ++d;
                else if (kt == "end") --d;
                else if (kt == "else" && d == 1) { elseIdx = k; break; }
            }
            const QString cond = line.section(QRegularExpression("\\s+"), 1);
            const bool ok = eval(cond) != 0.0;
            if (ok) log += runBlock(lines, i + 1, elseIdx >= 0 ? elseIdx : endIdx, depth + 1);
            else if (elseIdx >= 0) log += runBlock(lines, elseIdx + 1, endIdx, depth + 1);
            i = endIdx;
            continue;
        }
        if (head == "while") {
            const int endIdx = matchEnd(lines, i, to);
            if (endIdx < 0) return log + QString("%1: while 에 맞는 end 가 없습니다\n").arg(i + 1);
            const QString cond = line.section(QRegularExpression("\\s+"), 1);
            int guard = 0;
            while (eval(cond) != 0.0) {
                if (++guard > 100000 || ++steps_ > 500000) {
                    log += QStringLiteral("while 이 너무 오래 돕니다\n");
                    break;
                }
                log += runBlock(lines, i + 1, endIdx, depth + 1);
            }
            i = endIdx;
            continue;
        }
        if (head == "for") {
            // for i = 1 to 10  [step 2]
            const int endIdx = matchEnd(lines, i, to);
            if (endIdx < 0) return log + QString("%1: for 에 맞는 end 가 없습니다\n").arg(i + 1);
            if (t.size() < 5 || t[2] != "=") {
                log += QString("%1: 사용법: for 변수 = 시작 to 끝 [step N]\n").arg(i + 1);
                i = endIdx;
                continue;
            }
            const QString v = t[1];
            const QString rest = line.section(QRegularExpression("\\s+"), 3);
            const int toPos = rest.toLower().indexOf(" to ");
            if (toPos < 0) {
                log += QString("%1: for 에 to 가 없습니다\n").arg(i + 1);
                i = endIdx;
                continue;
            }
            const double a = eval(rest.left(toPos));
            QString tail = rest.mid(toPos + 4);
            double stepv = 1;
            const int sp = tail.toLower().indexOf(" step ");
            if (sp >= 0) {
                stepv = eval(tail.mid(sp + 6));
                tail = tail.left(sp);
            }
            const double b = eval(tail);
            if (stepv == 0) stepv = 1;
            int guard = 0;
            for (double x = a; (stepv > 0 ? x <= b : x >= b); x += stepv) {
                if (++guard > 100000 || ++steps_ > 500000) {
                    log += QStringLiteral("for 가 너무 오래 돕니다\n");
                    break;
                }
                vars_[v] = x;
                log += runBlock(lines, i + 1, endIdx, depth + 1);
            }
            i = endIdx;
            continue;
        }
        if (head == "else" || head == "end") continue;

        const QString r = runLine(line, depth);
        if (!r.isEmpty()) log += QString("%1: %2\n").arg(i + 1).arg(r);
    }
    return log;
}

// $이름 을 값으로 바꾼다
QString ScriptEngine::expand(const QString& s) const {
    QString out = s;
    static const QRegularExpression re("\\$([A-Za-z_][A-Za-z0-9_]*)");
    QRegularExpressionMatchIterator it = re.globalMatch(s);
    QVector<QPair<int, QPair<int, QString>>> repl;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1);
        QString v;
        if (svars_.contains(name)) v = svars_.value(name);
        else if (vars_.contains(name)) {
            const double d = vars_.value(name);
            v = (d == std::floor(d) && std::abs(d) < 1e15) ? QString::number(qint64(d)) : QString::number(d, 'g', 10);
        } else {
            v = QStringLiteral("0");
        }
        repl.push_back({int(m.capturedStart(0)), {int(m.capturedLength(0)), v}});
    }
    for (int k = repl.size() - 1; k >= 0; --k) out.replace(repl[k].first, repl[k].second.first, repl[k].second.second);
    return out;
}

// 아주 작은 식 계산기: or / and / 비교 / + - / * / % / 괄호 / 숫자 / $변수
namespace {
struct Expr {
    QString s;
    int i = 0;
    const QMap<QString, double>* vars = nullptr;
    void ws() { while (i < s.size() && s[i].isSpace()) ++i; }
    bool word(const char* w) {
        ws();
        const QString t = QString::fromLatin1(w);
        if (s.mid(i, t.size()).compare(t, Qt::CaseInsensitive) == 0) {
            const int after = i + t.size();
            if (after >= s.size() || !s[after].isLetterOrNumber()) {
                i = after;
                return true;
            }
        }
        return false;
    }
    bool sym(const char* w) {
        ws();
        const QString t = QString::fromLatin1(w);
        if (s.mid(i, t.size()) == t) {
            i += t.size();
            return true;
        }
        return false;
    }
    double orExpr() {
        double v = andExpr();
        while (word("or") || sym("||")) v = (v != 0 || andExpr() != 0) ? 1 : 0;
        return v;
    }
    double andExpr() {
        double v = cmp();
        while (word("and") || sym("&&")) v = (v != 0 && cmp() != 0) ? 1 : 0;
        return v;
    }
    double cmp() {
        double v = add();
        for (;;) {
            if (sym("<=")) v = v <= add() ? 1 : 0;
            else if (sym(">=")) v = v >= add() ? 1 : 0;
            else if (sym("==")) v = std::abs(v - add()) < 1e-9 ? 1 : 0;
            else if (sym("!=")) v = std::abs(v - add()) >= 1e-9 ? 1 : 0;
            else if (sym("<")) v = v < add() ? 1 : 0;
            else if (sym(">")) v = v > add() ? 1 : 0;
            else break;
        }
        return v;
    }
    double add() {
        double v = mul();
        for (;;) {
            if (sym("+")) v += mul();
            else if (sym("-")) v -= mul();
            else break;
        }
        return v;
    }
    double mul() {
        double v = unary();
        for (;;) {
            if (sym("*")) v *= unary();
            else if (sym("/")) {
                const double d = unary();
                v = std::abs(d) < 1e-12 ? 0 : v / d;
            } else if (sym("%")) {
                const double d = unary();
                v = std::abs(d) < 1e-12 ? 0 : std::fmod(v, d);
            } else break;
        }
        return v;
    }
    double unary() {
        ws();
        if (sym("-")) return -unary();
        if (sym("+")) return unary();
        if (word("not") || sym("!")) return unary() == 0 ? 1 : 0;
        return atom();
    }
    double atom() {
        ws();
        if (sym("(")) {
            const double v = orExpr();
            sym(")");
            return v;
        }
        if (word("abs")) { sym("("); const double v = orExpr(); sym(")"); return std::abs(v); }
        if (word("min")) { sym("("); const double a = orExpr(); sym(","); const double b = orExpr(); sym(")"); return std::min(a, b); }
        if (word("max")) { sym("("); const double a = orExpr(); sym(","); const double b = orExpr(); sym(")"); return std::max(a, b); }
        if (word("int")) { sym("("); const double v = orExpr(); sym(")"); return std::floor(v); }
        if (word("sin")) { sym("("); const double v = orExpr(); sym(")"); return std::sin(v); }
        if (word("cos")) { sym("("); const double v = orExpr(); sym(")"); return std::cos(v); }
        const int start = i;
        while (i < s.size() && (s[i].isDigit() || s[i] == '.')) ++i;
        if (i > start) return s.mid(start, i - start).toDouble();
        // 이름 (변수)
        const int ns = i;
        while (i < s.size() && (s[i].isLetterOrNumber() || s[i] == '_')) ++i;
        if (i > ns && vars) return vars->value(s.mid(ns, i - ns), 0.0);
        if (i == ns) ++i;   // 모르는 글자는 건너뛴다
        return 0;
    }
};
}

double ScriptEngine::eval(const QString& expr) const {
    Expr e;
    e.s = expand(expr);
    e.vars = &vars_;
    return e.orExpr();
}

QString ScriptEngine::runLine(const QString& rawLine, int depth) {
    if (depth > 16) return QStringLiteral("너무 깊습니다");
    const QString line = expand(rawLine);
    const QStringList t = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (t.isEmpty()) return {};
    const QString cmd = t[0].toLower();
    // 인자에 식을 써도 된다 (띄어쓰기 없이: $c*40+8)
    auto num = [&](int i, double def = 0) { return i < t.size() ? eval(t[i]) : def; };
    auto col = [&](int i) { return i < t.size() ? QColor(t[i]) : QColor(Qt::black); };
    const QString rest = line.section(QRegularExpression("\\s+"), 1);

    if (cmd == "echo" || cmd == "print") return rest;
    if (cmd == "set") {
        // set 이름 = 식   /   set 이름 식
        if (t.size() < 3) return QStringLiteral("사용법: set 이름 = 식");
        const QString name = t[1];
        const int eq = line.indexOf('=');
        const QString ex = eq >= 0 ? line.mid(eq + 1) : line.section(QRegularExpression("\\s+"), 2);
        vars_[name] = eval(ex);
        return {};
    }
    if (cmd == "sets") {   // 문자열 변수
        if (t.size() < 3) return QStringLiteral("사용법: sets 이름 문자열");
        svars_[t[1]] = line.section(QRegularExpression("\\s+"), 2);
        return {};
    }
    if (cmd == "vars") {
        QStringList out;
        for (auto it = vars_.cbegin(); it != vars_.cend(); ++it) out << QString("%1=%2").arg(it.key()).arg(it.value());
        for (auto it = svars_.cbegin(); it != svars_.cend(); ++it) out << QString("%1=\"%2\"").arg(it.key(), it.value());
        return out.isEmpty() ? QStringLiteral("(변수 없음)") : out.join(", ");
    }
    if (cmd == "eval") return QString::number(eval(rest));
    // ---- 문서에서 값을 읽어 변수에 담는다 ----
    if (cmd == "get") {
        if (t.size() < 3) return QStringLiteral("사용법: get 변수 항목  (항목: frame frames fps layer layers width height keys)");
        const QString what = t[2].toLower();
        double v = 0;
        if (what == "frame") v = doc_->currentFrame() + 1;
        else if (what == "frames") v = doc_->frameCount();
        else if (what == "fps") v = doc_->fps();
        else if (what == "layer") v = doc_->currentLayer();
        else if (what == "layers") v = doc_->layerCount();
        else if (what == "width") v = doc_->size().width();
        else if (what == "height") v = doc_->size().height();
        else if (what == "keys") {
            int n = 0;
            const Layer& L = doc_->layer(doc_->currentLayer());
            for (int f = 0; f < doc_->frameCount(); ++f) n += L.frames[f].isNull() ? 0 : 1;
            v = n;
        } else return QString("모르는 항목: %1").arg(what);
        vars_[t[1]] = v;
        return {};
    }
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
    // ---- 효과 (데모5에서 넓힘) ----
    {
        static const QMap<QString, Effects::Type> kFx{
            {"fx.gauss", Effects::Type::GaussBlur}, {"fx.contrast", Effects::Type::Contrast},
            {"fx.saturation", Effects::Type::Saturation}, {"fx.hue", Effects::Type::HueShift},
            {"fx.sepia", Effects::Type::Sepia}, {"fx.posterize", Effects::Type::Posterize},
            {"fx.threshold", Effects::Type::Threshold}, {"fx.wave", Effects::Type::Wave},
            {"fx.swirl", Effects::Type::Swirl}, {"fx.bulge", Effects::Type::Bulge},
            {"fx.keywhite", Effects::Type::KeyWhite}, {"fx.keyblack", Effects::Type::KeyBlack},
            {"fx.keygreen", Effects::Type::KeyGreen}, {"fx.motionblur", Effects::Type::MotionBlurH},
            {"fx.motionblurv", Effects::Type::MotionBlurV}, {"fx.echo", Effects::Type::Echo},
            {"fx.noise", Effects::Type::Noise}, {"fx.vignette", Effects::Type::Vignette},
            {"fx.shadow", Effects::Type::DropShadow}, {"fx.pixelate", Effects::Type::Pixelate},
            {"fx.emboss", Effects::Type::Emboss}, {"fx.edge", Effects::Type::Edge},
            {"fx.outline", Effects::Type::Outline}, {"fx.sharpen", Effects::Type::Sharpen},
            {"fx.curve", Effects::Type::Curves}, {"fx.levels", Effects::Type::Levels},
        };
        const auto it = kFx.constFind(cmd);
        if (it != kFx.constEnd()) {
            Effects::Item item;
            item.type = it.value();
            const auto pi = Effects::paramInfo(item.type);
            item.param = t.size() > 1 ? int(num(1)) : pi.def;
            doc_->transformCurrent([item](const QImage& i) { return Effects::apply(i, item); }, QStringLiteral("스크립트"));
            return {};
        }
    }
    // ---- 레이어 ----
    if (cmd == "layer.move") { doc_->moveLayer(doc_->currentLayer(), int(num(1, 1))); return {}; }
    if (cmd == "layer.duplicate") { doc_->duplicateLayer(doc_->currentLayer()); return {}; }
    if (cmd == "layer.mergedown") { return doc_->mergeDown(doc_->currentLayer()) ? QString() : QStringLiteral("합칠 아래 레이어가 없습니다"); }
    if (cmd == "layer.blend") { doc_->layer(doc_->currentLayer()).blend = std::clamp(int(num(1)), 0, Blend::Count - 1); doc_->notifyStructure(); return {}; }
    if (cmd == "layer.lock") { doc_->layer(doc_->currentLayer()).locked = num(1) != 0; doc_->notifyStructure(); return {}; }
    // ---- 프레임 ----
    if (cmd == "frame.insert") { doc_->insertFrame(doc_->currentFrame()); return {}; }
    if (cmd == "frame.delete") { doc_->deleteFrame(doc_->currentFrame()); return {}; }
    if (cmd == "frame.next") { doc_->setCurrentFrame(doc_->currentFrame() + 1); return {}; }
    if (cmd == "frame.prev") { doc_->setCurrentFrame(doc_->currentFrame() - 1); return {}; }
    if (cmd == "frame.copy") { frameClip_ = doc_->copyFrames(doc_->currentLayer(), int(num(1)) - 1, int(num(2)) - 1, num(3) != 0); return {}; }
    if (cmd == "frame.paste") {
        if (frameClip_.isEmpty()) return QStringLiteral("복사한 프레임이 없습니다");
        return doc_->pasteFrames(doc_->currentLayer(), t.size() > 1 ? int(num(1)) - 1 : doc_->currentFrame(), frameClip_, num(2, 1) != 0)
                   ? QString() : QStringLiteral("붙여넣지 못했습니다");
    }
    if (cmd == "key.exposure") { doc_->setUniformExposure(doc_->currentLayer(), std::max(1, int(num(1, 1)))); return {}; }
    if (cmd == "key.reverse") { doc_->reverseKeys(doc_->currentLayer(), 0, doc_->frameCount() - 1); return {}; }
    if (cmd == "key.repeat") { doc_->repeatKeys(doc_->currentLayer(), int(num(1)) - 1, int(num(2)) - 1, std::max(1, int(num(3, 1))), num(4) != 0); return {}; }
    // ---- 카메라 ----
    if (cmd == "camera.on") { doc_->setCameraEnabled(true); return {}; }
    if (cmd == "camera.off") { doc_->setCameraEnabled(false); return {}; }
    if (cmd == "camera.key") {
        CameraKey k;
        k.x = num(2);
        k.y = num(3);
        k.scale = t.size() > 4 ? num(4) / 100.0 : 1.0;
        k.rotation = num(5);
        k.ease = int(num(6));
        doc_->setCameraKey(std::max(0, int(num(1)) - 1), k);
        return {};
    }
    // ---- 북마크 · 라이브러리 ----
    if (cmd == "bookmark") { doc_->toggleBookmark(t.size() > 1 ? int(num(1)) - 1 : doc_->currentFrame()); return {}; }
    if (cmd == "lib.add") {
        const int k = doc_->keyIndexAt(doc_->currentLayer(), doc_->currentFrame());
        if (k < 0) return QStringLiteral("현재 프레임에 이미지가 없습니다");
        const int id = doc_->addToLibrary(rest.isEmpty() ? QString("이미지 %1").arg(doc_->libraryCount() + 1) : rest,
                                          doc_->keyImage(doc_->currentLayer(), k));
        if (t.size() > 2) doc_->setLibraryCategory(id, t[2]);
        return {};
    }
    // ---- 저장 ----
    if (cmd == "save") { return doc_->save(rest) ? QString() : QStringLiteral("저장하지 못했습니다"); }
    if (cmd == "savecopy") { return doc_->saveCopy(rest) ? QString() : QStringLiteral("저장하지 못했습니다"); }
    if (cmd == "export.png") { return doc_->exportFrame(doc_->currentFrame(), rest) ? QString() : QStringLiteral("내보내지 못했습니다"); }
    if (host_) {
        const auto r = host_(t);
        if (r.has_value()) return *r;
    }
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
        if (host_) eng.setHost(host_);
        output_->setPlainText(eng.run(input_->toPlainText()));
    });
    connect(help, &QPushButton::clicked, this, [this] { output_->setPlainText(ScriptEngine::help()); });
}

// ---------------------------------------------------------------- 사용자 패널
CustomPanel::CustomPanel(Document* doc, const CustomPanelDef& def, QWidget* parent)
    : QWidget(parent), doc_(doc), def_(def) {
    setMinimumSize(140, 120);
    editBox_ = new QCheckBox(QStringLiteral("배치 편집"), this);
    editBox_->setToolTip(QStringLiteral("켜면 버튼을 끌어 옮기고, 오른쪽 아래 모서리를 끌어 크기를 바꿉니다."));
    editBox_->move(6, 4);
    connect(editBox_, &QCheckBox::toggled, this, [this](bool on) { setEditing(on); });
    arrangeBtn_ = new QPushButton(QStringLiteral("자동 정렬"), this);
    arrangeBtn_->setFixedHeight(22);
    arrangeBtn_->move(6, 26);
    arrangeBtn_->setVisible(false);
    connect(arrangeBtn_, &QPushButton::clicked, this, [this] { autoArrange(); });

    for (int i = 0; i < def_.labels.size(); ++i) {
        auto* b = new QPushButton(def_.labels[i], this);
        const QString script = def_.scripts.value(i);
        Document* d = doc_;
        connect(b, &QPushButton::clicked, this, [this, d, script] {
            if (editing_) return;        // 편집 중에는 실행하지 않는다
            ScriptEngine eng(d);
            if (host_) eng.setHost(host_);
            eng.run(script);
        });
        b->installEventFilter(this);
        buttons_.push_back(b);
    }
    applyGeometry();
}

void CustomPanel::applyGeometry() {
    const int top = 52;
    for (int i = 0; i < buttons_.size(); ++i) {
        QRect r;
        const QStringList p = def_.geoms.value(i).split(',');
        if (p.size() == 4) r = QRect(p[0].toInt(), p[1].toInt(), std::max(24, p[2].toInt()), std::max(18, p[3].toInt()));
        else r = QRect(8, top + i * 30, std::max(90, width() - 16), 26);
        buttons_[i]->setGeometry(r);
        buttons_[i]->show();
    }
}

void CustomPanel::storeGeometry() {
    def_.geoms.clear();
    for (QPushButton* b : buttons_) {
        const QRect r = b->geometry();
        def_.geoms << QString("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    }
    emit layoutChanged(def_);
}

void CustomPanel::autoArrange() {
    const int top = 52;
    for (int i = 0; i < buttons_.size(); ++i)
        buttons_[i]->setGeometry(8, top + i * 30, std::max(90, width() - 16), 26);
    storeGeometry();
    update();
}

void CustomPanel::setEditing(bool on) {
    editing_ = on;
    if (editBox_ && editBox_->isChecked() != on) {
        QSignalBlocker b(editBox_);
        editBox_->setChecked(on);
    }
    if (arrangeBtn_) arrangeBtn_->setVisible(on);
    for (QPushButton* b : buttons_) b->setCursor(on ? Qt::SizeAllCursor : Qt::ArrowCursor);
    update();
}

void CustomPanel::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    // 저장된 배치가 없을 때만 폭을 따라간다
    if (def_.geoms.isEmpty()) applyGeometry();
}

bool CustomPanel::eventFilter(QObject* o, QEvent* e) {
    if (!editing_) return QWidget::eventFilter(o, e);
    auto* b = qobject_cast<QPushButton*>(o);
    const int idx = b ? int(buttons_.indexOf(b)) : -1;
    if (idx < 0) return QWidget::eventFilter(o, e);
    if (e->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() != Qt::LeftButton) return true;
        dragIdx_ = idx;
        dragStart_ = me->globalPosition().toPoint();
        dragOrig_ = b->geometry();
        // 오른쪽 아래 12px 안이면 크기 바꾸기
        resizing_ = me->position().x() > b->width() - 12 && me->position().y() > b->height() - 12;
        return true;
    }
    if (e->type() == QEvent::MouseMove && dragIdx_ == idx) {
        auto* me = static_cast<QMouseEvent*>(e);
        const QPoint d = me->globalPosition().toPoint() - dragStart_;
        if (resizing_) {
            b->setGeometry(dragOrig_.x(), dragOrig_.y(), std::max(24, dragOrig_.width() + d.x()), std::max(18, dragOrig_.height() + d.y()));
        } else {
            b->move(std::max(0, dragOrig_.x() + d.x()), std::max(0, dragOrig_.y() + d.y()));
        }
        update();
        return true;
    }
    if (e->type() == QEvent::MouseButtonRelease && dragIdx_ == idx) {
        dragIdx_ = -1;
        resizing_ = false;
        storeGeometry();
        return true;
    }
    return QWidget::eventFilter(o, e);
}

void CustomPanel::paintEvent(QPaintEvent*) {
    if (!editing_) return;
    QPainter p(this);
    p.setPen(QPen(QColor(0x4a, 0x90, 0xe2, 160), 1, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    for (QPushButton* b : buttons_) {
        p.drawRect(b->geometry().adjusted(-1, -1, 0, 0));
        const QRect g = b->geometry();
        p.fillRect(QRect(g.right() - 8, g.bottom() - 8, 8, 8), QColor(0x4a, 0x90, 0xe2, 160));
    }
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
        d.geoms = st.value("geoms").toStringList();
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
        st.setValue("geoms", defs[i].geoms);
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
    auto* bar = new QWidget(this);
    auto* bh = new QHBoxLayout(bar);
    bh->setContentsMargins(0, 0, 0, 0);
    bh->addWidget(new QLabel(QStringLiteral("격자")));
    colBox_ = new QSpinBox;
    colBox_->setRange(1, 24);
    colBox_->setValue(cols_);
    colBox_->setPrefix(QStringLiteral("가로 "));
    rowBox_ = new QSpinBox;
    rowBox_->setRange(1, 24);
    rowBox_->setValue(rows_);
    rowBox_->setPrefix(QStringLiteral("세로 "));
    bh->addWidget(colBox_);
    bh->addWidget(rowBox_);
    auto* reset = new QPushButton(QStringLiteral("점 되돌리기"));
    bh->addWidget(reset);
    auto* orig = new QPushButton(QStringLiteral("원본 보기"));
    orig->setCheckable(true);
    bh->addWidget(orig);
    bh->addStretch();
    bar->move(20, 6);
    bar->resize(520, 28);
    connect(colBox_, &QSpinBox::valueChanged, this, [this](int v) { setGrid(v, rows_); });
    connect(rowBox_, &QSpinBox::valueChanged, this, [this](int v) { setGrid(cols_, v); });
    connect(reset, &QPushButton::clicked, this, [this] { resetPoints(); });
    connect(orig, &QPushButton::toggled, this, [this](bool on) {
        showOriginal_ = on;
        update();
    });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    bb->move(width() - 200, height() - 40);
    bb->setFixedWidth(190);
    bb->raise();
}

void MeshWarpDialog::resetPoints() {
    pts_.clear();
    for (int r = 0; r <= rows_; ++r)
        for (int c = 0; c <= cols_; ++c) pts_.push_back(QPointF(c * img_.width() / double(cols_), r * img_.height() / double(rows_)));
    preview_ = QImage();
    update();
}

void MeshWarpDialog::setGrid(int cols, int rows) {
    cols = std::clamp(cols, 1, 24);
    rows = std::clamp(rows, 1, 24);
    if (cols == cols_ && rows == rows_) return;
    // 이미 옮긴 모양을 최대한 유지한다: 지금 격자에서 같은 비율 위치의 변형량을 새 격자에 옮겨 담는다
    const QVector<QPointF> old = pts_;
    const int oc = cols_, orow = rows_;
    cols_ = cols;
    rows_ = rows;
    pts_.clear();
    for (int r = 0; r <= rows_; ++r) {
        for (int c = 0; c <= cols_; ++c) {
            const double u = double(c) / cols_, v = double(r) / rows_;
            const QPointF base(u * img_.width(), v * img_.height());
            // 옛 격자에서 (u,v) 위치의 변형량을 쌍선형으로 읽는다
            const double fx = u * oc, fy = v * orow;
            const int x0 = std::clamp(int(fx), 0, oc), y0 = std::clamp(int(fy), 0, orow);
            const int x1 = std::min(x0 + 1, oc), y1 = std::min(y0 + 1, orow);
            const double tx = fx - x0, ty = fy - y0;
            auto at = [&](int cx, int cy) {
                const int idx = cy * (oc + 1) + cx;
                if (idx < 0 || idx >= old.size()) return QPointF(0, 0);
                return old[idx] - QPointF(double(cx) / oc * img_.width(), double(cy) / orow * img_.height());
            };
            const QPointF d = at(x0, y0) * (1 - tx) * (1 - ty) + at(x1, y0) * tx * (1 - ty) +
                              at(x0, y1) * (1 - tx) * ty + at(x1, y1) * tx * ty;
            pts_.push_back(base + d);
        }
    }
    if (colBox_ && colBox_->value() != cols_) {
        QSignalBlocker b(colBox_);
        colBox_->setValue(cols_);
    }
    if (rowBox_ && rowBox_->value() != rows_) {
        QSignalBlocker b(rowBox_);
        rowBox_->setValue(rows_);
    }
    preview_ = QImage();
    update();
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
    if (showOriginal_) {
        p.drawImage(r, img_);
    } else {
        if (preview_.isNull() || preview_.size() != img_.size()) preview_ = result();
        p.drawImage(r, preview_);
    }
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
