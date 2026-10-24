param(
    # Report files that are not formatted instead of rewriting them.
    [switch]$Check,
    # Path to clang-format.exe; auto-detected from PATH when omitted.
    [string]$ClangFormat = "",
    # Path to qmlformat.exe; auto-detected from QT_ROOT or PATH when omitted.
    [string]$QmlFormat = ""
)

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $ClangFormat) {
    $cmd = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($cmd) { $ClangFormat = $cmd.Source }
}
if (-not $ClangFormat -or -not (Test-Path $ClangFormat)) {
    Write-Error "clang-format not found. Add it to PATH or pass -ClangFormat <path>."
    exit 1
}
Write-Host "clang-format: $ClangFormat"

# clang-format picks up the nearest .clang-format for each file, so the
# bedrock-level sources use bedrock-level/.clang-format automatically.
$targets = @('src', 'bedrock-level/src', 'bedrock-level/app', 'bedrock-level/tests') |
    Where-Object { Test-Path $_ }

# Vendored code and build output are left untouched.
$exclude = '(^|\\)(build|build_rls|third|\.cache)(\\|$)'
$extensions = @('.cpp', '.cc', '.cxx', '.c', '.h', '.hpp', '.hxx', '.inl')

$files = foreach ($target in $targets) {
    Get-ChildItem -Path $target -Recurse -File |
        Where-Object { $extensions -contains $_.Extension } |
        Where-Object { $_.FullName -notmatch $exclude } |
        ForEach-Object { $_.FullName }
}
$files = @($files)

if ($files.Count -eq 0) {
    Write-Host "No source files found."
    exit 0
}
Write-Host "Formatting $($files.Count) files..."

# Batch the file list to stay well below the command line length limit.
$batchSize = 200
$failed = @()
for ($i = 0; $i -lt $files.Count; $i += $batchSize) {
    $batch = $files[$i..([Math]::Min($i + $batchSize - 1, $files.Count - 1))]
    if ($Check) {
        $output = & $ClangFormat --style=file --dry-run --Werror $batch 2>&1
        $code = $LASTEXITCODE
    }
    else {
        $output = & $ClangFormat --style=file -i $batch 2>&1
        $code = $LASTEXITCODE
    }
    if ($code -ne 0) {
        $failed += $output
    }
}

if ($failed.Count -gt 0) {
    $failed | ForEach-Object { Write-Host $_ }
    if ($Check) { exit 1 }
    Write-Error "clang-format reported errors."
    exit 1
}

if (-not $QmlFormat -and $env:QT_ROOT) {
    $candidate = Join-Path $env:QT_ROOT "bin\qmlformat.exe"
    if (Test-Path -LiteralPath $candidate) {
        $QmlFormat = $candidate
    }
}
if (-not $QmlFormat) {
    $cmd = Get-Command qmlformat -ErrorAction SilentlyContinue
    if ($cmd) { $QmlFormat = $cmd.Source }
}
if (-not $QmlFormat -or -not (Test-Path -LiteralPath $QmlFormat)) {
    Write-Error "qmlformat not found. Set QT_ROOT, add it to PATH, or pass -QmlFormat <path>."
    exit 1
}
Write-Host "qmlformat: $QmlFormat"

$qmlConfig = Join-Path $root ".qmlformat.ini"
if (-not (Test-Path -LiteralPath $qmlConfig)) {
    Write-Error "QML formatting settings not found: $qmlConfig"
    exit 1
}

# Format only project-owned QML. Build trees contain copies of Qt modules and
# must never be rewritten by this script.
$qmlRoot = Join-Path $root "src\qml"
$qmlFiles = @(Get-ChildItem -LiteralPath $qmlRoot -Recurse -Filter *.qml -File |
        Sort-Object FullName |
        ForEach-Object { $_.FullName })
if ($qmlFiles.Count -eq 0) {
    Write-Host "No QML files found."
}
else {
    Write-Host "$(if ($Check) { 'Checking' } else { 'Formatting' }) $($qmlFiles.Count) QML files..."
    $qmlFailed = @()
    $tempFile = $null
    try {
        for ($i = 0; $i -lt $qmlFiles.Count; $i += $batchSize) {
            $batch = $qmlFiles[$i..([Math]::Min($i + $batchSize - 1, $qmlFiles.Count - 1))]
            if ($Check) {
                foreach ($file in $batch) {
                    if (-not $tempFile) {
                        $tempFile = Join-Path ([System.IO.Path]::GetTempPath()) `
                            "bedrockmap-qmlformat-$([guid]::NewGuid().ToString('N')).qml"
                    }
                    Copy-Item -LiteralPath $file -Destination $tempFile -Force
                    $output = & $QmlFormat -s $qmlConfig -i $tempFile 2>&1
                    if ($LASTEXITCODE -ne 0) {
                        $qmlFailed += $output
                        continue
                    }
                    $original = [System.IO.File]::ReadAllText($file)
                    $formatted = [System.IO.File]::ReadAllText($tempFile)
                    if ($original -cne $formatted) {
                        $qmlFailed += "Not formatted: $file"
                    }
                }
            }
            else {
                $output = & $QmlFormat -s $qmlConfig -i $batch 2>&1
                if ($LASTEXITCODE -ne 0) {
                    $qmlFailed += $output
                }
            }
        }
    }
    finally {
        if ($tempFile) {
            Remove-Item -LiteralPath $tempFile -Force -ErrorAction SilentlyContinue
        }
    }

    if ($qmlFailed.Count -gt 0) {
        $qmlFailed | ForEach-Object { Write-Host $_ }
        if ($Check) { exit 1 }
        Write-Error "qmlformat reported errors."
        exit 1
    }
}

if ($Check) {
    Write-Host "All files are properly formatted."
}
else {
    Write-Host "Done."
}
