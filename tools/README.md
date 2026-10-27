# 真机测试工具（HG3142F / BCM96855）

2026-10-27 实测通过：**OpenWrt 6.18.55 内核 + 用户态已在真机 RAM 中启动成功**
（日志见 `../test_logs/openwrt_first_boot_20261027.log`）。

## 关键结论：这台机器的 U-Boot 只能这样启动自定义内核

| 事实 | 说明 |
|---|---|
| U-Boot 是 `U-Boot 2019.07-svn17541` | 裁剪版，**没有 `tftpboot` 的可用输入通道**（拿不到命令行提示符，autoboot 不响应按键） |
| `bootcmd=printenv;run once;sdk boot_img` | `once=loadx 0x02000000;bootm 0x02000000` |
| 唯一入口 = **XMODEM 串口上传** | 重启后会停在 `## Ready for binary (xmodem) download to 0x02000000` 并持续发 `C`，此时用 `loadx` 收固件，收完自动 `bootm` |
| `bootm` 必须是 **FIT**，不能是 legacy uImage | legacy uImage 不携带 FDT，会报 `FDT and ATAGS support not compiled in - hanging`。FIT 内含 `fdt@1` 才会走 FDT 路径正常启动 |
| 速率 | 115200 8N1，约 **11 KB/s**，6.2 MB 镜像约 **9~10 分钟** |

## 一次完整测试流程

```bash
# 0) 先确认设备在线（原厂固件）、并开启 telnet（开关不持久，每次重启都要开）
python fh_api.py telnet_on

# 1) 打包 FIT（从 initramfs uImage 里解出 zImage 和追加 DTB）
python - <<'EOF'
import lzma, re, struct
d = open('../test_images/openwrt-...-initramfs-kernel.bin','rb').read()
z = lzma.decompress(d[64:])                     # uImage 载荷 -> zImage
open('../test_images/zImage.bin','wb').write(z)
off = [m.start() for m in re.finditer(b'\xd0\x0d\xfe\xed', z)][-1]
tot = struct.unpack('>I', z[off+4:off+8])[0]
open('../test_images/our.dtb','wb').write(z[off:off+tot])
EOF
python fit_build.py ../test_images/zImage.bin ../test_images/our.dtb ../test_images/openwrt-fit.itb

# 2) 后台挂起发送器（等 "## Ready for binary" + CCC 后自动开始）
python xmodem_send.py ../test_images/openwrt-fit.itb xmodem.log 240 &

# 3) 重启设备，让它进入 XMODEM 接收态
cd .. && python dev.py root "reboot"

# 4) 约 10 分钟后观察串口：Starting kernel ... -> Linux version 6.18.55
```

## 脚本说明

| 脚本 | 用途 |
|---|---|
| `fh_api.py` | `/fh_tool/api`（AES-CBC）调用：`telnet_on` / `admin` / `info` / `call <Func>` |
| `serial_console.py` | 串口列表 / 抓取 / 发送 / 交互控制台（自动识别 CH340/CP210x/FTDI） |
| `uboot_run.py` | 尝试打断 autoboot 并执行命令（本机实测**打不进去**，保留备用） |
| `xmodem_send.py` | XMODEM-1K(CRC) 发送器，配合 U-Boot `loadx` |
| `fit_build.py` | 纯 Python 的 FIT 打包器（kernel + fdt + crc32 校验节点） |
| `tftp_server.py` | 备用 TFTP 服务（当前 U-Boot 用不上） |

## 已知问题 / 下一步

1. **80 秒看门狗复位**：U-Boot 开启硬件看门狗（`brcm,bcm6345-wdt` @ `ff800480`，80s），
   内核原先没编驱动 → 启动后约 80 秒硬复位回到原厂固件。
   → 已加 `CONFIG_BCM7038_WDT=y` + `watchdog` 包（本目录 Makefile/config 已改）。
2. **没有网络**：`bcm_enet`（Broadcom Runner/RDPA 交换芯片）尚未移植完；
   但板上 PCIe 有 **RTL8125 (10ec:8125)**，主线 `r8169` 即可驱动 → 已加 `kmod-r8169`。
3. 设备始终**只跑内存**，未写任何 flash；断电即回原厂固件，随时可恢复。
