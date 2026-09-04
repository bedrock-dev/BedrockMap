param(
    [switch]$Rel
)

$scriptsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $scriptsDir\..
& "$scriptsDir\build.ps1" -Rel:$Rel
& "$scriptsDir\run.ps1" -Rel:$Rel
Pop-Location