#pragma once
// 라이선스(유료화) 모듈: Cloudflare Workers + D1/KV 로 만든 가벼운 라이선스 서버와 짝을 이룬다.
//
// 동작 방식 (요약):
//  1) 사용자가 받은 라이선스 키를 입력하면 서버의 /activate 에 (키, 이 기기의 고유 ID)를 보낸다.
//  2) 서버는 키가 유효하고(취소/만료 안 됐고, 활성화 기기 수 한도 안이면) 플랜·만료 시각과 함께
//     HMAC-SHA256 서명을 돌려준다. 이 서명은 서버만 아는 비밀키로 만들어서, 클라이언트가 값을
//     마음대로 바꿔 "검증 통과"로 위조할 수 없게 막는다.
//  3) 클라이언트는 이 서명이 붙은 결과를 로컬에 저장해 두고, 앱을 켤 때마다는 네트워크 없이
//     저장된 서명만 다시 계산해서 맞춰 보는 것으로 "지금 쓸 수 있는지"를 빠르게 판단한다
//     (완전 오프라인 사용이 가능하되, 저장된 유효기간이 지나면 다시 서버 확인이 필요하다).
//  4) 주기적으로(예: 유효기간이 며칠 안 남았을 때) 서버의 /validate 를 다시 불러 새 서명을 받는다.
//     이때 서버가 "취소됨"으로 답하면 그 즉시 라이선스가 꺼진다 — 이것이 오프라인 키만으로는
//     안 되던 "회수(revoke)"를 가능하게 하는 지점이다.
//
// 주의: 서명에 쓰는 비밀키(kSharedSecretHex, 아래)는 대칭키라 실행 파일 안에 들어간다.
// 즉 바이너리를 분해해 보면 이 값을 찾을 수 있어서, 아주 집요한 사용자는 자기 컴퓨터에서
// 서명을 직접 만들어 라이선스를 위조할 수 있다 (비대칭키인 Ed25519 라면 이 공격이 안 통하지만,
// 별도 암호 라이브러리가 필요해서 빌드가 복잡해진다). 캐주얼한 복제·공유를 막는 수준으로는
// 충분하지만, 완전히 뚫리지 않는 보호는 아니라는 점은 분명히 알고 있어야 한다.
// 실제 배포 전에는 이 값을 반드시 새로 바꾸고, 서버 쪽 Worker 환경 변수(SECRET)도 같은 값으로
// 맞춰야 한다 (server/README.md 참고).

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QByteArray>

class QNetworkAccessManager;
class QNetworkReply;
class QJsonObject;
class QWidget;

namespace License {

// 서버와 공유하는 비밀키 (HMAC-SHA256). 배포 전 반드시 새 값으로 교체하고 서버 secret 도 맞출 것.
inline constexpr const char* kSharedSecretHex = "b0989dc6bc0cd01adcfcb08652df2077ce9dd2460ded10902eaf17b8c323b0f3";

// 라이선스 서버(Worker) 기본 주소. 배포한 뒤 실제 주소로 바꾸거나, setServerUrl() 로 덮어쓸 수 있다.
inline constexpr const char* kDefaultServerUrl = "https://license.jaesong.kr";

// 이 컴퓨터를 나타내는 고유 ID. 개인정보(이름·IP 등)는 들어가지 않고, 해시된 값만 쓴다.
QString machineId();

// 지금 저장된 라이선스 상태를 네트워크 없이 판단한다 (서명만 로컬에서 다시 검증).
struct Status {
    bool licensed = false;        // 지금 이 기능을 써도 되는지
    bool knownBad = false;        // 서버가 명확히 "취소/만료"라고 답한 적이 있는지 (키가 없는 것과는 다름)
    QString plan;                  // 서버가 내려준 플랜 이름 ("standard", "pro" 등)
    QString licenseKey;            // 저장된 라이선스 키 (일부만 보여줄 때 사용)
    QDateTime expiresAt;           // 오프라인으로 버틸 수 있는 한도 시각
    QString message;               // 화면에 보여줄 한 줄 설명
    bool contactConsentGiven = false; // 연락처(이메일) 수집에 동의했는지 (로컬에 남겨 둔 값, 참고용)
    QString contactEmail;              // 동의했을 때 입력한 이메일 (로컬 echo, 서버에 저장된 것과는 별개로 보여주기용)
};
Status currentStatus();

// 저장된 라이선스 정보를 모두 지운다 (다른 키로 바꾸거나, 이 기기에서 라이선스를 뺄 때).
void clearLicense();

// 서버 주소를 읽고/설정한다 (QSettings 에 저장).
QString serverUrl();
void setServerUrl(const QString& url);

// 서버와 통신해서 활성화/재검증을 수행한다. Qt 네트워크는 비동기라 완료 시 신호로 알려준다.
class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject* parent = nullptr);

    // 새 라이선스 키를 이 기기에 등록한다 (서버 POST /activate).
    // contactEmail·consent 는 "라이선스 무단 사용이 의심될 때 연락"을 위한 선택 정보다.
    // consent 가 true 이고 contactEmail 이 비어 있지 않을 때만 서버로 보내 저장한다 — 동의 없이는
    // 아무것도 보내지 않는다 (서버 쪽에서도 같은 조건을 한 번 더 검사해 이중으로 막아 둔다).
    void activate(const QString& licenseKey, const QString& contactEmail = QString(), bool consent = false);
    // 지금 저장된 키로 서버에 다시 확인한다 (서버 POST /validate). 주기적 재검증·취소 반영용.
    void revalidate();
    // 이 기기에서 라이선스를 반납한다 (서버 POST /deactivate, 다른 기기에서 쓸 수 있게 자리 비우기).
    void deactivate();
    // 이미 동의하고 보낸 연락처 정보를 서버에서 지워 달라고 요청한다 (활성화 자체는 그대로 유지된다).
    void withdrawContact();

signals:
    void activated(bool ok, const QString& message);
    void revalidated(bool ok, const QString& message);
    void deactivated(bool ok, const QString& message);
    void contactWithdrawn(bool ok, const QString& message);

private:
    QNetworkAccessManager* nam_;
    // 서버 응답(license_key/machine_id/plan/issued_at/expires_at + sig)을 검증하고 로컬에 저장한다.
    bool applyGrant(const QJsonObject& obj, QString* err);
};

// 서명 검증 로직만 네트워크 없이 테스트할 수 있도록 뽑아 둔 함수 (selftest 용).
// canonical = "license_key|machine_id|plan|issued_at|expires_at" 형태의 문자열.
QByteArray hmacHex(const QByteArray& canonical, const QByteArray& secretHex);

// 라이선스 등록·확인 대화상자를 띄워, 쓸 수 있는 상태가 될 때까지 진행한다 (프로그램 시작 시 호출용).
// 이미 유효한 라이선스가 있으면 창 없이 곧바로 true 를 돌려준다(필요하면 뒤에서 조용히 재검증만 건다).
// 사용자가 "종료"를 누르면 false 를 돌려주며, 호출한 쪽에서 프로그램을 끝내야 한다.
bool ensureLicensed(QWidget* parent);

} // namespace License
