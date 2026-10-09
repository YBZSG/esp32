param([int]$Port = 8080)
$cloudflared = Get-Command cloudflared -ErrorAction SilentlyContinue
if (-not $cloudflared) {
  Write-Error "未找到 cloudflared。请先安装 Cloudflare Tunnel 客户端。"
  exit 1
}
if ($env:MONITOR_TOKEN -eq "change-me-before-public-access" -or -not $env:MONITOR_TOKEN) {
  Write-Error "公开访问前必须设置安全的 MONITOR_TOKEN，并与固件 API_TOKEN 保持一致。"
  exit 1
}
& $cloudflared.Source tunnel --url "http://localhost:$Port"
