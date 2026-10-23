<#
.SYNOPSIS
  Builds PortalVNC.msi from the WiX source. Requires the WiX v5 dotnet tool and
  the WixToolset.Firewall.wixext extension (v5.0.2):
    dotnet tool install --global wix
    wix extension add -g WixToolset.Firewall.wixext/5.0.2
#>
param(
  [string]$AgentExe = (Join-Path (Split-Path $PSScriptRoot -Parent) "build\agent\Release\pvnc-agent.exe"),
  [int]$Port = 7654,
  [string]$Version = "0.1.1",
  [string]$Out = (Join-Path $PSScriptRoot "PortalVNC.msi")
)
$ErrorActionPreference = "Stop"
if (-not (Test-Path $AgentExe)) { throw "Build the agent first: $AgentExe not found" }

Push-Location $PSScriptRoot
try {
  & wix build PortalVNC.wxs `
    -ext WixToolset.Firewall.wixext `
    -d "AgentExe=$AgentExe" `
    -d "ConfigSrc=config.install.json" `
    -d "Port=$Port" `
    -d "Ver=$Version" `
    -o $Out
  if ($LASTEXITCODE -ne 0) { throw "wix build failed ($LASTEXITCODE)" }
  Write-Host "Built: $Out"
}
finally { Pop-Location }
