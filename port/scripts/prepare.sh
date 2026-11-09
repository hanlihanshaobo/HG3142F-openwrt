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
KERNEL_SERIES=$(python3 -c "import json;print(json.load(open('$LOCK'))['openwrt']['kernel'])")
KERNEL_EXPECTED=$(python3 -c "import json;print(json.load(open('$LOCK'))['openwrt'].get('kernel_version',''))")

echo "==> OpenWrt: $SRC_URL @ $SRC_REF"
echo "==> 目标目录: $DEST"

if [ -d "$DEST/.git" ]; then
	echo "==> 复用已有源码树"
	git -C "$DEST" fetch --depth 1 origin "$SRC_REF" || git -C "$DEST" fetch origin
	git -C "$DEST" checkout -q --detach FETCH_HEAD 2>/dev/null || git -C "$DEST" checkout -q --detach "$SRC_REF"
else
	rm -rf "$DEST"
	mkdir -p "$DEST"
	git -C "$DEST" init -q
	git -C "$DEST" remote add origin "$SRC_URL"
	# 按 commit sha 浅拉，比全量 clone 快一个数量级
	git -C "$DEST" fetch --depth 1 origin "$SRC_REF"
	git -C "$DEST" checkout -q -b "port/mb4784w" FETCH_HEAD
fi

# 内核基线自检：锁文件里的 kernel_version 必须与 pin 到的上游源码一致。
# 这样以后升级内核时，忘记同步 sources.lock.json 会在这里直接失败，而不是在 CI 深处报错。
KERNEL_FILE="$DEST/target/linux/generic/kernel-$KERNEL_SERIES"
if [ ! -f "$KERNEL_FILE" ]; then
	if [ -n "${OPENWRT_REF_OVERRIDE:-}" ]; then
		# workflow_dispatch 的实验通道：允许覆盖到别的 kernel series，只提示不拦截
		echo "警告: 覆盖的 OpenWrt commit 没有 $KERNEL_FILE，跳过内核基线检查" >&2
	else
		echo "错误: 上游源码没有 $KERNEL_FILE（kernel series=$KERNEL_SERIES）" >&2
		exit 1
	fi
else
	KERNEL_ACTUAL=$(sed -n "s/^LINUX_VERSION-$KERNEL_SERIES *= *\.\([0-9][0-9]*\).*/\1/p" "$KERNEL_FILE")
	echo "==> 内核基线: Linux $KERNEL_SERIES.${KERNEL_ACTUAL:-?}"
	if [ -n "$KERNEL_EXPECTED" ]; then
		KERNEL_EXPECTED_PATCH=${KERNEL_EXPECTED##*.}
		if [ "$KERNEL_ACTUAL" != "$KERNEL_EXPECTED_PATCH" ]; then
			if [ -n "${OPENWRT_REF_OVERRIDE:-}" ]; then
				echo "警告: 覆盖的 OpenWrt commit 内核是 $KERNEL_SERIES.${KERNEL_ACTUAL:-?}，锁文件期望 $KERNEL_EXPECTED" >&2
			else
				echo "错误: sources.lock.json 要求 $KERNEL_EXPECTED，但上游 pin 是 $KERNEL_SERIES.${KERNEL_ACTUAL:-?}" >&2
				exit 1
			fi
		fi
	fi
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

# BCM96855 SDK 源码树（enet / PCIe 等 GPL 驱动）：拷进 OpenWrt 源码树，
# 供 target/linux/bcmbca/packages/bcm-pcie 这类 target 包以相对路径引用。
if [ -d "$REPO_ROOT/port/sdk-bcm96855" ]; then
	echo "==> 复制 SDK 源码树 -> target/linux/bcmbca/sdk-bcm96855"
	rm -rf "$DEST/target/linux/bcmbca/sdk-bcm96855"
	mkdir -p "$DEST/target/linux/bcmbca/sdk-bcm96855"
	( cd "$REPO_ROOT/port/sdk-bcm96855" && tar cf - sdk compat ) \
		| ( cd "$DEST/target/linux/bcmbca/sdk-bcm96855" && tar xf - )
fi

echo "==> 完成"
git -C "$DEST" show -s --format='    %H %cI %s'
if ls "$DEST/target/linux" | grep -qx bcmbca; then
	echo "    target/linux/bcmbca 已就位"
else
	echo "    警告: target/linux/bcmbca 未就位（overlay 未叠加成功？）" >&2
	exit 1
fi
