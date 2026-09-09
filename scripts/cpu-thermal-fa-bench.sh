#!/usr/bin/env bash
# PerfectSense — CPU load sweep across thermal modes (± FanAggressor)
#
# For each thermal mode: stress CPU 30s, sample every 2.5s:
#   temp_cpu, fan_rpm_cpu, fan_duty_cpu, FA status fields
#
# Usage:
#   sudo ./scripts/cpu-thermal-fa-bench.sh
#   OUT=/tmp/ps-bench ./scripts/cpu-thermal-fa-bench.sh
#
set -euo pipefail

PS="${PERFECTSENSE_BIN:-/usr/bin/perfectsense}"
OUT_DIR="${OUT:-/tmp/perfectsense-cpu-bench-$$}"
DUR_SEC="${DUR_SEC:-30}"
INTERVAL="${INTERVAL:-2.5}"
MODES=(low-power quiet balanced balanced-performance performance)
CORES="$(nproc)"

die() { echo "error: $*" >&2; exit 1; }

need_root() {
	[[ "$(id -u)" -eq 0 ]] || exec sudo -E OUT="$OUT_DIR" DUR_SEC="$DUR_SEC" \
		INTERVAL="$INTERVAL" PERFECTSENSE_BIN="$PS" "$0" "$@"
}

ps_cmd() { "$PS" "$@"; }

sample_line() {
	local ts mode phase fa_on
	ts="$1"; mode="$2"; phase="$3"; fa_on="$4"
	local st cpu_t cpu_rpm duty_c duty_g tgt_c fa_en fa_extra
	st="$(ps_cmd status 2>/dev/null || true)"
	# status: temps_c: 81,58   fan_rpm: 3703,3333   fan_duty: 45,40   fan_target: 0,0
	cpu_t="$(echo "$st" | awk -F': ' '/^temps_c:/{print $2}' | cut -d, -f1)"
	cpu_rpm="$(echo "$st" | awk -F': ' '/^fan_rpm:/{print $2}' | cut -d, -f1)"
	duty_c="$(echo "$st" | awk -F': ' '/^fan_duty:/{print $2}' | cut -d, -f1)"
	duty_g="$(echo "$st" | awk -F': ' '/^fan_duty:/{print $2}' | cut -d, -f2 | tr -d ' ')"
	tgt_c="$(echo "$st" | awk -F': ' '/^fan_target:/{print $2}' | cut -d, -f1)"
	fa_en="$(ps_cmd fan aggressor status 2>/dev/null | awk '/fan aggressor:/{print $3}')"
	fa_extra="tgt=${tgt_c:--}"
	if [[ -r /run/perfectsense/fan-aggressor.status ]]; then
		fa_extra="$(tr '\n' ' ' </run/perfectsense/fan-aggressor.status | tr -s ' ')"
	fi
	printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
		"$ts" "$mode" "$phase" "$fa_on" \
		"${cpu_t:--1}" "${cpu_rpm:--1}" "${duty_c:--1}" "${duty_g:--1}" \
		"${fa_en:-off}" "${fa_extra}"
}

run_phase() {
	local phase="$1" fa_on="$2"
	local mode csv stress_pid t0 elapsed samples
	csv="$OUT_DIR/${phase}.csv"
	echo "ts,mode,phase,fa,temp_cpu,rpm_cpu,duty_cpu,duty_gpu,fa_en,fa_extra" >"$csv"

	echo
	echo "======== PHASE: $phase (FA=$fa_on) ========"

	if [[ "$fa_on" == "on" ]]; then
		ps_cmd fan aggressor on
		# Ensure FA can engage during 30s CPU load (user engage was 95)
		ps_cmd fan aggressor set engage_c=78 hold_sec=1 interval_ms=1000
		ps_cmd fan aggressor reload
	else
		ps_cmd fan aggressor off || true
		ps_cmd fan auto || true
	fi
	ps_cmd fan aggressor status || true

	for mode in "${MODES[@]}"; do
		echo
		echo "---- mode=$mode  ${DUR_SEC}s CPU stress (${CORES} workers) ----"
		ps_cmd mode set "$mode" || echo "WARN: mode set $mode failed"
		# short settle after mode change
		sleep 1
		ps_cmd fan auto >/dev/null 2>&1 || true

		# CPU-only stress (avoid GPU burn)
		stress-ng --cpu "$CORES" --cpu-method fft --timeout "${DUR_SEC}s" \
			--metrics-brief >/dev/null 2>&1 &
		stress_pid=$!

		t0="$(date +%s.%N)"
		samples=0
		while kill -0 "$stress_pid" 2>/dev/null; do
			elapsed="$(awk -v t0="$t0" 'BEGIN{srand(); print systime()+0}' )"
			# portable elapsed via date
			elapsed="$(python3 -c "import time; print(f'{time.time()-float(\"$t0\"):.1f}')")"
			sample_line "$elapsed" "$mode" "$phase" "$fa_on" | tee -a "$csv"
			samples=$((samples + 1))
			sleep "$INTERVAL"
		done
		wait "$stress_pid" || true
		# final sample
		sample_line "$(python3 -c "import time; print(f'{time.time()-float(\"$t0\"):.1f}')")" \
			"$mode" "$phase" "$fa_on" | tee -a "$csv"
		echo "(samples≈$samples for $mode)"
		# cool a bit between modes so next curve starts cleaner
		sleep 3
	done
}

summarize() {
	python3 - "$OUT_DIR" <<'PY'
import csv, sys, statistics as stats
from pathlib import Path
root = Path(sys.argv[1])
print("\n========== SUMMARY (samples with ts>=10s) ==========")
for csv_path in sorted(root.glob("*.csv")):
    rows = list(csv.DictReader(csv_path.open()))
    if not rows:
        continue
    print(f"\n## {csv_path.name}")
    by = {}
    for r in rows:
        try:
            float(r["ts"])
        except Exception:
            continue
        by.setdefault(r["mode"], []).append(r)
    for mode, rs in by.items():
        steady = [r for r in rs if float(r["ts"]) >= 10]
        use = steady or rs
        def nums(key):
            out = []
            for r in use:
                try:
                    v = float(r[key])
                    if v >= 0:
                        out.append(v)
                except Exception:
                    pass
            return out
        temps, rpms, dutys = nums("temp_cpu"), nums("rpm_cpu"), nums("duty_cpu")
        def fmt(xs):
            if not xs:
                return "n/a"
            return f"mean={stats.mean(xs):.1f} max={max(xs):.0f}"
        print(f"  {mode:24s}  temp[{fmt(temps)}]  rpm[{fmt(rpms)}]  duty[{fmt(dutys)}]")
PY
}

main() {
	need_root "$@"
	command -v stress-ng >/dev/null || die "stress-ng required"
	command -v "$PS" >/dev/null || die "CLI missing: $PS"
	mkdir -p "$OUT_DIR"
	echo "OUT_DIR=$OUT_DIR"
	echo "Saving FA/mode baseline…"
	BASE_MODE="$(ps_cmd mode get)"
	ps_cmd fan aggressor status | tee "$OUT_DIR/fa-before.txt" || true

	# Capture whether fan_duty tracks load in auto (FA off diagnostic)
	run_phase "fa_off" "off"
	run_phase "fa_on" "on"

	echo
	echo "Restoring balanced + FA off…"
	ps_cmd fan aggressor off || true
	ps_cmd fan auto || true
	ps_cmd mode set balanced || ps_cmd mode set "$BASE_MODE" || true

	summarize
	echo
	echo "Raw CSVs in $OUT_DIR"
	echo "Done."
}

main "$@"
