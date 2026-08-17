param(
    [Parameter(Position = 0)]
    [string]$ModelPath = "",

    [string]$OutputPath = "",

    [ValidateSet("iso", "front", "side", "top", "low")]
    [string]$View = "iso",

    [ValidateSet("draft", "fine", "final")]
    [string]$Quality = "fine",

    [string]$Size = "",

    [int]$Width = 0,

    [int]$Height = 0,

    [double]$FramePadding = 1.9,

    [int]$Samples = 0,


    [double]$Brightness = 1.0,

    [double]$TransparentAlphaCap = 0.65,

    [ValidateSet("voxel-shell", "exact")]
    [string]$TransparentAlphaMode = "voxel-shell",

    [double]$Azimuth = [double]::NaN,

    [double]$Elevation = [double]::NaN,

    [ValidateSet("raw", "standard", "agx")]
    [string]$ColorManagement = "raw",

    [switch]$NoRenderWindow,

    [switch]$Background,

    [switch]$Detached,

    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-BedrockMapRenderPreviewHelp {
    @"
BedrockMap GLB Blender Preview

Usage:
  .\scripts\render_glb_preview.ps1 [model.glb] [options]

Examples:
  .\scripts\render_glb_preview.ps1
      Open Blender and show a file picker.

  .\scripts\render_glb_preview.ps1 .\model.glb
      Render with the default fine preset, Raw color management, and a render progress window.

  .\scripts\render_glb_preview.ps1 .\model.glb -View low
      Render from a low angle, useful for underside checks.

  .\scripts\render_glb_preview.ps1 .\model.glb -Azimuth 45
  .\scripts\render_glb_preview.ps1 .\model.glb -Azimuth 135
  .\scripts\render_glb_preview.ps1 .\model.glb -Azimuth 225
  .\scripts\render_glb_preview.ps1 .\model.glb -Azimuth 315
      Render the four orthographic 45-degree corner directions.

  .\scripts\render_glb_preview.ps1 .\model.glb -Quality final -Brightness 0.8
      Render a higher-quality preview and slightly reduce lighting.

  .\scripts\render_glb_preview.ps1 .\model.glb -Size 1920x1080
  .\scripts\render_glb_preview.ps1 .\model.glb -Size 1920*1080
      Render a custom rectangular image.

  .\scripts\render_glb_preview.ps1 .\model.glb -Size 1920x1080 -FramePadding 1.9
      Leave more empty border if the model is still too close to the frame edge.

  .\scripts\render_glb_preview.ps1 .\model.glb -TransparentAlphaCap 1.0
      Disable the high-alpha clamp. With the default voxel-shell alpha mode,
      transparent voxel faces are still softened for ray-traced preview.

  .\scripts\render_glb_preview.ps1 .\model.glb -TransparentAlphaMode exact
      Apply exported alpha directly to every mesh face. This is useful for
      debugging raw GLB data, but glass/water can look too opaque in Cycles.

  .\scripts\render_glb_preview.ps1 .\model.glb -Background
      Render without Blender UI. Progress is printed in this terminal; no render window is shown.

Options:
  -ModelPath <path>
      Optional .glb/.gltf path. If omitted, Blender opens a file picker.

  -OutputPath <path>
      Optional output PNG path. Defaults to '<model>_blender_preview.png' beside the model.

  -View <iso|front|side|top|low>
      Camera preset. Default: iso.

  -Quality <draft|fine|final>
      Render preset. Default: fine.
      draft: 1200px / 64 samples
      fine:  2200px / 256 samples
      final: 3200px / 512 samples

  -Size <width>x<height>
      Override output size. Examples: 1920x1080, 1920*1080, 1920×1080.

  -Width <number>
      Override output width. If -Height is omitted, the same value is used.

  -Height <number>
      Override output height. If -Width is omitted, the same value is used.

  -FramePadding <number>
      Camera framing multiplier. Default: 1.9.
      Larger values leave more empty border around the model.

  -Samples <number>
      Override Cycles sample count.

  -Brightness <number>
      Light multiplier. Default: 1.0. Try 0.6 if overexposed, 1.3 if too dark.

  -TransparentAlphaCap <number>
      Maximum alpha for transparent materials. Default: 0.65.
      This makes high-alpha water visible while preserving lower-alpha glass/leaves.
      Use 1.0 to disable the clamp.

  -TransparentAlphaMode <voxel-shell|exact>
      Default: voxel-shell.
      voxel-shell: treats vertex alpha as the opacity of a whole transparent voxel shell,
        then converts it to a softer per-face alpha for Cycles.
      exact: applies exported alpha directly to every rendered face.

  -Azimuth <degrees>
      Override camera yaw around the model. Common corner values: 45, 135, 225, 315.

  -Elevation <degrees>
      Override camera elevation. If -Azimuth is set, default is 35 degrees, or -16 with -View low.

  -ColorManagement <raw|standard|agx>
      Display transform. Default: raw.
      raw: closest direct voxel-color inspection
      standard: normal display transform, usually punchier than AgX
      agx: filmic-style highlight handling, but can look gray/desaturated

  -NoRenderWindow
      Do not open Blender's render progress window.

  -Background
      Run Blender in background mode. No UI/render window.

  -Detached
      Start Blender detached from this PowerShell terminal.

  -Help
      Show this help and exit.

Blender discovery:
  The launcher checks BLENDER_EXE, PATH, and common 'Blender Foundation' install folders.
"@
}

if ($Help) {
    Show-BedrockMapRenderPreviewHelp
    return
}

function ConvertTo-ProcessArgument {
    param([Parameter(Mandatory = $true)][string]$Value)
    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

$scriptPath = Join-Path $PSScriptRoot "blender_render_preview.py"
if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "Cannot find Blender preview script: $scriptPath"
}

$candidates = New-Object System.Collections.Generic.List[string]

if ($env:BLENDER_EXE) {
    $candidates.Add($env:BLENDER_EXE)
}

$command = Get-Command blender.exe -ErrorAction SilentlyContinue
if ($command) {
    $candidates.Add($command.Source)
}

$programRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) | Where-Object { $_ }
foreach ($root in $programRoots) {
    $blenderRoot = Join-Path $root "Blender Foundation"
    if (Test-Path -LiteralPath $blenderRoot) {
        Get-ChildItem -LiteralPath $blenderRoot -Filter blender.exe -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            ForEach-Object { $candidates.Add($_.FullName) }
    }
}

