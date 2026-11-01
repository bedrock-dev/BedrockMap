param(
    [string]$QtRoot = 'C:\noinstall\Qt\6.11.2',
    [string]$SdkRoot = '',
    [string]$NdkRoot = 'C:\noinstall\android-ndk-r30',
    [string]$JavaRoot = $env:JAVA_HOME,
    [string]$BuildDir = 'build_android_rls',
    [string]$CompileSdk = '37.0',
    [string]$ProxyUrl = '',
    [ValidateRange(1, 256)]
    [int]$Jobs = 8,
    [string]$KeystorePath = $env:BEDROCKMAP_KEYSTORE_PATH,
    [string]$KeystoreAlias = 'bedrockmap',
    [string]$KeystoreCredentials = $env:BEDROCKMAP_KEYSTORE_CREDENTIALS,
    [switch]$NativeOnly
)

$ErrorActionPreference = 'Stop'

# Resolve local tools and output paths.
$sourceDir = Split-Path -Parent $PSScriptRoot
$qtAndroid = Join-Path $QtRoot 'android_arm64_v8a'
$qtHost = Join-Path $QtRoot 'mingw_64'
$ndkBin = Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin'
$qtCmake = Join-Path $qtAndroid 'bin\qt-cmake.bat'
$androidDeployQt = Join-Path $qtHost 'bin\androiddeployqt.exe'
$stripTool = Join-Path $ndkBin 'llvm-strip.exe'
$readelfTool = Join-Path $ndkBin 'llvm-readelf.exe'
if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    $SdkRoot = if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
}
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $sourceDir $BuildDir }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$deploymentDir = Join-Path $BuildDir 'android-build'

