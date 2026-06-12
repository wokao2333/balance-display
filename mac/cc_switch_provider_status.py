#!/usr/bin/env python3
import json
import re
import sqlite3
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen
from pathlib import Path
from typing import Any, Optional


DEFAULT_DB_PATH = Path.home() / ".cc-switch" / "cc-switch.db"
DEFAULT_CACHE_PATH = Path.home() / ".cc-switch" / "ble-display-status-cache.json"


def now_ms() -> int:
    return int(time.time() * 1000)


def format_refresh_time(value_ms: int) -> str:
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(value_ms / 1000))


def read_current_provider(db_path: Path = DEFAULT_DB_PATH, app_type: str = "codex") -> Optional[dict]:
    query = """
        SELECT id, app_type, name, category, provider_type, meta
        FROM providers
        WHERE app_type = ? AND is_current = 1
        LIMIT 1
    """
    conn = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True, timeout=5)
    conn.row_factory = sqlite3.Row
    try:
        row = conn.execute(query, (app_type,)).fetchone()
    finally:
        conn.close()

    if row is None:
        return None

    payload = {
        "providerId": row["id"],
        "providerName": row["name"],
        "status": "using",
        "updatedAt": now_ms(),
    }

    try:
        meta = json.loads(row["meta"] or "{}")
    except json.JSONDecodeError:
        meta = {}

    cache_key = f"{row['app_type']}:{row['id']}"
    usage = with_cached_usage(cache_key, fetch_usage(meta.get("usage_script") or {}))
    payload.update(usage)
    refreshed_at = now_ms()
    payload["refreshedAt"] = refreshed_at
    payload["refreshedAtText"] = format_refresh_time(refreshed_at)
    return payload


