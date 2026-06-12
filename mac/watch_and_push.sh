#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INTERVAL="${CC_SWITCH_BLE_INTERVAL:-120}"
LAST_PAYLOAD=""

log() {
  printf '[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

while true; do
  payload="$(python3 "$SCRIPT_DIR/cc_switch_provider_status.py")"
  provider_name="$(python3 -c 'import json,sys; print(json.load(sys.stdin).get("providerName",""))' <<< "$payload")"
  balance_text="$(python3 -c 'import json,sys; print(json.load(sys.stdin).get("balanceText",""))' <<< "$payload")"
  currency="$(python3 -c 'import json,sys; print(json.load(sys.stdin).get("currency",""))' <<< "$payload")"

  if [ "$payload" != "$LAST_PAYLOAD" ]; then
    log "pushing ${provider_name:-unknown} balance=${balance_text:-unknown} ${currency:-}"
    if CC_SWITCH_STATUS_PAYLOAD="$payload" "$SCRIPT_DIR/push_status_once.sh"; then
      LAST_PAYLOAD="$payload"
      log "push succeeded"
    else
      log "push failed; will retry next cycle"
    fi
  fi

  sleep "$INTERVAL"
done