function Optimize-AndroidDeployment {
    $nativeDir = Join-Path $deploymentDir 'libs\arm64-v8a'
    $libraries = @(Get-ChildItem -LiteralPath $nativeDir -Filter '*.so' -File)
    if ($libraries.Count -eq 0) { throw "No deployed native libraries found in $nativeDir" }

    # Keep Basic controls; remove unused styles and tooling.
    $unusedPattern = '^lib(?:Qt6QuickControls2(?:Fusion|Imagine|Material|Universal|FluentWinUI3)|qml_QtQuick_Controls_(?:Fusion|Imagine|Material|Universal|FluentWinUI3)_|plugins_qmltooling_|plugins_styles_|Qt6Widgets_)'
    $removed = @($libraries | Where-Object Name -Match $unusedPattern)
    $retained = @($libraries | Where-Object Name -NotMatch $unusedPattern)
    $removedNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($library in $removed) { [void]$removedNames.Add($library.Name) }

    # Check dependencies before deleting deployment copies.
    foreach ($library in $retained) {
        $dynamic = & $readelfTool --dynamic $library.FullName
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect native dependencies: $($library.Name)" }
        foreach ($line in $dynamic) {
            if ($line -match '\(NEEDED\).*\[([^\]]+)\]' -and $removedNames.Contains($Matches[1])) {
                throw "$($library.Name) still needs excluded library $($Matches[1])."
            }
        }
    }
    foreach ($library in $removed) {
        if (-not $library.FullName.StartsWith($deploymentDir + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Library outside deployment directory: $($library.FullName)"
        }
        Remove-Item -LiteralPath $library.FullName -Force
    }

    # Keep QtLoader's library list in sync.
    $libsXmlPath = Join-Path $deploymentDir 'res\values\libs.xml'
    $libsXml = [xml][IO.File]::ReadAllText($libsXmlPath)
    foreach ($item in @($libsXml.SelectNodes('/resources/array/item'))) {
        $name = ($item.InnerText -split ';')[1]
        if ($removedNames.Contains($name) -or $removedNames.Contains("lib$name.so")) {
            [void]$item.ParentNode.RemoveChild($item)
        }
    }
    $xmlSettings = [Xml.XmlWriterSettings]::new()
    $xmlSettings.Encoding = [Text.UTF8Encoding]::new($false)
    $xmlSettings.Indent = $true
    $writer = [Xml.XmlWriter]::Create($libsXmlPath, $xmlSettings)
    try { $libsXml.Save($writer) } finally { $writer.Dispose() }

    foreach ($library in $retained) {
        & $stripTool --strip-unneeded $library.FullName
        if ($LASTEXITCODE -ne 0) { throw "Cannot strip deployment copy: $($library.Name)" }
    }
    Write-Host "Excluded $($removed.Count) unused libraries; stripped $($retained.Count) deployment copies."
}

# Check only the dependencies used by this build.
if ([string]::IsNullOrWhiteSpace($JavaRoot)) { throw 'Pass -JavaRoot or set JAVA_HOME.' }
$requiredPaths = @($qtCmake, $stripTool, (Join-Path $JavaRoot 'bin\javac.exe'),
    (Join-Path $NdkRoot 'build\cmake\android.toolchain.cmake'))
$buildEnvironment = @{
    QT_ROOT = $qtAndroid
    ANDROID_SDK_ROOT = $SdkRoot
    ANDROID_HOME = $SdkRoot
    ANDROID_NDK_ROOT = $NdkRoot
    JAVA_HOME = $JavaRoot
}
if (-not $NativeOnly) {
    # Read newly configured user variables without restarting the terminal.
    if ([string]::IsNullOrWhiteSpace($KeystorePath)) {
        $KeystorePath = [Environment]::GetEnvironmentVariable('BEDROCKMAP_KEYSTORE_PATH', 'User')
    }
    if ([string]::IsNullOrWhiteSpace($KeystoreCredentials)) {
        $KeystoreCredentials = [Environment]::GetEnvironmentVariable('BEDROCKMAP_KEYSTORE_CREDENTIALS', 'User')
    }
    if ([string]::IsNullOrWhiteSpace($KeystorePath) -or [string]::IsNullOrWhiteSpace($KeystoreCredentials)) {
        throw 'Set BEDROCKMAP_KEYSTORE_PATH and BEDROCKMAP_KEYSTORE_CREDENTIALS, or pass their script parameters.'
    }
    $buildEnvironment.BEDROCKMAP_KEYSTORE_PATH = [IO.Path]::GetFullPath($KeystorePath)
    $buildEnvironment.BEDROCKMAP_KEYSTORE_CREDENTIALS = [IO.Path]::GetFullPath($KeystoreCredentials)
    $buildEnvironment.BEDROCKMAP_KEYSTORE_ALIAS = $KeystoreAlias
    $buildTools = Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'build-tools') -Directory |
        Sort-Object { [version]($_.Name.Split('-')[0]) } -Descending | Select-Object -First 1
    if (-not $buildTools) { throw 'Install Android SDK Build-Tools.' }
    $zipalign = Join-Path $buildTools.FullName 'zipalign.exe'
    $apksigner = Join-Path $buildTools.FullName 'apksigner.bat'
    $aapt = Join-Path $buildTools.FullName 'aapt2.exe'
    $requiredPaths += @($androidDeployQt, $readelfTool, $zipalign, $apksigner, $aapt,
        $KeystorePath, $KeystoreCredentials, (Join-Path $SdkRoot "platforms\android-$CompileSdk\android.jar"))

    # Forward the explicit, environment or Windows proxy to Gradle.
    if ([string]::IsNullOrWhiteSpace($ProxyUrl)) { $ProxyUrl = $env:HTTPS_PROXY }
    if ([string]::IsNullOrWhiteSpace($ProxyUrl)) {
        $systemProxy = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' -ErrorAction SilentlyContinue
        if ($systemProxy.ProxyEnable -eq 1 -and $systemProxy.ProxyServer) {
            $servers = $systemProxy.ProxyServer.Split(';')
            $httpsServer = $servers | Where-Object { $_.StartsWith('https=') } | Select-Object -First 1
            if ($httpsServer) { $ProxyUrl = $httpsServer.Substring(6) }
            elseif ($servers.Count -eq 1 -and -not $servers[0].Contains('=')) { $ProxyUrl = $servers[0] }
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($ProxyUrl)) {
        if (-not $ProxyUrl.Contains('://')) { $ProxyUrl = 'http://' + $ProxyUrl }
        $proxyUri = [Uri]$ProxyUrl
        if ($proxyUri.Scheme -ne 'http' -or $proxyUri.UserInfo) { throw 'Use an HTTP proxy without embedded credentials.' }
        $proxyOptions = "-Dhttp.proxyHost=$($proxyUri.Host) -Dhttp.proxyPort=$($proxyUri.Port) -Dhttps.proxyHost=$($proxyUri.Host) -Dhttps.proxyPort=$($proxyUri.Port)"
        $buildEnvironment.JAVA_TOOL_OPTIONS = "$env:JAVA_TOOL_OPTIONS $proxyOptions".Trim()
    }
}
foreach ($requiredPath in $requiredPaths) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) { throw "Build dependency not found: $requiredPath" }
}

