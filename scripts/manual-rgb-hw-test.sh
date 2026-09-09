#!/usr/bin/env bash
# PerfectSense — interactive hardware / RGB smoke tour
#
# Each step waits for any keypress before advancing (no timeout).
# Restores keyboard + logo (+ optional thermal) at the end.
#
# Firmware notes (PHN16-72 / Predator Sense EC):
#   mode 0 static   — LEDs via per-zone regs (PerfectSense paints zones on effect set)
#   mode 1 breathing — uses RGB; EC forces speed=0
#   mode 2 neon     — RGB ignored; rainbow cycle
#   mode 3 wave     — RGB ignored; rainbow wave; dir 0–2 matters
#   mode 4 shifting — RGB + dir matter
#   mode 5–7        — RGB matter; dir forced 0
#
# Usage:
#   sudo ./scripts/manual-rgb-hw-test.sh
#   SKIP_EXTRAS=1 ./scripts/manual-rgb-hw-test.sh # RGB/logo only
#
# Requires: /usr/bin/perfectsense, module loaded, root (or passwordless sudo).

set -euo pipefail

PS="${PERFECTSENSE_BIN:-/usr/bin/perfectsense}"
SKIP_EXTRAS="${SKIP_EXTRAS:-0}"
RESTORE_THERMAL="${RESTORE_THERMAL:-1}"

declare -a EFFECT_NAMES=(
	[0]=static
	[1]=breathing
	[2]=neon
	[3]=wave
	[4]=shifting
	[5]=zoom
	[6]=meteor
	[7]=twinkling
)

RED=$'\033[31m'
GRN=$'\033[32m'
YLW=$'\033[33m'
CYN=$'\033[36m'
BLD=$'\033[1m'
RST=$'\033[0m'

die() { echo "${RED}error:${RST} $*" >&2; exit 1; }

need_root() {
	if [[ "$(id -u)" -eq 0 ]]; then
		return 0
	fi
	if sudo -n true 2>/dev/null; then
		return 0
	fi
	echo "This script needs root to write sysfs. Re-run with sudo."
	exec sudo -E env SKIP_EXTRAS="$SKIP_EXTRAS" \
		RESTORE_THERMAL="$RESTORE_THERMAL" PERFECTSENSE_BIN="$PS" "$0" "$@"
}

ps_cmd() {
	if [[ "$(id -u)" -eq 0 ]]; then
		"$PS" "$@"
	else
		sudo -n "$PS" "$@"
	fi
}

banner() {
	echo
	echo "${BLD}${CYN}════════════════════════════════════════════════════════════${RST}"
	echo "${BLD}  $*${RST}"
	echo "${CYN}════════════════════════════════════════════════════════════${RST}"
}

