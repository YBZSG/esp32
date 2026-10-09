param(
  [string]$Token = "change-me-before-public-access",
  [string]$ViewToken = "",
  [int]$Port = 8080
)
$env:MONITOR_TOKEN = $Token
$env:MONITOR_VIEW_TOKEN = $ViewToken
$env:MONITOR_PORT = "$Port"
node "$PSScriptRoot\server.js"
