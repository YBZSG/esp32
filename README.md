# ESP32 智能温湿度管家

项目包含两个版本：

- **触摸屏环境健康助手**：位于 [`cyd-environment`](cyd-environment/README.md)，面向 ESP32-2432S028R（CYD）。包含温湿度监测、环境建议、趋势图、局域网网页、TF 卡记录、Miku 互动宠物、DeepSeek 对话和 Qwen 语音合成。
- **Wi-Fi 上位机版**：位于 `firmware` 与根目录的 Node.js 服务，ESP32 将 DHT22 数据发送到电脑，在浏览器中查看仪表盘。接线与使用方法见下文。

触摸屏版在 `cyd-environment` 目录执行 `pio run` 构建；动画文件 `cyd-environment/assets/miku.rle` 需复制到 TF 卡根目录。AI 对话及语音服务的密钥通过设备网页配置，详细说明见对应 README。

真实 Wi-Fi 配置、编译缓存、设备 Flash 备份和原始厂商资料不纳入版本控制。

## DHT22 Wi-Fi 上位机版

链路：`DHT22 → ESP32 GPIO13 → Wi‑Fi → Windows 上位机 → 浏览器仪表盘`。

## 接线

- DHT22 `VCC` → ESP32 `3.3V`
- DHT22 `GND` → ESP32 `GND`
- DHT22 `DAT/DATA` → ESP32 `GPIO13`（板上可能印为 `G13`）
- 裸 DHT22 建议在 `VCC` 与 `DATA` 之间加一个 4.7kΩ～10kΩ 上拉电阻；带三针模块通常已经自带。

### LCD9648（单色 SPI，控制器待确认）

- `GND` → `G`
- `VCC` → `V`（JUMP 必须选择 3.3V）
- `NC` → 不连接
- `SCL/CLK` → `P18`
- `SDA` → `P23`
- `RS` → `P27`
- `CS` → `P25`
- `RST` → `P26`

DHT22 继续使用 `P13`，与 LCD 不冲突。所有接线都应在断电状态下完成。该屏不是 ST7735 彩屏，烧录显示驱动前需确认背面控制器型号或商品链接。

## 启动上位机

```powershell
.\start-monitor.ps1 -Token "与固件相同的字符串" -ViewToken "手机查看密码"
```

浏览器首次打开 `http://localhost:8080/?key=手机查看密码`。验证成功后浏览器会保存安全 Cookie。控制台还会打印局域网地址，ESP32 与电脑必须能互相访问。

## 配置与烧录固件

1. 安装 PlatformIO。
2. 将 `firmware/include/config.example.h` 复制为 `config.h`。
3. 填写 Wi‑Fi、电脑局域网 IP、API Token；Token 必须与上位机一致。
4. 在 `firmware` 目录执行 `pio run -t upload --upload-port COM4`。
5. 执行 `pio device monitor -p COM4 -b 115200` 查看读数和网络状态。

只支持 2.4 GHz Wi‑Fi。若路由器开启 AP 隔离，ESP32 将无法连接电脑。

## 内网穿透

### CYD 触摸屏版：只读中继 + 微信提醒

电脑每 5 秒轮询 CYD 的 `/api`，不需要改固件；只读页面可以通过隧道公开，数据接口需要查看密码。

1. 先在设备屏幕“网络”页连接 Wi-Fi，记下屏幕显示的设备 IP（例如 `192.168.1.50`）。
2. 个人微信使用 PushPlus：浏览器打开 <https://www.pushplus.plus/>，微信扫码登录，复制页面上的 token；按提示关注“pushplus推送加”公众号后才能收到消息。
3. 在能访问设备 IP 的电脑上启动上位机（把 token 换成真实值）：

```powershell
.\start-monitor.ps1 `
  -Esp32Url "http://192.168.1.50" `
  -Token "随机长字符串" `
  -ViewToken "手机查看密码" `
  -PushPlusToken "你的PushPlusToken" `
  -TMin 20 -TMax 26 -HMin 40 -HMax 60 -HoldMin 10
```

企业微信 / 钉钉群机器人则用 `-NotifyWebhook "https://..."` 代替 `-PushPlusToken`；钉钉启用“加签”时再加 `-DingTalkSecret "SEC..."`。

4. 本机打开 `http://localhost:8080/?key=手机查看密码` 确认页面正常。
5. 需要外网访问时，在另一个 PowerShell 窗口设置同样的两个 Token，再启动隧道：

```powershell
$env:MONITOR_TOKEN = "随机长字符串"
$env:MONITOR_VIEW_TOKEN = "手机查看密码"
.\start-tunnel.ps1
```

提醒规则：

- 设备网页 `/alert-config` 启用区间提醒时，直接使用设备计算结果（已包含容差和持续时间）。
- 设备未启用时，电脑按 `-TMin`、`-TMax`、`-HMin`、`-HMax`、`-HoldMin` 判断。
- 超出区间发一次提醒，恢复正常后再发一次；设置 `$env:NOTIFY_RECOVERY = "0"` 可关闭恢复通知。
- 通知渠道可以同时配置 PushPlus 和群机器人，消息会并行发送。

### DHT22 上位机版

先确认局域网通信正常，再使用 `start-tunnel.ps1`。Quick Tunnel 会生成临时公网 HTTPS 地址，适合临时查看；不要使用默认 Token。公网地址仅用于打开仪表盘，ESP32 仍优先向局域网电脑地址上报，避免网络不稳定和额外暴露。

```powershell
$env:MONITOR_TOKEN = "与你的固件相同的随机长字符串"
$env:MONITOR_VIEW_TOKEN = "手机查看密码"
.\start-tunnel.ps1
```

正式长期使用建议配置命名 Cloudflare Tunnel 并增加 Cloudflare Access 身份验证。