$blenderPath = $candidates |
    Where-Object { $_ -and (Test-Path -LiteralPath $_) } |
    Select-Object -First 1

if (-not $blenderPath) {
    throw "Cannot find blender.exe. Install Blender, add it to PATH, or set BLENDER_EXE to the full blender.exe path."
}

$blenderArgs = New-Object System.Collections.Generic.List[string]
if ($Background) {
    $blenderArgs.Add("--background")
}
$blenderArgs.Add("--python")
$blenderArgs.Add($scriptPath)

$scriptArgs = New-Object System.Collections.Generic.List[string]
if ($ModelPath) {
    $resolvedModelPath = (Resolve-Path -LiteralPath $ModelPath).Path
    $scriptArgs.Add("--model")
    $scriptArgs.Add($resolvedModelPath)
}
if ($OutputPath) {
    $scriptArgs.Add("--output")
    $scriptArgs.Add($OutputPath)
}
$scriptArgs.Add("--view")
$scriptArgs.Add($View)
$scriptArgs.Add("--quality")
$scriptArgs.Add($Quality)
if ($Size) {
    $scriptArgs.Add("--size")
    $scriptArgs.Add($Size)
}
if ($Width -gt 0) {
    $scriptArgs.Add("--width")
    $scriptArgs.Add([string]$Width)
}
if ($Height -gt 0) {
    $scriptArgs.Add("--height")
    $scriptArgs.Add([string]$Height)
}
$scriptArgs.Add("--frame-padding")
$scriptArgs.Add([string]$FramePadding)
if ($Samples -gt 0) {
    $scriptArgs.Add("--samples")
    $scriptArgs.Add([string]$Samples)
}
$scriptArgs.Add("--brightness")
$scriptArgs.Add([string]$Brightness)
$scriptArgs.Add("--transparent-alpha-cap")
$scriptArgs.Add([string]$TransparentAlphaCap)
$scriptArgs.Add("--transparent-alpha-mode")
$scriptArgs.Add($TransparentAlphaMode)
if (-not [double]::IsNaN($Azimuth)) {
    $scriptArgs.Add("--azimuth")
    $scriptArgs.Add([string]$Azimuth)
}
if (-not [double]::IsNaN($Elevation)) {
    $scriptArgs.Add("--elevation")
    $scriptArgs.Add([string]$Elevation)
}
$scriptArgs.Add("--color-management")
$scriptArgs.Add($ColorManagement)
if ($NoRenderWindow) {
    $scriptArgs.Add("--no-render-window")
}

if ($scriptArgs.Count -gt 0) {
    $blenderArgs.Add("--")
    foreach ($arg in $scriptArgs) {
        $blenderArgs.Add($arg)
    }
}

Write-Host "Starting Blender: $blenderPath"
$angleText = ""
if (-not [double]::IsNaN($Azimuth)) {
    $angleText += ", Azimuth: $Azimuth"
}
if (-not [double]::IsNaN($Elevation)) {
    $angleText += ", Elevation: $Elevation"
}
$sizeText = ""
if ($Size) {
    $sizeText = ", Size: $Size"
} elseif ($Width -gt 0 -or $Height -gt 0) {
    $sizeText = ", Size: ${Width}x${Height}"
}
Write-Host "Quality: $Quality$sizeText, View: $View$angleText, FramePadding: $FramePadding, Brightness: $Brightness, TransparentAlphaCap: $TransparentAlphaCap, TransparentAlphaMode: $TransparentAlphaMode, ColorManagement: $ColorManagement"
if ($Detached) {
    $argumentLine = ($blenderArgs | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join " "
    Start-Process -FilePath $blenderPath -ArgumentList $argumentLine
} else {
    Write-Host "Blender progress will be printed below. Keep this terminal open until rendering finishes."
    & $blenderPath @blenderArgs
}
