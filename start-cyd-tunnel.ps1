# 为 ESP32 触摸屏网页建立带口令的临时公网访问。
# 用法：
#   .\start-cyd-tunnel.ps1 -Esp32Host 192.168.1.50
#   $env:PROXY_PASS = "自定义密码"   # 可选，避免每次生成随机密码
#
# 链路：浏览器 --HTTPS--> Cloudflare Quick Tunnel --> 本机口令代理 --> ESP32:80
param(
  [Parameter(Mandatory = $true)][string]$Esp32Host,
  [int]$Port = 8081,
  [string]$User = "admin",
  [string]$Password = ""
)

$ErrorActionPreference = "Stop"

$cloudflared = Get-Command cloudflared -ErrorAction SilentlyContinue
if (-not $cloudflared) {
  Write-Error "未找到 cloudflared，请先安装 Cloudflare Tunnel 客户端。"
  exit 1
}
$node = Get-Command node -ErrorAction SilentlyContinue
if (-not $node) {
  Write-Error "未找到 node，口令代理需要 Node.js。"
  exit 1
}

if (-not $Password) {
  if ($env:PROXY_PASS) {
    $Password = $env:PROXY_PASS
  } else {
    $chars = (48..57) + (65..90) + (97..122)
    $Password = -join (1..14 | ForEach-Object { [char]($chars | Get-Random) })
  }
}

Write-Host "检查 ESP32 网页 http://$Esp32Host/ ..."
try {
  $null = Invoke-WebRequest -Uri "http://$Esp32Host/" -TimeoutSec 5 -UseBasicParsing
} catch {
  Write-Error "无法访问 http://$Esp32Host/。请先在屏幕网络页连接 Wi-Fi，并确认电脑与 ESP32 在同一局域网。"
  exit 1
}

$env:ESP32_HOST = $Esp32Host
$env:ESP32_PORT = "80"
$env:PROXY_PORT = "$Port"
$env:PROXY_USER = $User
$env:PROXY_PASS = $Password

$proxyScript = Join-Path $PSScriptRoot "tunnel-proxy.js"
$proxy = Start-Process -FilePath $node.Source -ArgumentList "`"$proxyScript`"" -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 900
if ($proxy.HasExited) {
  Write-Error "口令代理启动失败，请检查 Node.js 和 tunnel-proxy.js。"
  exit 1
}

Write-Host ""
Write-Host "本地地址：http://127.0.0.1:$Port/"
Write-Host "登录用户：$User"
Write-Host "登录密码：$Password"
Write-Host "公网地址会在下方由 cloudflared 输出；按 Ctrl+C 结束隧道，代理会自动关闭。"
Write-Host ""

try {
  & $cloudflared.Source tunnel --url "http://127.0.0.1:$Port"
} finally {
  if ($proxy -and -not $proxy.HasExited) {
    Stop-Process -Id $proxy.Id -Force -ErrorAction SilentlyContinue
  }
}
