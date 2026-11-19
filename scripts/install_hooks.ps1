$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

git config --local core.hooksPath .githooks
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to configure core.hooksPath."
    exit $LASTEXITCODE
}

Write-Host "Git hooks enabled from $root\.githooks"
