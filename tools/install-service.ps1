<#
.SYNOPSIS
  Dev install of the PortalVNC agent as a Windows service. Self-elevates.
  Places exe + config + certs under ProgramData and registers the service.

.NOTES
  For production use the MSI (plan 5). This script is for testing the service
  path (including the lock screen / UAC capture) on a dev machine.
#>
param(
  [int]$Port = 7654,
  [string]$BuildDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "build")
)
$ErrorActionPreference = "Stop"

# Self-elevate.
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$pr = New-Object Security.Principal.WindowsPrincipal($id)
if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  Write-Host "Elevating..."
  Start-Process powershell -Verb RunAs -ArgumentList @(
    "-ExecutionPolicy","Bypass","-File","`"$PSCommandPath`"","-Port",$Port,"-BuildDir","`"$BuildDir`""
  )
  return
}

$root = Split-Path $PSScriptRoot -Parent
$certs = Join-Path $root "certs"
$agent = Join-Path $BuildDir "agent\pvnc-agent.exe"
if (-not (Test-Path $agent)) { throw "Build first: $agent not found" }
if (-not (Test-Path (Join-Path $certs 'agent.crt'))) {
  & powershell -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'make-dev-certs.ps1')
}

$dest = "C:\ProgramData\PortalVNC"
New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item $agent (Join-Path $dest 'pvnc-agent.exe') -Force
Copy-Item (Join-Path $certs 'agent.crt') $dest -Force
Copy-Item (Join-Path $certs 'agent.key') $dest -Force
Copy-Item (Join-Path $certs 'devca.crt') $dest -Force

# config.json next to the exe (the worker's default location), absolute paths.
$cfg = @{
  trayIcon       = $true
  listenAddress  = "0.0.0.0"
  port           = $Port
  tls            = @{
    cert = (Join-Path $dest 'agent.crt')
    key  = (Join-Path $dest 'agent.key')
    ca   = (Join-Path $dest 'devca.crt')
  }
  allowedClients = @("127.0.0.1")
  access         = @{ sids = @() }
  logFile        = (Join-Path $dest 'pvnc-agent.log')
} | ConvertTo-Json -Depth 5
Set-Content -Path (Join-Path $dest 'config.json') -Value $cfg -Encoding ascii

& (Join-Path $dest 'pvnc-agent.exe') --install
Start-Service PortalVNC
Start-Sleep 2
sc.exe query PortalVNC
Write-Host "Installed and started. Tray icon should appear in the active session."
Write-Host "Uninstall with: tools\uninstall-service.ps1"
