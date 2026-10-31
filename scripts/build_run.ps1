param(
    [switch]$Rel
)

$scriptsDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $scriptsDir\..
try {
    & "$scriptsDir\build.ps1" -Rel:$Rel
    $buildExitCode = $LASTEXITCODE
    if ($buildExitCode -ne 0) {
        Write-Error "Build failed with exit code $buildExitCode. The application will not be started."
        exit $buildExitCode
    }

    & "$scriptsDir\run.ps1" -Rel:$Rel
    $runExitCode = $LASTEXITCODE
    if ($runExitCode -ne 0) {
        exit $runExitCode
    }
}
finally {
    Pop-Location
}
