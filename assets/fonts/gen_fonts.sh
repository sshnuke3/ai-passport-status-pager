#!/usr/bin/env bash
# Generate the compact Chinese+ASCII subset fonts for the status pager app.
#
# Source font: Noto Sans CJK SC (OFL-1.1), see assets/README.md.
# Converter:   lv_font_conv 1.5.3 (pinned), https://github.com/lvgl/lv_font_conv
#
# The character inventory below is the exact set of glyphs displayed at
# runtime by main/status_ui.c (fixed labels, event names, duration units).
# Regenerate both sizes whenever that string set changes, and keep the
# inventory and this command under version control.
#
# Usage: from the repository root:
#   assets/fonts/gen_fonts.sh   # assumes lv_font_conv is on PATH

set -euo pipefail

FONT_SRC="${FONT_SRC:-$(dirname "$0")/NotoSansCJKsc-Regular.otf}"
OUT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Printable ASCII (0x20-0x7E) plus the exact Chinese glyphs used by the UI,
# plus U+00B7 MIDDLE DOT used in the key hint line.
SYMBOLS='专注中离开可打扰当前已周期上下切换确认长按查看统计暂无记录返回次数段最近事件分秒时卡·'

gen() {
    local size="$1"
    local name="app_font_${size}"
    lv_font_conv \
        --font "$FONT_SRC" \
        --range 0x20-0x7E \
        --symbols "$SYMBOLS" \
        --size "$size" --bpp 4 \
        --format lvgl --no-compress \
        --lv-font-name "$name" --lv-include lvgl.h \
        --output "$OUT_DIR/$name.c"
    echo "generated $OUT_DIR/$name.c"
}

command -v lv_font_conv >/dev/null 2>&1 || { echo "lv_font_conv not found; install pinned lv_font_conv 1.5.3" >&2; exit 1; }
[ -f "$FONT_SRC" ] || { echo "source font not found: $FONT_SRC (set FONT_SRC=/path/to/NotoSansCJKsc-Regular.otf)" >&2; exit 1; }

gen 32
gen 16