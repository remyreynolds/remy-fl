PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS sessions (
    session_id TEXT PRIMARY KEY,
    user_id TEXT NOT NULL DEFAULT 'default',
    key TEXT,
    bpm REAL,
    loop_bars INTEGER,
    subgenre TEXT,
    elements_json TEXT NOT NULL DEFAULT '{}',
    last_plan_json TEXT NOT NULL DEFAULT '{}',
    summary TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    closed_at TEXT
);

CREATE TABLE IF NOT EXISTS generations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    request TEXT NOT NULL DEFAULT '',
    raw_response TEXT NOT NULL DEFAULT '',
    output_json TEXT,
    validation_ok INTEGER NOT NULL DEFAULT 0,
    validation_errors TEXT NOT NULL DEFAULT '',
    latency_ms INTEGER NOT NULL DEFAULT 0,
    kept INTEGER NOT NULL DEFAULT 0,
    session_id TEXT,
    user_id TEXT NOT NULL DEFAULT 'default',
    element TEXT,
    intent_json TEXT NOT NULL DEFAULT '{}',
    context_json TEXT NOT NULL DEFAULT '{}',
    plan_json TEXT NOT NULL DEFAULT '{}',
    warnings_json TEXT NOT NULL DEFAULT '[]',
    fingerprint_ids_json TEXT NOT NULL DEFAULT '[]',
    trace_id TEXT,
    revision_count INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_generations_session_element
ON generations(session_id, element, created_at DESC);

CREATE VIRTUAL TABLE IF NOT EXISTS generations_fts USING fts5(
    generation_id UNINDEXED,
    prompt,
    plan
);

CREATE TABLE IF NOT EXISTS feedback (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    generation_id INTEGER,
    session_id TEXT,
    user_id TEXT NOT NULL DEFAULT 'default',
    rating INTEGER NOT NULL DEFAULT 0,
    text TEXT NOT NULL DEFAULT '',
    diff_json TEXT NOT NULL DEFAULT '{}',
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(generation_id) REFERENCES generations(id)
);

CREATE TABLE IF NOT EXISTS critic_verdicts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    generation_id INTEGER,
    trace_id TEXT,
    session_id TEXT,
    element TEXT,
    attempt INTEGER NOT NULL,
    code_passed INTEGER NOT NULL,
    code_errors_json TEXT NOT NULL DEFAULT '[]',
    scores_json TEXT NOT NULL DEFAULT '{}',
    revision_note TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(generation_id) REFERENCES generations(id)
);
CREATE INDEX IF NOT EXISTS idx_verdict_session_element
ON critic_verdicts(session_id, element, created_at DESC);

CREATE TABLE IF NOT EXISTS fingerprints (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    track TEXT,
    subgenre TEXT,
    bpm REAL,
    key TEXT,
    mode TEXT,
    data_json TEXT NOT NULL,
    kept_rate REAL NOT NULL DEFAULT 0.5,
    use_count INTEGER NOT NULL DEFAULT 0,
    search_text TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_fp_subgenre ON fingerprints(subgenre);
CREATE INDEX IF NOT EXISTS idx_fp_bpm ON fingerprints(bpm);
CREATE INDEX IF NOT EXISTS idx_fp_key ON fingerprints(key);

CREATE VIRTUAL TABLE IF NOT EXISTS fingerprints_fts USING fts5(
    fingerprint_id UNINDEXED,
    track,
    subgenre,
    search_text
);

CREATE TABLE IF NOT EXISTS taste_profiles (
    user_id TEXT PRIMARY KEY,
    preferred_keys_json TEXT NOT NULL DEFAULT '{}',
    swing_bias REAL NOT NULL DEFAULT 0,
    density_bias REAL NOT NULL DEFAULT 0,
    brightness REAL NOT NULL DEFAULT 0,
    complexity REAL NOT NULL DEFAULT 3,
    banned_moves_json TEXT NOT NULL DEFAULT '[]',
    confidence_json TEXT NOT NULL DEFAULT '{}',
    evidence_count INTEGER NOT NULL DEFAULT 0,
    data_json TEXT NOT NULL DEFAULT '{}',
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS mood_table (
    word TEXT PRIMARY KEY,
    params_json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS genre_cards (
    subgenre TEXT PRIMARY KEY,
    card_json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS sound_types (
    type TEXT PRIMARY KEY,
    constraints_json TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS heuristics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id TEXT NOT NULL DEFAULT 'default',
    trigger_condition TEXT NOT NULL,
    rule TEXT NOT NULL,
    evidence INTEGER NOT NULL DEFAULT 1,
    contradictions INTEGER NOT NULL DEFAULT 0,
    active INTEGER NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(user_id, trigger_condition, rule)
);

CREATE TABLE IF NOT EXISTS traces (
    trace_id TEXT PRIMARY KEY,
    session_id TEXT,
    request TEXT NOT NULL,
    stages_json TEXT NOT NULL DEFAULT '[]',
    models_json TEXT NOT NULL DEFAULT '{}',
    total_tokens INTEGER NOT NULL DEFAULT 0,
    verdict_scores_json TEXT NOT NULL DEFAULT '{}',
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_at TEXT
);

CREATE TABLE IF NOT EXISTS context_cache (
    cache_key TEXT PRIMARY KEY,
    session_id TEXT NOT NULL,
    intent_hash TEXT NOT NULL,
    context_json TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS session_summaries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT NOT NULL,
    user_id TEXT NOT NULL,
    summary TEXT NOT NULL,
    generation_count INTEGER NOT NULL,
    closed_at TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

