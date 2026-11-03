-- ONY 라이선스 서버 D1 스키마.
-- 적용: wrangler d1 execute ony-license --file=./schema.sql   (--remote 를 붙이면 실제 배포된 DB에 적용)

CREATE TABLE IF NOT EXISTS licenses (
  license_key      TEXT PRIMARY KEY,   -- 예: ONY-AB12-CD34-EF56-GH78
  email            TEXT,               -- 구매자 메모용 (선택)
  plan             TEXT NOT NULL DEFAULT 'standard',
  max_activations  INTEGER NOT NULL DEFAULT 1,  -- 동시에 쓸 수 있는 기기 수
  revoked          INTEGER NOT NULL DEFAULT 0,  -- 1 이면 즉시 사용 불가 (환불/도용 등)
  expires_at       TEXT,               -- ISO8601, NULL 이면 영구 라이선스
  created_at       TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS activations (
  license_key    TEXT NOT NULL,
  machine_id     TEXT NOT NULL,         -- ONY 클라이언트가 보내는, 해시된 기기 ID (개인정보 아님)
  activated_at   TEXT NOT NULL,
  last_seen      TEXT NOT NULL,
  contact_email  TEXT,                  -- 사용자가 동의 체크 후 직접 입력한 경우에만 (무단 사용 의심 시 연락용)
  consent_at     TEXT,                  -- 동의한 시각. NULL이면 이메일을 받은 적 없다는 뜻
  PRIMARY KEY (license_key, machine_id),
  FOREIGN KEY (license_key) REFERENCES licenses(license_key)
);

-- 이미 만들어 둔 DB에 이 컬럼들만 추가하려면 (기존 테이블은 그대로 두고 컬럼만 더하기):
--   ALTER TABLE activations ADD COLUMN contact_email TEXT;
--   ALTER TABLE activations ADD COLUMN consent_at TEXT;

CREATE INDEX IF NOT EXISTS idx_activations_key ON activations(license_key);
