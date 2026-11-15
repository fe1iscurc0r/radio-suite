#!/usr/bin/env bash
# N1 定因 A/B 的 ICMP 段。只发 input keyevent KEYCODE_WAKEUP，不改任何设备设置。
# 用法：bash ping-ab.sh [轮数]，每轮先唤醒再 ping 10 个包（约 10 秒）。
export MSYS_NO_PATHCONV=1
ADB=${ADB:-/d/Android/sdk/platform-tools/adb.exe}
IP=${IP:-192.168.137.177}
ROUNDS=${1:-6}
OUT=${OUT:-./ping-ab.log}
: > "$OUT"
echo "start=$(date +%H:%M:%S)" >> "$OUT"
for round in $(seq 1 "$ROUNDS"); do
  "$ADB" shell input keyevent KEYCODE_WAKEUP >/dev/null 2>&1
  W=$("$ADB" shell dumpsys power 2>/dev/null | grep -m1 'mWakefulness=' | tr -d '\r' | sed 's/^ *//')
  echo "round=$round wake_at=$(date +%H:%M:%S) $W" >> "$OUT"
  ping -n 10 "$IP" 2>&1 | while IFS= read -r l; do
    printf '%s %s\n' "$(date +%H:%M:%S)" "$(printf '%s' "$l" | iconv -f GBK -t UTF-8 -c)" >> "$OUT"
  done
done
echo "end=$(date +%H:%M:%S)" >> "$OUT"
echo "reply_lines=$(grep -c 'TTL=' "$OUT")" >> "$OUT"
echo "timeout_lines=$(grep -c '超时' "$OUT")" >> "$OUT"
