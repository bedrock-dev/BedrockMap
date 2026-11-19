param(
    # Format staged files and stage the result after a successful run.
    [switch]$ForCommit
)

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$qtBin = if ($env:QT_ROOT) { Join-Path $env:QT_ROOT "bin" }
if ($qtBin -and (Test-Path -LiteralPath $qtBin)) {
    $env:PATH = "$qtBin;$env:PATH"
}

$batchSize = 200
$extensions = @('.cpp', '.cc', '.cxx', '.c', '.h', '.hpp', '.hxx', '.inl')
$exclude = '(^|\\)(build|build_rls|third|\.cache)(\\|$)'

function Test-FormattedSourcePath([string]$path) {
    $normalized = $path.Replace('\', '/')
    if ($normalized -match '(^|/)(build|build_rls|third|\.cache)(/|$)') {
        return $false
    }
    if ($normalized -match '^(src|bedrock-level/(src|app|tests))/' -and
        $extensions -contains [System.IO.Path]::GetExtension($normalized)) {
        return $true
    }
    return $normalized -match '^src/qml/.+\.qml$'
}

$stagedPathSet = $null
if ($ForCommit) {
    $stagedPaths = @(& git -c core.quotepath=false diff --cached --name-only --diff-filter=ACMR --)
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to read the staged file list."
        exit $LASTEXITCODE
    }
    if ($stagedPaths.Count -eq 0) {
        Write-Host "No staged files."
        exit 0
    }

    $stagedPathSet = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $stagedPaths) {
        $fullPath = [System.IO.Path]::GetFullPath((Join-Path $root $path))
        [void]$stagedPathSet.Add($fullPath)
    }

    $unstagedPaths = @(& git -c core.quotepath=false diff --name-only --diff-filter=ACMR --)
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to read the unstaged file list."
        exit $LASTEXITCODE
    }
    $unstagedPathSet = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase)
    foreach ($path in $unstagedPaths) {
        [void]$unstagedPathSet.Add($path.Replace('\', '/'))
    }
    $partiallyStaged = @($stagedPaths | Where-Object {
            (Test-FormattedSourcePath $_) -and
            $unstagedPathSet.Contains($_.Replace('\', '/'))
        })
    if ($partiallyStaged.Count -gt 0) {
        $message = "Cannot auto-format partially staged source files:`n  " +
            ($partiallyStaged -join "`n  ") +
            "`nStage each complete file or commit the partial changes separately."
        Write-Error $message
        exit 1
    }
}

# clang-format picks up the nearest .clang-format for each file, so the
# bedrock-level sources use bedrock-level/.clang-format automatically.
$targets = @('src', 'bedrock-level/src', 'bedrock-level/app', 'bedrock-level/tests') |
    Where-Object { Test-Path $_ }

# Vendored code and build output are left untouched.

$files = foreach ($target in $targets) {
    Get-ChildItem -Path $target -Recurse -File |
        Where-Object { $extensions -contains $_.Extension } |
        Where-Object { $_.FullName -notmatch $exclude } |
        ForEach-Object { $_.FullName }
}
$files = @($files)
if ($ForCommit) {
    $files = @($files | Where-Object { $stagedPathSet.Contains($_) })
}

if ($files.Count -eq 0) {
    Write-Host "No source files found."
}
else {
    Write-Host "Formatting $($files.Count) files..."

    # Batch the file list to stay well below the command line length limit.
    for ($i = 0; $i -lt $files.Count; $i += $batchSize) {
        $batch = $files[$i..([Math]::Min($i + $batchSize - 1, $files.Count - 1))]
        try {
            $output = & clang-format --style=file -i $batch 2>&1
            $code = $LASTEXITCODE
        }
        catch {
            Write-Error "clang-format could not be started: $($_.Exception.Message)"
            exit 1
        }
        if ($code -ne 0) {
            $output | ForEach-Object { Write-Host $_ }
            Write-Error "clang-format reported errors."
            exit 1
        }
    }
}

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
if ($ForCommit) {
    $qmlFiles = @($qmlFiles | Where-Object { $stagedPathSet.Contains($_) })
}
if ($qmlFiles.Count -eq 0) {
    Write-Host "No QML files found."
}
else {
    Write-Host "Formatting $($qmlFiles.Count) QML files..."
    for ($i = 0; $i -lt $qmlFiles.Count; $i += $batchSize) {
        $batch = $qmlFiles[$i..([Math]::Min($i + $batchSize - 1, $qmlFiles.Count - 1))]
        try {
            $output = & qmlformat -s $qmlConfig -i $batch 2>&1
            $code = $LASTEXITCODE
        }
        catch {
            Write-Error "qmlformat could not be started: $($_.Exception.Message)"
            exit 1
        }
        if ($code -ne 0) {
            $output | ForEach-Object { Write-Host $_ }
            Write-Error "qmlformat reported errors."
            exit 1
        }
    }
}

if ($ForCommit) {
    $filesToStage = @($files + $qmlFiles | Where-Object { $_ } | Select-Object -Unique)
    if ($filesToStage.Count -gt 0) {
        $relativeFiles = @($filesToStage | ForEach-Object {
                $_.Substring($root.Length).TrimStart([char[]]@('\', '/'))
            })
        & git add -- $relativeFiles
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Failed to stage formatted files."
            exit $LASTEXITCODE
        }
        Write-Host "Staged $($filesToStage.Count) formatted files."
    }
}

Write-Host "Done."
