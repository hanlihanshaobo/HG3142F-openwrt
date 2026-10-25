#!/bin/sh
# 拉取 OpenWrt 源码（按 port/sources.lock.json 锁定提交）并叠加本仓库的 bcmbca target。
# 用法: sh port/scripts/prepare.sh [目标目录，默认 sources/openwrt]
#
# 结构参考 PonWRT-TYFZA1 / pbs05/ponwrt 的做法：上游源码 pin + port/overlay 叠层。
set -eu

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
DEST="${1:-$REPO_ROOT/sources/openwrt}"
LOCK="$REPO_ROOT/port/sources.lock.json"

SRC_URL=$(python3 -c "import json;print(json.load(open('$LOCK'))['openwrt']['url'])")
SRC_REF=${OPENWRT_REF_OVERRIDE:-$(python3 -c "import json;print(json.load(open('$LOCK'))['openwrt']['commit'])")}

echo "==> OpenWrt: $SRC_URL @ $SRC_REF"
echo "==> 目标目录: $DEST"

if [ -d "$DEST/.git" ]; then
	echo "==> 复用已有源码树"
	git -C "$DEST" fetch --depth 1 origin "$SRC_REF" || git -C "$DEST" fetch origin
	git -C "$DEST" checkout -q --detach "$SRC_REF"
else
	rm -rf "$DEST"
	mkdir -p "$(dirname "$DEST")"
	git clone --filter=blob:none "$SRC_URL" "$DEST"
	git -C "$DEST" checkout -q --detach "$SRC_REF"
fi

echo "==> 叠加 overlay（target/linux/bcmbca、base-files 等）"
if [ -d "$REPO_ROOT/port/overlay" ]; then
	( cd "$REPO_ROOT/port/overlay" && tar cf - . ) | ( cd "$DEST" && tar xf - )
fi

echo "==> 应用源码补丁"
if [ -d "$REPO_ROOT/port/patches" ]; then
	for p in "$REPO_ROOT"/port/patches/*.patch; do
		[ -e "$p" ] || continue
		echo "    patch: $(basename "$p")"
		git -C "$DEST" apply --3way "$p"
	done
fi

echo "==> 完成"
git -C "$DEST" show -s --format='    %H %cI %s'
ls "$DEST/target/linux" | grep -x bcmbca >/dev/null && echo "    target/linux/bcmbca 已就位"
