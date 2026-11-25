# ESP32 智能温湿度管家

主版本是 **触摸屏环境健康助手**（[`cyd-environment`](cyd-environment/README.md)），面向 ESP32-2432S028R（CYD）。温湿度传感器已由 DHT22 换为 **SHT3x**（I2C）。

## 功能

- SHT3x 高精度温度、湿度、露点、结露余量、绝对湿度和焓值
- 霉变风险计时；1 Hz 采样识别吹气、开窗、加湿、除湿等短时事件
- 6 分钟实时曲线、24 小时历史曲线与超标统计（来自 TF 卡记录）
- 触摸屏与局域网网页：环境、趋势、Miku 宠物、室外天气
- DeepSeek 对话、Qwen 语音合成和区间提醒
- 电脑只读中继，支持 PushPlus 微信提醒、企业微信/钉钉群机器人
- 可选的 Cloudflare 隧道，用于外网查看只读页面

## 温湿度接线

SHT3x 使用 I2C，默认地址 `0x44`：

- `VCC` → 3.3V
- `GND` → GND
- `SDA` → `GPIO27`（沿用原 4 针接口的 DATA 脚）
- `SCL` → `GPIO22`（P3 排针）

GY-SHT3x 模块自带 4.7 kΩ 上拉；裸芯片需要在 SDA/SCL 外接 4.7～10 kΩ 上拉到 3.3V。`GPIO26` 是喇叭 DAC 输出，不要占用。

## 构建与烧录

```powershell
cd G:\esp32\cyd-environment
pio run
pio run -t upload --upload-port COM7
pio device monitor -p COM7 -b 115200
```

动画文件 `cyd-environment/assets/miku.rle` 需要复制到 TF 卡根目录。AI 对话和语音服务的密钥通过设备网页配置，详细说明见 [cyd-environment/README.md](cyd-environment/README.md)。

## 设备使用

1. 在屏幕“网络”页连接 2.4 GHz Wi-Fi，记下屏幕显示的设备 IP。
2. 同一局域网内用浏览器打开 `http://设备IP/`，可查看环境、趋势、Miku 和天气。
3. 在 `http://设备IP/alert-config` 设置温湿度区间、允许偏差和持续时长。

## 电脑只读中继 + 微信提醒

电脑每 5 秒轮询 CYD 的 `/api`，不需要改固件；只读页面可以通过隧道公开，数据接口需要查看密码。

1. 确认电脑能访问设备 IP，例如 `http://192.168.1.50/api` 能返回 JSON。
2. 个人微信使用 PushPlus：浏览器打开 <https://www.pushplus.plus/>，微信扫码登录，复制 token；按提示关注“pushplus推送加”公众号后才能收到消息。
3. 启动上位机（把 IP 和 token 换成真实值）：

```powershell
cd G:\esp32
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

cloudflared 会输出一个临时的 `https://xxx.trycloudflare.com` 地址，手机用流量打开 `https://xxx.trycloudflare.com/?key=手机查看密码` 即可。两个窗口都要保持运行；Quick Tunnel 的域名每次重启都会变化，正式长期使用建议配置命名 Tunnel 和 Cloudflare Access。

提醒规则：

- 设备网页 `/alert-config` 启用区间提醒时，直接使用设备计算结果（已包含容差和持续时间）。
- 设备未启用时，电脑按 `-TMin`、`-TMax`、`-HMin`、`-HMax`、`-HoldMin` 判断。
- 超出区间发一次提醒，恢复正常后再发一次；设置 `$env:NOTIFY_RECOVERY = "0"` 可关闭恢复通知。
- 通知渠道可以同时配置 PushPlus 和群机器人，消息会并行发送。

## 旧版：DHT22 Wi-Fi 上位机（`firmware/`）

`firmware/` 保留早期 DHT22 主动上报固件，仅用于兼容旧接线。主版本 `cyd-environment` 已不再使用 DHT22。

<details>
<summary>旧版接线与烧录</summary>

链路：`DHT22 → ESP32 GPIO13 → Wi-Fi → Windows 上位机 → 浏览器仪表盘`。

- DHT22 `VCC` → 3.3V
- DHT22 `GND` → GND
- DHT22 `DAT/DATA` → `GPIO13`
- 裸 DHT22 建议在 VCC 与 DATA 之间加 4.7～10 kΩ 上拉

LCD9648（单色 SPI）：

- `GND` → `G`，`VCC` → `V`（JUMP 选 3.3V）
- `SCL/CLK` → `P18`，`SDA` → `P23`
- `RS` → `P27`，`CS` → `P25`，`RST` → `P26`

烧录步骤：

1. 将 `firmware/include/config.example.h` 复制为 `config.h`，填写 Wi-Fi、电脑局域网 IP 和 API Token。
2. 在 `firmware` 目录执行 `pio run -t upload --upload-port COM4`。
3. 电脑运行 `.\start-monitor.ps1 -Token "与固件相同的字符串"`，不要传 `-Esp32Url`，进入原有的 ESP32 主动上报模式。

</details>

真实 Wi-Fi 配置、编译缓存、设备 Flash 备份和原始厂商资料不纳入版本控制。
