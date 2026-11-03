# ONY 라이선스 서버 (Cloudflare Workers + D1 + KV)

ONY 클라이언트(메뉴: 도움말 → 라이선스 정보, 프로그램 시작 시 뜨는 등록 창)가 이 서버에 붙어서
라이선스 키를 활성화/재검증합니다. 서버는 D1(SQLite 호환)에 라이선스·활성화 기기 목록을 저장하고,
KV는 가벼운 캐시(취소/만료 결과를 잠깐 기억해 D1 읍답 줄이기)에만 씁니다 — KV 없이도 동작합니다.

## 0. 준비

```bash
cd server
npm install           # wrangler 설치
npx wrangler login    # 브라우저로 Cloudflare 계정 로그인
```

## 1. D1 데이터베이스 만들기

```bash
npx wrangler d1 create ony-license
```

출력에 나오는 `database_id` 값을 `wrangler.toml` 의 `database_id = "..."` 자리에 붙여넣습니다.

스키마 적용:

```bash
npm run db:init:remote   # 실제 배포된 DB에 테이블 생성 (schema.sql)
```

## 2. KV 네임스페이스 만들기 (선택, 권장)

```bash
npx wrangler kv namespace create ony-license-kv
```

출력에 나오는 `id` 값을 `wrangler.toml` 의 `[[kv_namespaces]]` 쪽 `id = "..."` 자리에 붙여넣습니다.
(KV 를 안 쓰고 싶으면 `wrangler.toml` 에서 `[[kv_namespaces]]` 블록을 지우면 됩니다 — 코드가 `env.KV` 존재 여부를 확인하므로 그대로 동작합니다.)

## 3. 비밀값 설정

```bash
npx wrangler secret put SECRET
# ONY 클라이언트 소스(src/License.h 의 kSharedSecretHex)와 "정확히 같은" 값을 입력해야 합니다.
# 지금 저장소에 기본값으로 들어 있는 건 개발용 예시 값이라, 실제 배포 전에
# 아래 명령으로 새로 만든 값으로 양쪽(서버 secret + 클라이언트 소스)을 같이 바꾸세요.
#   python3 -c "import secrets; print(secrets.token_hex(32))"

npx wrangler secret put ADMIN_KEY
# 라이선스를 발급/취소할 때 쓰는 관리자 키. 아무 긴 임의 문자열이면 됩니다. 이것도 직접 보관하세요.
```

## 4. 배포

```bash
npm run deploy
```

배포가 끝나면 `https://ony-license.<당신의-서브도메인>.workers.dev` 같은 주소가 나옵니다.
ONY 쪽에서는 메뉴 **도움말 → 라이선스 정보** 를 열기 전에, 설정에서 서버 주소를 이 값으로 지정해야 합니다
(지금은 `src/License.h` 의 `kDefaultServerUrl` 을 이 주소로 바꿔서 다시 빌드하는 게 가장 간단합니다.
런타임에 바꾸고 싶다면 코드에서 `License::setServerUrl(...)` 를 호출하는 설정 화면을 하나 더 추가하면 됩니다).

## 5. 라이선스 키 발급 (판매할 때마다)

```bash
curl -X POST https://ony-license.<서브도메인>.workers.dev/admin/create \
  -H "x-admin-key: <3단계에서 만든 ADMIN_KEY>" \
  -H "content-type: application/json" \
  -d '{"email":"customer@example.com","plan":"standard","max_activations":2}'
```

응답 예:

```json
{"ok":true,"license_key":"ONY-AB12-CD34-EF56-GH78","plan":"standard","max_activations":2,"expires_at":null}
```

이 `license_key` 를 구매자에게 전달하면, ONY 프로그램 첫 실행 시 뜨는 등록 창에 입력해서 쓸 수 있습니다.

- `max_activations` : 동시에 활성화할 수 있는 기기 수 (가정용 PC+노트북이면 2 정도 추천).
- `expires_at` : 구독형이면 `"2027-01-01T00:00:00Z"` 같은 값을 지정 (정기적으로 갱신 처리 필요), 평생 라이선스면 생략(null).

