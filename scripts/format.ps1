param(
    # Report files that are not formatted instead of rewriting them.
    [switch]$Check,
    # Path to clang-format.exe; auto-detected from PATH when omitted.
    [string]$ClangFormat = ""
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

if ($Check) {
    Write-Host "All files are properly formatted."
}
else {
    Write-Host "Done."
}
