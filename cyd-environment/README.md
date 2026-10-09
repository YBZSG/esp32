# ESP32-2432S028R 环境健康助手

独立的 PlatformIO/Arduino 固件，面向 ESP32-2432S028R（CYD）触摸屏开发板。

## 功能

- 触摸扫描并选择 2.4 GHz Wi-Fi，屏幕键盘输入密码；成功后凭据保存在 ESP32 NVS。
- 中文显示当前温度、湿度、露点、环境等级和调整建议。
- 保存最近约 6 分钟数据，显示温湿度曲线、均值和范围。
- 数值采用平滑过渡；指数滑动平均抑制 DHT22 跳动，时间线性回归给出约 10 分钟趋势估计。
- 默认白色日间界面，右上角可切换低亮度夜间界面。
- 极端温湿度、干燥、潮湿、闷热等场景给出健康提示与决策辅助。
- ESP32 内置局域网网页；同一 Wi-Fi 下的手机或电脑可查看实时数据、建议和趋势图。
- “宠物”页使用本机 Codex Pet 中已选择的 Miku：点按挥手、长按跳跃、滑动时看向对应方向，并随环境舒适度自动切换状态。
- 系统采用 240×320 竖屏布局，开机默认进入宠物页；环境监测作为独立功能保留。
- 局域网网页可填写 DeepSeek API Key 并与 Miku 对话；密钥仅保存在 ESP32 NVS，不写入源码、不通过接口回显。对话会携带当前温湿度和环境等级作为上下文。
- TF 卡挂载后，每分钟将温湿度及运行时间追加到 `environment.csv`。
- 已接入 Qwen 实时语音合成和 I2S/DAC 音频输出，可朗读 DeepSeek 回复；另有喇叭测试页面。源码接入不代表实物播音效果已验收。

宠物动画先在离屏画布合成，再一次性推送到屏幕，避免逐层擦除产生闪烁。57 帧 RGB565 RLE 动画已迁移到 TF 卡根目录的 `miku.rle`，固件启动时会校验文件头、尺寸和帧索引；卡未插入或文件损坏时会安全降级，不会读取越界。`tools/make_miku_asset.py` 可从保留的原始头文件重新生成该文件。网页的 `/miku-upload` 页面可以更新动画文件，写入后重启设备即可生效。这些宠物资产沿用当前设备已安装的 Codex Pet，发布或再分发前应另行核对其授权。

## 温湿度接口

根据随板 V0.1 官方原理图，板载 4 针温湿度接口的数据脚是 `GPIO27`，并带 10 kΩ 上拉：

- 1：3.3V
- 2：DATA / GPIO27
- 3：未连接
- 4：GND

当前实物和工作区已有传感器工程确认使用 DHT22/AM2302。务必核对模块的 VCC/DATA/GND 标识，不要仅按线材颜色判断。

## 构建与烧录

```powershell
cd G:\esp32\cyd-environment
pio run
pio run -t upload --upload-port COM10
pio device monitor -p COM10 -b 115200
```

当前只支持 2.4 GHz Wi-Fi。DHT22 有测量误差和响应延迟，预测是用于决策提示的短期线性趋势，不是专业气象预测或医疗结论。

## 手机或电脑查看

在屏幕“网络”页选择 Wi-Fi 并连接。成功后页面会显示 `http://192.168.x.x` 一类地址；让手机或电脑连接同一个 Wi-Fi，并在浏览器中打开该地址。ESP32 连接手机热点时，也可在开热点的手机上尝试打开，但部分系统的热点客户端隔离策略可能阻止访问。

在主页保存 DeepSeek API Key 后即可发送文字对话。需要语音时，打开设备地址的 `/qwen-config` 页面，保存百炼 API Key 和音色 ID，再发送对话。`/speaker-test` 用于播放测试音，`/tts/status` 用于查看语音状态。播放期间会暂停触摸采样，以避开音频与触摸共用 GPIO25 的干扰。

## 参考

- 工作区随板资料：规格书、V0.1 原理图、Arduino LovyanGFX 触摸示例。
- [ESP32 Cheap Yellow Display 社区资料](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display)
- [esp32-smartdisplay](https://github.com/rzeldent/esp32-smartdisplay)
- [Adafruit DHT 传感器库](https://github.com/adafruit/DHT-sensor-library)
