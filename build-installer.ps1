$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$nsis = Get-Command makensis.exe -ErrorAction SilentlyContinue
if ($null -eq $nsis) {
    throw 'NSIS is required to build the installer.'
}

New-Item -ItemType Directory -Force (Join-Path $root 'release') | Out-Null

Push-Location $root
try {
    & $nsis.Source 'installer.nsi'
    if ($LASTEXITCODE -ne 0) {
        throw 'Installer build failed.'
    }
}
finally {
    Pop-Location
}

Write-Host "Built $root\release\Boykisser-Avatar-Setup-1.0.0.exe"