def read_usage_cache(cache_path: Path = DEFAULT_CACHE_PATH) -> dict:
    try:
        return json.loads(cache_path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return {}


def write_usage_cache(cache: dict, cache_path: Path = DEFAULT_CACHE_PATH) -> None:
    cache_path.parent.mkdir(parents=True, exist_ok=True)
    cache_path.write_text(
        json.dumps(cache, ensure_ascii=False, separators=(",", ":")),
        encoding="utf-8",
    )


def cacheable_usage_fields(usage: dict) -> dict:
    cached = {}
    for key in ("balanceText", "currency", "resetAt"):
        if usage.get(key):
            cached[key] = usage[key]
    if cached:
        cached["cachedAt"] = now_ms()
    return cached


def with_cached_usage(cache_key: str, usage: dict) -> dict:
    current_ms = now_ms()
    cache = read_usage_cache()
    fresh = cacheable_usage_fields(usage)
    if fresh:
        cache[cache_key] = fresh
        write_usage_cache(cache)
        return usage

    cached = cache.get(cache_key)
    if not isinstance(cached, dict):
        return usage

    usage = dict(usage)
    for key in ("balanceText", "currency", "resetAt"):
        if cached.get(key) and not usage.get(key):
            usage[key] = cached[key]
    if usage.get("balanceText"):
        usage["usageCached"] = True
        if cached.get("cachedAt"):
            usage["usageCachedAt"] = cached["cachedAt"]
            usage["ageSeconds"] = max(0, int((current_ms - int(cached["cachedAt"])) / 1000))
        usage["updatedAt"] = current_ms
    return usage


def first_present(*values: Any) -> Any:
    for value in values:
        if value is not None:
            return value
    return None


def deep_get(data: Any, *path: str) -> Any:
    current = data
    for key in path:
        if not isinstance(current, dict):
            return None
        current = current.get(key)
    return current


def format_balance(value: Any) -> Optional[str]:
    try:
        return f"{float(value):.2f}"
    except (TypeError, ValueError):
        return None


def template(value: str, usage_script: dict) -> str:
    return (
        value.replace("{{baseUrl}}", str(usage_script.get("baseUrl") or "").rstrip("/"))
        .replace("{{apiKey}}", str(usage_script.get("apiKey") or ""))
    )


def extract_object_body(code: str, key: str) -> Optional[str]:
    match = re.search(rf"\b{re.escape(key)}\s*:\s*\{{", code, re.S)
    if not match:
        return None

    start = match.end() - 1
    depth = 0
    quote = None
    escaped = False
    for index in range(start, len(code)):
        character = code[index]
        if quote:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = None
            continue

        if character in "\"'`":
            quote = character
        elif character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
            if depth == 0:
                return code[start + 1 : index]
    return None


def extract_request_config(code: str, usage_script: dict) -> tuple[str, str, dict]:
    url_match = re.search(r"url\s*:\s*([\"'`])(.+?)\1", code, re.S)
    method_match = re.search(r"method\s*:\s*([\"'`])(.+?)\1", code, re.S)
    header_body = extract_object_body(code, "headers")

    url = template(url_match.group(2), usage_script) if url_match else ""
    method = (method_match.group(2) if method_match else "GET").upper()
    headers = {}

    if header_body:
        header_pairs = re.findall(
            r"(?:([A-Za-z0-9_-]+)|([\"'`])([^\"'`]+)\2)\s*:\s*([\"'`])(.+?)\4",
            header_body,
            re.S,
        )
        for bare_key, _key_quote, quoted_key, _value_quote, value in header_pairs:
            key = bare_key or quoted_key
            headers[key] = template(value, usage_script)

    return url, method, headers


def normalize_usage_response(response: Any) -> dict:
    data = response.get("data") if isinstance(response, dict) and isinstance(response.get("data"), dict) else response

    remaining = first_present(
        deep_get(data, "remaining"),
        deep_get(data, "quota", "remaining"),
        deep_get(data, "balance"),
        deep_get(response, "remaining"),
        deep_get(response, "quota", "remaining"),
        deep_get(response, "balance"),
    )
    currency = first_present(
        deep_get(data, "unit"),
        deep_get(data, "quota", "unit"),
        deep_get(data, "currency"),
        deep_get(response, "unit"),
        deep_get(response, "quota", "unit"),
        deep_get(response, "currency"),
        "USD",
    )
    is_valid = first_present(
        deep_get(data, "is_active"),
        deep_get(data, "isValid"),
        deep_get(response, "is_active"),
        deep_get(response, "isValid"),
        True,
    )
    reset_at = first_present(
        deep_get(data, "resetAt"),
        deep_get(data, "quota", "resetAt"),
        deep_get(data, "nextResetAt"),
        deep_get(data, "refreshAt"),
        deep_get(response, "resetAt"),
        deep_get(response, "quota", "resetAt"),
        deep_get(response, "nextResetAt"),
        deep_get(response, "refreshAt"),
    )

    balance_text = format_balance(remaining)
    normalized = {"updatedAt": now_ms(), "ageSeconds": 0}
    if not bool(is_valid):
        normalized["usageOk"] = False
    if balance_text is not None:
        normalized["balanceText"] = balance_text
    if currency:
        normalized["currency"] = currency
    if reset_at:
        normalized["resetAt"] = reset_at
    return normalized


def fetch_usage(usage_script: dict) -> dict:
    if not usage_script.get("enabled"):
        return {"usageOk": False, "usageError": "usage script disabled"}

    code = usage_script.get("code") or ""
    url, method, headers = extract_request_config(code, usage_script)
    if not url:
        return {"usageOk": False, "usageError": "usage URL missing"}

    timeout = float(usage_script.get("timeout") or 10)
    try:
        request_headers = {"User-Agent": "cc-switch-ble-display/1.0"}
        request_headers.update(headers)
        request = Request(url, headers=request_headers, method=method)
        with urlopen(request, timeout=timeout) as response:
            raw = response.read().decode("utf-8")
        data = json.loads(raw)
        return normalize_usage_response(data)
    except (HTTPError, URLError, TimeoutError, json.JSONDecodeError, OSError) as exc:
        return {
            "usageOk": False,
            "usageError": type(exc).__name__,
            "updatedAt": now_ms(),
            "ageSeconds": 0,
        }


def main() -> int:
    provider = read_current_provider()
    if provider is None:
        print("No current codex provider found")
        return 2
    print(json.dumps(provider, ensure_ascii=False, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
