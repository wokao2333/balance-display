#!/usr/bin/env python3
import argparse
import html
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "esp32" / "cc_switch_display_ble" / "cc_switch_display_ble.ino"
DEFAULT_OUT = ROOT / "scripts" / "ui_preview.svg"


def load_constants() -> tuple[dict[str, int], dict[str, str]]:
    source = FIRMWARE.read_text(encoding="utf-8")
    values: dict[str, int] = {}
    colors: dict[str, str] = {}

    for name, r, g, b in re.findall(
        r"static const uint16_t (COLOR_[A-Z_]+) = RGB565\((\d+),\s*(\d+),\s*(\d+)\);",
        source,
    ):
        colors[name] = f"#{int(r):02x}{int(g):02x}{int(b):02x}"

    pattern = re.compile(r"static const int16_t ([A-Z_]+) = ([^;]+);")
    changed = True
    while changed:
        changed = False
        for name, expr in pattern.findall(source):
            if name in values:
                continue
            safe_expr = expr.strip()
            try:
                value = int(eval(safe_expr, {"__builtins__": {}}, values))
            except Exception:
                continue
            values[name] = value
            changed = True

    return values, colors


def short_text(value: str, max_chars: int) -> str:
    if len(value) <= max_chars:
        return value
    if max_chars <= 3:
        return value[:max_chars]
    return value[: max_chars - 3] + "..."


def status_text(status: str) -> str:
    if status == "using":
        return "使用中"
    if status == "error":
        return "异常"
    if status == "inactive":
        return "停用"
    return short_text(status, 8)


def render(payload: dict) -> str:
    c, color = load_constants()
    age = payload.get("ageText") or "9 分钟前"
    balance = short_text(str(payload.get("balanceText") or "--"), 7)
    currency = short_text(str(payload.get("currency") or "USD"), 3)
    status = status_text(str(payload.get("status") or "using"))

    unit_x = c["BALANCE_VALUE_X"] + len(balance) * 18 + 14
    max_unit_x = c["STATUS_PILL_X"] - 58
    unit_x = min(unit_x, max_unit_x)

    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="480" height="320" viewBox="0 0 480 320">
  <rect width="480" height="320" fill="{color['COLOR_BG']}"/>
  <rect x="{c['CARD_X']}" y="{c['CARD_Y']}" width="{c['CARD_W']}" height="{c['CARD_H']}" rx="20" fill="{color['COLOR_CARD']}"/>

  <circle cx="{c['AGE_ICON_CX']}" cy="{c['AGE_ICON_CY']}" r="9" fill="none" stroke="{color['COLOR_LIGHT_MUTED']}" stroke-width="2"/>
  <path d="M {c['AGE_ICON_CX']} {c['AGE_ICON_CY']} L {c['AGE_ICON_CX']} {c['AGE_ICON_CY'] - 5} M {c['AGE_ICON_CX']} {c['AGE_ICON_CY']} L {c['AGE_ICON_CX'] + 5} {c['AGE_ICON_CY'] + 2}" stroke="{color['COLOR_LIGHT_MUTED']}" stroke-width="2" stroke-linecap="round"/>
  <text x="{c['AGE_TEXT_X']}" y="{c['AGE_TEXT_BASELINE']}" fill="{color['COLOR_LIGHT_MUTED']}" font-family="-apple-system,BlinkMacSystemFont,'PingFang SC',sans-serif" font-size="22">{html.escape(age)}</text>

  <path d="M {c['REFRESH_ICON_CX'] + 10} {c['REFRESH_ICON_CY'] - 9} A 12 10 0 1 0 {c['REFRESH_ICON_CX'] - 9} {c['REFRESH_ICON_CY'] + 8}" fill="none" stroke="{color['COLOR_MUTED']}" stroke-width="2"/>
  <path d="M {c['REFRESH_ICON_CX'] + 10} {c['REFRESH_ICON_CY'] - 9} L {c['REFRESH_ICON_CX'] + 15} {c['REFRESH_ICON_CY'] - 6} L {c['REFRESH_ICON_CX'] + 10} {c['REFRESH_ICON_CY'] - 2} Z" fill="{color['COLOR_MUTED']}"/>
  <path d="M {c['REFRESH_ICON_CX'] - 9} {c['REFRESH_ICON_CY'] + 8} L {c['REFRESH_ICON_CX'] - 13} {c['REFRESH_ICON_CY'] + 5}" stroke="{color['COLOR_MUTED']}" stroke-width="2" stroke-linecap="round"/>

  <rect x="{c['STATUS_PILL_X']}" y="{c['STATUS_PILL_Y']}" width="{c['STATUS_PILL_W']}" height="{c['STATUS_PILL_H']}" rx="14" fill="{color['COLOR_PILL']}"/>
  <path d="M {c['STATUS_PILL_X'] + 20} {c['STATUS_PILL_Y'] + 29} L {c['STATUS_PILL_X'] + 26} {c['STATUS_PILL_Y'] + 35} L {c['STATUS_PILL_X'] + 38} {c['STATUS_PILL_Y'] + 20}" fill="none" stroke="{color['COLOR_LIGHT_MUTED']}" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/>
  <text x="{c['STATUS_PILL_X'] + 52}" y="{c['STATUS_PILL_Y'] + 37}" fill="{color['COLOR_LIGHT_MUTED']}" font-family="-apple-system,BlinkMacSystemFont,'PingFang SC',sans-serif" font-size="22" font-weight="600">{html.escape(status)}</text>

  <text x="{c['BALANCE_LABEL_X']}" y="{c['BALANCE_LABEL_BASELINE']}" fill="{color['COLOR_MUTED']}" font-family="-apple-system,BlinkMacSystemFont,'PingFang SC',sans-serif" font-size="22">剩余：</text>
  <text x="{c['BALANCE_VALUE_X']}" y="{c['BALANCE_VALUE_Y'] + 25}" fill="{color['COLOR_GREEN']}" font-family="Menlo,Consolas,monospace" font-size="24" font-weight="700">{html.escape(balance)}</text>
  <text x="{unit_x}" y="{c['CURRENCY_Y'] + 23}" fill="{color['COLOR_MUTED']}" font-family="Menlo,Consolas,monospace" font-size="24">{html.escape(currency)}</text>
</svg>
"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--payload", help="JSON status payload. Defaults to the visual QA sample.")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    payload = {
        "ageText": "9 分钟前",
        "balanceText": "25.31",
        "currency": "USD",
        "status": "using",
    }
    if args.payload:
        payload.update(json.loads(args.payload))

    args.out.write_text(render(payload), encoding="utf-8")
    print(args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