step() {
	local title="$1"
	shift
	echo
	echo "${YLW}▸ ${BLD}${title}${RST}"
	if [[ $# -gt 0 ]]; then
		echo "  ${*}"
	fi
}

expect() {
	echo "  ${GRN}esperado:${RST} $*"
}

note() {
	echo "  ${CYN}nota:${RST} $*"
}

show_live() {
	local rgb effect logo
	rgb="$(ps_cmd rgb get 2>/dev/null || echo '?')"
	effect="$(ps_cmd rgb effect get 2>/dev/null || echo '?')"
	logo="$(ps_cmd logo get 2>/dev/null || echo '?')"
	echo "  ${CYN}agora:${RST} rgb=${rgb}"
	echo "           effect=${effect}"
	echo "           logo=${logo}"
}

# Wait for any key before advancing. 'q' aborts tour early.
pause_or_key() {
	local hint="${1:-Olha o teclado / logo e confere.}"
	local key=""
	echo
	echo "  ${hint}"
	echo -n "  [qualquer tecla → próximo | q = sair] "
	read -r -n 1 -s key
	echo
	if [[ "$key" == "q" || "$key" == "Q" ]]; then
		echo "${YLW}Saída pedida — a restaurar estado…${RST}"
		restore_all
		exit 0
	fi
}

run_checked() {
	local desc="$1"
	shift
	echo "  run: $*"
	if ! "$@"; then
		echo "${RED}  FALHOU: ${desc}${RST}" >&2
		return 1
	fi
}

hex_to_rgb() {
	local chex="$1"
	R_OUT=$((16#${chex:0:2}))
	G_OUT=$((16#${chex:2:2}))
	B_OUT=$((16#${chex:4:2}))
}

# ─── saved state ───────────────────────────────────────────────────────────
SAVED_RGB=""
SAVED_EFFECT=""
SAVED_LOGO=""
SAVED_MODE=""
RESTORED=0

save_state() {
	SAVED_RGB="$(ps_cmd rgb get)"
	SAVED_EFFECT="$(ps_cmd rgb effect get)"
	SAVED_LOGO="$(ps_cmd logo get)"
	SAVED_MODE="$(ps_cmd mode get 2>/dev/null || true)"
	echo "Estado inicial guardado:"
	echo "  rgb=$SAVED_RGB"
	echo "  effect=$SAVED_EFFECT"
	echo "  logo=$SAVED_LOGO"
	echo "  mode=${SAVED_MODE:-?}"
}

restore_all() {
	[[ "$RESTORED" -eq 1 ]] && return 0
	# Nothing saved yet (early failure / help path)
	if [[ -z "${SAVED_RGB}${SAVED_EFFECT}${SAVED_LOGO}${SAVED_MODE}" ]]; then
		RESTORED=1
		return 0
	fi
	RESTORED=1
	banner "RESTORE"
	local mode=0 r=0 g=0 b=0
	local apply_effect=0

	# Effect first only when it is a real animation / uniform static colour.
	# Mode 0 with RGB 0,0,0 means "per-zone static" — reapplying it after
	# rgb set would be a no-op now, but we still prefer rgb set last so
	# four distinct zone colours win.
	if [[ -n "$SAVED_EFFECT" ]]; then
		IFS=',' read -r mode _ _ _ r g b <<<"$SAVED_EFFECT"
		if [[ "${mode:-0}" != "0" ]] || [[ "${r:-0}" != "0" || "${g:-0}" != "0" || "${b:-0}" != "0" ]]; then
			apply_effect=1
			ps_cmd rgb effect set "$SAVED_EFFECT" || true
		fi
	fi
	if [[ -n "$SAVED_RGB" ]]; then
		IFS=',' read -r z1 z2 z3 z4 bri <<<"$SAVED_RGB"
		if [[ -n "${bri:-}" ]]; then
			ps_cmd rgb set "$z1" "$z2" "$z3" "$z4" "$bri" || true
		else
			ps_cmd rgb set "$z1" "$z2" "$z3" "$z4" || true
		fi
	elif [[ "$apply_effect" -eq 0 && -n "$SAVED_EFFECT" ]]; then
		# No zone snapshot; fall back to effect (brightness-only static).
		ps_cmd rgb effect set "$SAVED_EFFECT" || true
	fi
	if [[ -n "$SAVED_LOGO" ]]; then
		IFS=',' read -r lc lb le <<<"$SAVED_LOGO"
		ps_cmd logo set "$lc" "${lb:-100}" "${le:-0}" || true
	fi
	if [[ "$RESTORE_THERMAL" == "1" && -n "$SAVED_MODE" ]]; then
		ps_cmd mode set "$SAVED_MODE" || true
	fi
	show_live
	echo "${GRN}Restore feito.${RST}"
}

trap 'echo; echo "Interrupt — restore…"; restore_all; exit 130' INT TERM
trap 'restore_all' EXIT

effect_set() {
	local mode="$1" speed="$2" bri="$3" dir="$4" r="$5" g="$6" b="$7"
	run_checked "effect" ps_cmd rgb effect set \
		"${mode},${speed},${bri},${dir},${r},${g},${b}"
	show_live
}

# ─── sections ──────────────────────────────────────────────────────────────

section_per_zone_palettes() {
	banner "1) Cores estáticas por zona (per_zone_mode)"
	note "Isto é o caminho certo para 4 cores diferentes ao mesmo tempo."
	local -a palettes=(
		"tudo vermelho|ff0000 ff0000 ff0000 ff0000 100"
		"tudo verde|00ff00 00ff00 00ff00 00ff00 100"
		"tudo azul|0000ff 0000ff 0000ff 0000ff 100"
		"tudo branco|ffffff ffffff ffffff ffffff 80"
		"arco-íris zonas|ff0000 ffff00 00ff00 0000ff 100"
		"gradiente quente|ff0000 ff4400 ff8800 ffcc00 100"
		"gradiente frio|00ffff 0088ff 0000ff 8800ff 100"
		"só zona1 vermelha|ff0000 000000 000000 000000 100"
		"só zona4 azul|000000 000000 000000 0000ff 100"
		"brilho baixo|ff0000 ff0000 ff0000 ff0000 20"
		"brilho médio|00ff00 00ff00 00ff00 00ff00 50"
		"apagado (off)|off"
	)

	local entry name args
	for entry in "${palettes[@]}"; do
		name="${entry%%|*}"
		args="${entry#*|}"
		step "Per-zone: $name" "rgb set $args"
		expect "4 zonas com as cores/brilho descritos (ou teclado apagado se off)."
		if [[ "$args" == "off" ]]; then
			run_checked "rgb off" ps_cmd rgb off
		else
			# shellcheck disable=SC2086
			run_checked "rgb set" ps_cmd rgb set $args
		fi
		show_live
		pause_or_key
	done
}

section_effects() {
	banner "2) Efeitos four_zone_mode (mode 0–7)"
	echo "Formato: mode,speed,bri,dir,R,G,B"
	echo "Nomes: ${EFFECT_NAMES[*]}"
	note "Neon/wave IGNORAM RGB (arco-íris). Breathing ignora speed (EC força 0)."
	note "Static (0) via effect = cor uniforme nas 4 zonas (desde PerfectSense ≥ fix)."

	local -a color_list=(
		"ff0000|vermelho"
		"00ff00|verde"
		"0000ff|azul"
		"ff00ff|magenta"
		"00ffff|ciano"
		"ffff00|amarelo"
		"ffffff|branco"
	)
	local chex cname r g b dir

	# ── 0 static: cores (agora deve acender) ──
	banner "2a) mode 0 static — cor uniforme"
	for color_csv in "${color_list[@]}"; do
		chex="${color_csv%%|*}"
		cname="${color_csv#*|}"
		hex_to_rgb "$chex"
		r=$R_OUT; g=$G_OUT; b=$B_OUT
		step "Static · ${cname}" "rgb effect set 0,0,90,0,${r},${g},${b}"
		expect "Teclado inteiro ${cname} (4 zonas iguais). rgb get deve refletir a cor."
		effect_set 0 0 90 0 "$r" "$g" "$b"
		pause_or_key
	done

	# ── 1 breathing: cores (speed sempre 0 no EC) ──
	banner "2b) mode 1 breathing — pulsa a cor"
	for color_csv in "${color_list[@]}"; do
		chex="${color_csv%%|*}"
		cname="${color_csv#*|}"
		hex_to_rgb "$chex"
		r=$R_OUT; g=$G_OUT; b=$B_OUT
		step "Breathing · ${cname}" "rgb effect set 1,5,90,0,${r},${g},${b}"
		expect "Pulsa em ${cname}. effect get: speed=0 (EC ignora speed no breathing)."
		effect_set 1 5 90 0 "$r" "$g" "$b"
		pause_or_key
	done

	# ── 2 neon: RGB ignorado — 1 passo + speeds ──
	banner "2c) mode 2 neon — arco-íris (RGB ignorado)"
	step "Neon" "rgb effect set 2,5,90,0,255,0,0"
	expect "Ciclo gradual por todas as cores. RGB no comando é forçado a 0,0,0 no get."
	note "Repetir neon com outras cores NÃO muda nada — por isso só 1 cor aqui."
	effect_set 2 5 90 0 255 0 0
	pause_or_key

	# ── 3 wave: RGB ignorado — dirs ──
	banner "2d) mode 3 wave — onda arco-íris + direção"
	for dir in 0 1 2; do
		step "Wave · dir=${dir}" "rgb effect set 3,5,90,${dir},0,0,0"
		expect "Onda atravessa zonas; só a direção muda o sentido/padrão. RGB=0 no get."
		effect_set 3 5 90 "$dir" 0 0 0
		pause_or_key
	done

	# ── 4 shifting: cores + dirs ──
	banner "2e) mode 4 shifting — cor + direção"
	local -a shift_colors=("ff0000|vermelho" "0000ff|azul" "ffffff|branco")
	for color_csv in "${shift_colors[@]}"; do
		chex="${color_csv%%|*}"
		cname="${color_csv#*|}"
		hex_to_rgb "$chex"
		r=$R_OUT; g=$G_OUT; b=$B_OUT
		for dir in 0 1 2; do
			step "Shifting · ${cname} · dir=${dir}" \
				"rgb effect set 4,5,90,${dir},${r},${g},${b}"
			expect "Deslocamento baseado em ${cname}; dir=${dir} muda o movimento."
			effect_set 4 5 90 "$dir" "$r" "$g" "$b"
			pause_or_key
		done
	done

	# ── 5–7: cores, sem dir ──
	local mode name
	local -a mode_banners=([5]="2f) mode 5 zoom" [6]="2g) mode 6 meteor" [7]="2h) mode 7 twinkling")
	for mode in 5 6 7; do
		name="${EFFECT_NAMES[$mode]}"
		banner "${mode_banners[$mode]}"
		for color_csv in "${color_list[@]}"; do
			chex="${color_csv%%|*}"
			cname="${color_csv#*|}"
			hex_to_rgb "$chex"
			r=$R_OUT; g=$G_OUT; b=$B_OUT
			step "${name} · ${cname}" \
				"rgb effect set ${mode},5,90,0,${r},${g},${b}"
			case "$mode" in
			5) expect "Zoom em ${cname}." ;;
			6) expect "Meteor em ${cname}." ;;
			7) expect "Twinkling/cintilação em ${cname}." ;;
			esac
			effect_set "$mode" 5 90 0 "$r" "$g" "$b"
			pause_or_key
		done
	done
}

section_effect_speed_bri() {
	banner "3) Speed / brightness (onde o EC realmente usa)"
	note "Breathing força speed=0 — testamos speed em WAVE (mode 3)."
	local s br

	for s in 1 5 9; do
		step "Wave speed=${s}" "rgb effect set 3,${s},90,1,0,0,0"
		expect "Onda mais rápida com speed maior (1→9)."
		effect_set 3 "$s" 90 1 0 0 0
		pause_or_key
	done

	for br in 20 60 100; do
		step "Wave brightness=${br}"
		expect "Mesma onda, intensidade diferente."
		effect_set 3 5 "$br" 1 0 0 0
		pause_or_key
	done

	step "Breathing brightness (speed ignorado)" "1,*,bri,0,255,0,0"
	expect "Pulsa vermelho; effect get mostra speed=0 mesmo pedindo 9."
	effect_set 1 9 50 0 255 0 0
	pause_or_key
}

section_logo() {
	banner "4) Logo traseiro (back_logo)"
	local -a logos=(
		"vermelho on|ff0000 100 1"
		"verde on|00ff00 100 1"
		"azul on|0000ff 80 1"
		"branco dim|ffffff 30 1"
		"apagado|000000 0 0"
		"off CLI|off"
	)
	local entry name args
	for entry in "${logos[@]}"; do
		name="${entry%%|*}"
		args="${entry#*|}"
		step "Logo: $name" "logo set/off $args"
		expect "Luz do Predator no tampo muda / apaga."
		if [[ "$args" == "off" ]]; then
			run_checked "logo off" ps_cmd logo off
		else
			# shellcheck disable=SC2086
			run_checked "logo set" ps_cmd logo set $args
		fi
		show_live
		pause_or_key
	done
}

section_lights_reset() {
	banner "5) lights reset"
	step "Reset controlador" "depois volta a um estático conhecido"
	expect "Pode piscar/reiniciar o KB; a seguir forçamos vermelho estático."
	run_checked "reset" ps_cmd lights reset
	pause_or_key "Reset enviado — observa se o teclado reacende/reage."
	run_checked "rgb set" ps_cmd rgb set ff0000 ff0000 ff0000 ff0000 80
	show_live
	pause_or_key
}

section_extras() {
	[[ "$SKIP_EXTRAS" == "1" ]] && return 0
	banner "6) Extras hardware (não RGB)"

	local lcd0 kb0 mode0
	lcd0="$(ps_cmd lcd get 2>/dev/null || echo 0)"
	kb0="$(ps_cmd kb-timeout get 2>/dev/null || echo 0)"
	mode0="$(ps_cmd mode get)"

	step "LCD override ON" "reduz latency/ghosting (OEM)"
	expect "lcd get → 1 (mudança visual nem sempre óbvia)."
	run_checked "lcd 1" ps_cmd lcd set 1
	echo "  lcd=$(ps_cmd lcd get)"
	pause_or_key

	step "LCD override OFF"
	run_checked "lcd 0" ps_cmd lcd set 0
	echo "  lcd=$(ps_cmd lcd get)"
	pause_or_key

	step "KB backlight timeout ON" "teclado RGB apaga ~30s sem input"
	expect "kb-timeout get → 1."
	run_checked "kb 1" ps_cmd kb-timeout set 1
	echo "  kb-timeout=$(ps_cmd kb-timeout get)"
	pause_or_key

	step "KB backlight timeout OFF"
	run_checked "kb 0" ps_cmd kb-timeout set 0
	echo "  kb-timeout=$(ps_cmd kb-timeout get)"
	pause_or_key

	step "Thermal: quiet → balanced → performance"
	expect "Pass: thermal_profile muda + nvidia_powerd synced yes."
	note "quiet→powerd off; balanced/performance→powerd on."
	note "Sem carga CPU/GPU, o barulho dos fans pode parecer igual — isso é normal."
	note "Critério objetivo = status (perfil + powerd), não 'sentir' o turbo."
	for m in quiet balanced performance; do
		echo "  → mode set $m"
		ps_cmd mode set "$m" || true
		ps_cmd status 2>/dev/null | grep -E 'thermal|nvidia_powerd' || true
		case "$m" in
		quiet)
			expect "thermal=quiet, powerd off, synced yes"
			;;
		balanced|performance)
			expect "thermal=$m, powerd on, synced yes"
			;;
		esac
		pause_or_key "Perfil $m — confere as linhas thermal/powerd acima."
	done
	if [[ "$RESTORE_THERMAL" == "1" ]]; then
		ps_cmd mode set "$mode0" || true
	fi

	ps_cmd lcd set "$lcd0" || true
	ps_cmd kb-timeout set "$kb0" || true

	step "Fan duty snapshot" "só leitura"
	expect "fan get mostra duty/rpm."
	ps_cmd fan get || true
	pause_or_key

	step "Status completo (head)"
	ps_cmd status 2>/dev/null | head -25 || true
	pause_or_key
}

section_combo() {
	banner "7) Combo final"
	step "Wave + logo azul"
	expect "Onda arco-íris no teclado + logo azul (RGB do wave é ignorado — ok)."
	effect_set 3 6 100 1 0 255 255
	run_checked "logo" ps_cmd logo set 0000ff 100 1
	show_live
	pause_or_key

	step "4 zonas distintas (per_zone)"
	run_checked "rgb" ps_cmd rgb set aa0000 00aa00 0000aa aaaaaa 100
	show_live
	pause_or_key
}

# ─── main ──────────────────────────────────────────────────────────────────

main() {
	need_root "$@"
	[[ -x "$PS" ]] || die "CLI não encontrado: $PS"
	"$PS" status >/dev/null 2>&1 || die "perfectsense sysfs indisponível (módulo carregado?)"

	banner "PerfectSense interactive tour"
	echo "CLI: $PS"
	echo "Avança só com tecla | q = sair e restaurar"
	echo "SKIP_EXTRAS=$SKIP_EXTRAS  RESTORE_THERMAL=$RESTORE_THERMAL"
	echo
	ps_cmd status 2>/dev/null | head -20 || true
	pause_or_key "Pronto? Começamos."

	save_state

	section_per_zone_palettes
	section_effects
	section_effect_speed_bri
	section_logo
	section_lights_reset
	section_extras
	section_combo

	restore_all
	banner "FIM"
	echo "Se algo ficou estranho: sudo $PS lights reset && sudo $PS rgb set ff0000 ff0000 ff0000 ff0000 80"
}

main "$@"
