param([switch]$BuildBL)

$scriptsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $scriptsDir\..
& "$scriptsDir\build.ps1" -BuildBL:$BuildBL
& "$scriptsDir\run.ps1"
Pop-Location