## 6. 라이선스 취소하기

```bash
curl -X POST https://ony-license.<서브도메인>.workers.dev/admin/revoke \
  -H "x-admin-key: <ADMIN_KEY>" -H "content-type: application/json" \
  -d '{"license_key":"ONY-AB12-CD34-EF56-GH78"}'
```

ONY 클라이언트는 평소엔 오프라인 캐시(최대 14일, `TOKEN_TTL_DAYS`)로 동작하다가, 그 안에 한 번이라도
서버에 다시 접속하면(주기적 자동 재검증, 또는 사용자가 "지금 다시 확인"을 누르면) 이 취소가 즉시 반영됩니다.

## 7. 특정 라이선스 상태 조회

```bash
curl "https://ony-license.<서브도메인>.workers.dev/admin/license?key=ONY-AB12-CD34-EF56-GH78" \
  -H "x-admin-key: <ADMIN_KEY>"
```

## 로컬에서 먼저 테스트하고 싶다면

```bash
npm run dev          # 로컬 서버 (http://localhost:8787)
npm run db:init      # 로컬 D1 에 스키마 적용 (--remote 없이)
```

이후 ONY 클라이언트의 `kDefaultServerUrl` 을 `http://localhost:8787` 로 잠깐 바꿔서 활성화 테스트를 해볼 수 있습니다.

## 연락처(이메일) 수집 — 라이선스 무단 사용 대응용

클라이언트의 라이선스 등록 창에는 "연락처 수집에 동의합니다" 체크박스 + 이메일 입력칸이 있습니다.
사용자가 **직접 체크하고 이메일을 적은 경우에만** `/activate` 요청에 `contact_email`/`consent`가
같이 실려오고, 서버는 그걸 그 기기의 `activations` 행에 저장합니다 (동의 없이 보낸 이메일은 서버
쪽에서도 다시 한번 조건을 확인해서 통째로 버립니다 — 코드 `handleActivate`의 `consentGiven` 부분).

- 이미 만들어 둔 DB라면 `server/schema.sql` 맨 아래 적힌 `ALTER TABLE` 두 줄을 먼저 실행해야
  새 컬럼(`contact_email`, `consent_at`)이 생깁니다.
- 용도는 "이 키가 무단으로 여러 곳에서 쓰이는 것 같을 때 연락"으로만 제한하세요 — 수집한 이메일로
  마케팅을 보내거나 다른 목적으로 쓰면 고지한 수집 목적을 벗어나는 것이라 문제가 될 수 있습니다.
- 사용자가 ONY 쪽 "라이선스 정보 → 연락처 정보 삭제"를 누르면 `/withdraw-contact`가 호출되어
  그 기기의 이메일/동의 시각만 지워집니다 (라이선스 활성화 자체는 그대로 유지).
- `/admin/license?key=...` 조회 결과에 기기별 `contact_email`/`consent_at`이 같이 나옵니다 — 의심
  사례 조사할 때 여기서 확인하면 됩니다.

## 알아둘 점 (한계)

- 서명은 **대칭키(HMAC-SHA256)** 방식입니다. 클라이언트 실행 파일 안에 비밀키가 들어 있어서, 아주
  집요한 사용자는 실행 파일을 분해해 이 키를 찾아내 스스로 "유효함" 서명을 만들어낼 수 있습니다.
  캐주얼한 복제·공유는 충분히 막지만, 작정하고 크래킹하는 공격까지는 못 막습니다. 더 강한 보호가
  필요해지면 Ed25519(비대칭키) 서명으로 바꾸는 게 다음 단계입니다 (클라이언트는 공개키만 가지면 되므로
  키를 추출해도 위조 서명을 만들 수 없습니다) — 다만 암호 라이브러리를 새로 붙여야 해서 빌드가 복잡해집니다.
- Cloudflare Workers Free 플랜은 하루 100,000 요청, D1 Free 는 하루 500만 행 읽기/하루 10만 행 쓰기까지
  무료입니다. 개인/소규모 판매 규모에서는 두 한도 다 여유 있게 남습니다.
