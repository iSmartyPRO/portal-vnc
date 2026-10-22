<#
.SYNOPSIS
  Removes the PortalVNC dev service and its ProgramData files. Self-elevates.
#>
$ErrorActionPreference = "SilentlyContinue"
$id = [Security.Principal.WindowsIdentity]::GetCurrent()
$pr = New-Object Security.Principal.WindowsPrincipal($id)
if (-not $pr.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
  Start-Process powershell -Verb RunAs -ArgumentList @("-ExecutionPolicy","Bypass","-File","`"$PSCommandPath`"")
  return
}
$dest = "C:\ProgramData\PortalVNC"
if (Test-Path (Join-Path $dest 'pvnc-agent.exe')) {
  & (Join-Path $dest 'pvnc-agent.exe') --uninstall
} else {
  sc.exe stop PortalVNC | Out-Null
  sc.exe delete PortalVNC | Out-Null
}
Start-Sleep 1
Remove-Item -Recurse -Force $dest -ErrorAction SilentlyContinue
Write-Host "PortalVNC service removed."
