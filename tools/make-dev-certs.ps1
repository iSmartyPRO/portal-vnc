<#
.SYNOPSIS
  Generates development certificates for PortalVNC into .\certs (gitignored):
    devca.crt / devca.key   - the development CA
    agent.crt / agent.key   - agent server cert (CN=agent), signed by the CA
    client.crt / client.key - viewer/portal client cert (CN=portalvnc-client)
    rogue.crt / rogue.key   - a self-signed cert NOT trusted by the CA (for tests)

  These are for local development and tests ONLY. Production provisioning of the
  per-fleet certificate is a separate concern (see the design doc).

.NOTES
  Requires openssl on PATH (ships with Git for Windows / OpenSSL-Win64).
  Uses a self-contained config so a broken system openssl.cnf does not matter.
#>
param(
  [string]$OutDir = (Join-Path (Split-Path $PSScriptRoot -Parent) "certs")
)

$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

# Minimal, self-contained OpenSSL config (avoids the system openssl.cnf).
$cnf = Join-Path $OutDir "openssl-dev.cnf"
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

# openssl writes informational text to stderr; a non-zero exit is the only real
# failure, so do not let stderr abort the script.
$ErrorActionPreference = 'Continue'
function Check($what) { if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" } }

Push-Location $OutDir
try {
  Write-Host "Generating dev CA..."
  & openssl genrsa -out devca.key 4096 2>&1 | Out-Null; Check "genrsa ca"
  & openssl req -x509 -new -nodes -key devca.key -sha256 -days 3650 `
    -subj "/CN=PortalVNC Dev CA" -config $cnf -extensions v3_ca -out devca.crt 2>&1 | Out-Null; Check "req ca"

  function New-Signed($name, $cn) {
    & openssl genrsa -out "$name.key" 2048 2>&1 | Out-Null; Check "genrsa $name"
    & openssl req -new -key "$name.key" -subj "/CN=$cn" -config $cnf -out "$name.csr" 2>&1 | Out-Null; Check "req $name"
    & openssl x509 -req -in "$name.csr" -CA devca.crt -CAkey devca.key `
      -CAcreateserial -days 825 -sha256 -extfile $cnf -extensions v3_leaf -out "$name.crt" 2>&1 | Out-Null; Check "x509 $name"
    Remove-Item "$name.csr" -ErrorAction SilentlyContinue
  }

  Write-Host "Generating agent cert (CN=agent)..."
  New-Signed "agent" "agent"
  Write-Host "Generating client cert (CN=portalvnc-client)..."
  New-Signed "client" "portalvnc-client"

  Write-Host "Generating rogue self-signed cert (untrusted)..."
  & openssl genrsa -out rogue.key 2048 2>&1 | Out-Null; Check "genrsa rogue"
  & openssl req -x509 -new -nodes -key rogue.key -sha256 -days 825 `
    -subj "/CN=rogue" -config $cnf -extensions v3_leaf -out rogue.crt 2>&1 | Out-Null; Check "req rogue"

  Write-Host "Done. Certificates in: $OutDir"
  Get-ChildItem *.crt, *.key | Select-Object Name | Format-Table -AutoSize
}
finally {
  Pop-Location
}
