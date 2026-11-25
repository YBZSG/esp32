param(
  [string]$Token = "change-me-before-public-access",
  [string]$ViewToken = "",
  [int]$Port = 8080,
  [string]$Esp32Url = "",
  [string]$NotifyWebhook = "",
  [string]$PushPlusToken = "",
  [string]$DingTalkSecret = "",
  [double]$TMin = 20,
  [double]$TMax = 26,
  [double]$HMin = 40,
  [double]$HMax = 60,
  [double]$HoldMin = 10
)
$env:MONITOR_TOKEN = $Token
$env:MONITOR_VIEW_TOKEN = $ViewToken
$env:MONITOR_PORT = "$Port"
$env:ALERT_T_MIN = "$TMin"
$env:ALERT_T_MAX = "$TMax"
$env:ALERT_H_MIN = "$HMin"
$env:ALERT_H_MAX = "$HMax"
$env:ALERT_HOLD_MIN = "$HoldMin"
$Esp32Url = $Esp32Url.Trim()
if ($Esp32Url -and $Esp32Url -notmatch '^https?://') { $Esp32Url = "http://$Esp32Url" }
$env:ESP32_URL = $Esp32Url
$env:NOTIFY_WEBHOOK = $NotifyWebhook
$env:PUSHPLUS_TOKEN = $PushPlusToken
$env:DINGTALK_SECRET = $DingTalkSecret
node "$PSScriptRoot\server.js"
