$scriptsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $scriptsDir\..
& "$scriptsDir\build.ps1"
& "$scriptsDir\run.ps1"
Pop-Location