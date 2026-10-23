<#
.SYNOPSIS
  Generates PortalVNC certificates next to the agent (default: this script's
  folder, i.e. C:\ProgramData\PortalVNC):
    devca.crt / devca.key   - local CA
    agent.crt / agent.key   - agent server certificate (CN=agent), signed by CA
    client.crt / client.key - viewer/portal client certificate (CN=portalvnc-client)

  The agent trusts devca.crt and presents agent.crt/key. Operators (the native
  viewer, or the portal gateway) connect with client.crt/key — copy those to the
  operator side. For production, replace these with certificates from your own CA
  (keep the same file names, or update config.json).

.NOTES
  Requires openssl on PATH. Run as administrator (ProgramData is machine-wide).
  Does not overwrite existing certificates unless -Force is given.
#>
param(
  [string]$OutDir = $PSScriptRoot,
  [switch]$Force
)
$ErrorActionPreference = "Stop"

if (-not (Get-Command openssl -ErrorAction SilentlyContinue)) {
  throw "openssl not found on PATH. Install OpenSSL (or Git for Windows) and retry."
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if ((Test-Path (Join-Path $OutDir 'agent.crt')) -and -not $Force) {
  Write-Host "Certificates already exist in $OutDir (use -Force to regenerate). Nothing to do."
  return
}

$cnf = Join-Path $OutDir "openssl-portalvnc.cnf"
@'
[req]
distinguished_name = dn
prompt = no
[dn]
CN = placeholder
[v3_ca]
basicConstraints = critical,CA:TRUE
keyUsage = critical,keyCertSign,cRLSign
[v3_leaf]
basicConstraints = critical,CA:FALSE
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = serverAuth,clientAuth
'@ | Set-Content -Path $cnf -Encoding ascii

$env:OPENSSL_CONF = $cnf
$ErrorActionPreference = 'Continue'
function Check($what) { if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" } }

Push-Location $OutDir
try {
  Write-Host "Generating CA..."
  & openssl genrsa -out devca.key 4096 2>&1 | Out-Null; Check "genrsa ca"
  & openssl req -x509 -new -nodes -key devca.key -sha256 -days 3650 `
    -subj "/CN=PortalVNC CA" -config $cnf -extensions v3_ca -out devca.crt 2>&1 | Out-Null; Check "req ca"

  function New-Signed($name, $cn) {
    & openssl genrsa -out "$name.key" 2048 2>&1 | Out-Null; Check "genrsa $name"
    & openssl req -new -key "$name.key" -subj "/CN=$cn" -config $cnf -out "$name.csr" 2>&1 | Out-Null; Check "req $name"
    & openssl x509 -req -in "$name.csr" -CA devca.crt -CAkey devca.key `
      -CAcreateserial -days 825 -sha256 -extfile $cnf -extensions v3_leaf -out "$name.crt" 2>&1 | Out-Null; Check "x509 $name"
    Remove-Item "$name.csr" -ErrorAction SilentlyContinue
  }

  Write-Host "Generating agent certificate (CN=agent)..."
  New-Signed "agent" "agent"
  Write-Host "Generating client certificate (CN=portalvnc-client)..."
  New-Signed "client" "portalvnc-client"

  Write-Host ""
  Write-Host "Done. Certificates in: $OutDir"
  Write-Host "Agent uses: devca.crt, agent.crt, agent.key"
  Write-Host "Operators use: devca.crt, client.crt, client.key (copy to the viewer/portal side)"
  Write-Host "Now set allowedClients in config.json and (re)start the PortalVNC service."
}
finally { Pop-Location }