# Restore the caller's environment even if a build step fails.
$previousEnvironment = @{}
try {
    foreach ($entry in $buildEnvironment.GetEnumerator()) {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }

    # Compile the arm64 Release library.
    & $qtCmake -S $sourceDir -B $BuildDir -G Ninja `
        "-DQT_HOST_PATH=$qtHost" "-DANDROID_SDK_ROOT=$SdkRoot" "-DANDROID_NDK_ROOT=$NdkRoot" `
        '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-28' '-DCMAKE_BUILD_TYPE=Release' `
        '-DBEDROCKMAP_FULL_DEBUG=OFF' "-DBEDROCKMAP_ANDROID_COMPILE_SDK=$CompileSdk" `
        '-DBEDROCKMAP_ANDROID_COMPRESS_LIBS=ON' '-DQT_ANDROID_DEPLOYMENT_TYPE=Release' `
        '-DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL=ON'
    if ($LASTEXITCODE -ne 0) { throw "Android configure failed ($LASTEXITCODE)." }
    cmake --build $BuildDir --target BedrockMap --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Android build failed ($LASTEXITCODE)." }
    $nativeSo = Join-Path $BuildDir 'libBedrockMap_arm64-v8a.so'
    if ($NativeOnly) {
        & $stripTool --strip-unneeded $nativeSo
        if ($LASTEXITCODE -ne 0) { throw "Native stripping failed ($LASTEXITCODE)." }
        return
    }

    # Deploy Qt, then trim libraries before packaging.
    $deployedNativeDir = Join-Path $deploymentDir 'libs\arm64-v8a'
    [void][IO.Directory]::CreateDirectory($deployedNativeDir)
    Copy-Item -LiteralPath $nativeSo -Destination $deployedNativeDir -Force
    & $androidDeployQt --input (Join-Path $BuildDir 'android-BedrockMap-deployment-settings.json') `
        --output $deploymentDir --android-platform "android-$CompileSdk" --jdk $JavaRoot --release --aux-mode
    if ($LASTEXITCODE -ne 0) { throw "Android deployment failed ($LASTEXITCODE)." }
    Optimize-AndroidDeployment

    # Gradle packages, aligns and signs with the configured key.
    Push-Location -LiteralPath $deploymentDir
    try {
        & .\gradlew.bat --no-daemon "-PbedrockmapNdkPath=$NdkRoot" '-PlegacyPackaging=true' assembleRelease
        if ($LASTEXITCODE -ne 0) { throw "Android package failed ($LASTEXITCODE)." }
    } finally { Pop-Location }
    $signedApk = Join-Path $deploymentDir 'build\outputs\apk\release\android-build-release.apk'
    if (-not (Test-Path -LiteralPath $signedApk -PathType Leaf)) { throw "Signed APK not found: $signedApk" }

    # Verify packaging and signature before publishing the output file.
    $manifestDump = & $aapt dump xmltree $signedApk --file AndroidManifest.xml
    if ($LASTEXITCODE -ne 0 -or -not ($manifestDump -match 'extractNativeLibs.*=true')) {
        throw 'APK must declare extractNativeLibs=true for compressed native libraries.'
    }
    & $zipalign -c -P 16 4 $signedApk
    if ($LASTEXITCODE -ne 0) { throw "APK alignment verification failed ($LASTEXITCODE)." }
    & $apksigner verify $signedApk
    if ($LASTEXITCODE -ne 0) { throw "APK signature verification failed ($LASTEXITCODE)." }
    $apk = Join-Path $deploymentDir 'BedrockMap.apk'
    Copy-Item -LiteralPath $signedApk -Destination $apk -Force
    # Copy the verified APK to dist using the configured application version.
    $deploymentSettings = Get-Content -LiteralPath (Join-Path $BuildDir 'android-BedrockMap-deployment-settings.json') -Raw | ConvertFrom-Json
    $versionName = $deploymentSettings.'android-version-name'
    if ([string]::IsNullOrWhiteSpace($versionName)) { throw 'Android version name is missing.' }
    $distApk = Join-Path $sourceDir "dist\BedrockMap-$versionName-arm64-v8a.apk"
    Copy-Item -LiteralPath $apk -Destination $distApk -Force
    Write-Host "APK: $apk"
    Write-Host "Distribution APK: $distApk"
    Write-Host ('APK size: {0:N2} MiB ({1:N0} bytes)' -f ((Get-Item -LiteralPath $apk).Length / 1MB), (Get-Item -LiteralPath $apk).Length)
} finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
}
