<#
.SYNOPSIS
  Runs the PortalVNC vertical slice locally: ensures dev certs exist, writes a
  local agent config bound to 127.0.0.1:7654, starts the agent, and prints the
  viewer command to connect.

.NOTES
  Build first:  cmake --preset default; cmake --build --preset default
  Run elevated so the agent's logon + Administrators SID check can succeed.
#>
param(
  [int]$Port = 7654,
  [string]$BuildDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "build")
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$certs = Join-Path $root "certs"

if (-not (Test-Path (Join-Path $certs "agent.crt"))) {
  Write-Host "Generating dev certificates..."
  & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "make-dev-certs.ps1")
}

$cfgPath = Join-Path $certs "agent-config.json"
$cfg = @{
  trayIcon       = $false
  listenAddress  = "127.0.0.1"
  port           = $Port
  tls            = @{
    cert = (Join-Path $certs "agent.crt")
    key  = (Join-Path $certs "agent.key")
    ca   = (Join-Path $certs "devca.crt")
  }
  allowedClients = @("127.0.0.1")
  access         = @{ sids = @() }
  logFile        = (Join-Path $certs "pvnc-agent.log")
} | ConvertTo-Json -Depth 5
Set-Content -Path $cfgPath -Value $cfg -Encoding ascii
Write-Host "Wrote agent config: $cfgPath"

$agent  = Join-Path $BuildDir "agent\pvnc-agent.exe"
$viewer = Join-Path $BuildDir "viewer\pvnc-viewer.exe"
if (-not (Test-Path $agent))  { throw "Agent not built: $agent" }

Write-Host "Starting agent on 127.0.0.1:$Port ..."
Start-Process -FilePath $agent -ArgumentList @("--config", $cfgPath)

Write-Host ""
Write-Host "Connect with the viewer (will prompt for your Windows password):"
Write-Host "  $viewer --host 127.0.0.1 --port $Port ``"
Write-Host "    --ca `"$certs\devca.crt`" --cert `"$certs\client.crt`" --key `"$certs\client.key`" ``"
Write-Host "    --user $env:USERNAME"
