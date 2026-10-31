param(
    [string]$QtRoot = 'C:\noinstall\Qt\6.11.2',
    [string]$SdkRoot = '',
    [string]$NdkRoot = 'C:\noinstall\android-ndk-r30',
    [string]$JavaRoot = $env:JAVA_HOME,
    [string]$BuildDir = '',
    [string]$CompileSdk = '37.0',
    [string]$ProxyUrl = '',
    [ValidateRange(1, 256)]
    [int]$Jobs = 8,
    [string]$KeystorePath = (Join-Path $env:USERPROFILE '.bedrockmap-signing\bedrockmap-release.p12'),
    [string]$KeystoreAlias = 'bedrockmap',
    [string]$KeystoreCredentials = (Join-Path $env:USERPROFILE '.bedrockmap-signing\credentials.txt'),
    [switch]$NativeOnly
)

$ErrorActionPreference = 'Stop'
$sourceDir = Split-Path -Parent $PSScriptRoot
$qtAndroid = Join-Path $QtRoot 'android_arm64_v8a'
$qtHost = Join-Path $QtRoot 'mingw_64'
if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    $SdkRoot = if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = 'build_android_rls'
}
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $sourceDir $BuildDir }
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)
# Keep the normal Release compiler optimizations; size trimming happens on
# packaged libraries and resources, without LTO or Java code shrinking.
$buildType = 'Release'

function Optimize-AndroidDeployment([string]$DeploymentDir, [string]$StripTool, [string]$ReadelfTool) {
    $deploymentRoot = [IO.Path]::GetFullPath($DeploymentDir)
    $nativeDir = Join-Path $deploymentRoot 'libs\arm64-v8a'
    $libraries = @(Get-ChildItem -LiteralPath $nativeDir -Filter '*.so' -File)
    if ($libraries.Count -eq 0) { throw "No deployed native libraries found in $nativeDir" }
    # The application fixes Controls to Basic. Optional style imports still
    # cause Qt's scanner to deploy other styles, even in a release build.
    $unusedPattern = '^lib(?:Qt6QuickControls2(?:Fusion|Imagine|Material|Universal|FluentWinUI3)|qml_QtQuick_Controls_(?:Fusion|Imagine|Material|Universal|FluentWinUI3)_|plugins_qmltooling_|plugins_styles_|Qt6Widgets_)'
    $removed = @($libraries | Where-Object Name -Match $unusedPattern)
    $retained = @($libraries | Where-Object Name -NotMatch $unusedPattern)
    $removedNames = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($library in $removed) { [void]$removedNames.Add($library.Name) }

    # Refuse a deployment change if a surviving ELF still links a removed lib.
    foreach ($library in $retained) {
        $dynamic = & $ReadelfTool --dynamic $library.FullName
        if ($LASTEXITCODE -ne 0) { throw "Cannot inspect native dependencies: $($library.Name)" }
        foreach ($line in $dynamic) {
            if ($line -match '\(NEEDED\).*\[([^\]]+)\]' -and $removedNames.Contains($Matches[1])) {
                throw "$($library.Name) still needs excluded library $($Matches[1])."
            }
        }
    }
    foreach ($library in $removed) {
        # Delete only individual deployment copies, never the Qt/NDK originals.
        if (-not $library.FullName.StartsWith($deploymentRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Library outside deployment directory: $($library.FullName)"
        }
        Remove-Item -LiteralPath $library.FullName -Force
    }

    # QtLoader must not try to load libraries removed from the staging folder.
    $libsXmlPath = Join-Path $deploymentRoot 'res\values\libs.xml'
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

    # In particular, the NDK's libc++_shared.so carries several MiB of DWARF.
    foreach ($library in $retained) {
        & $StripTool --strip-unneeded $library.FullName
        if ($LASTEXITCODE -ne 0) { throw "Cannot strip deployment copy: $($library.Name)" }
    }
    Write-Host "Excluded $($removed.Count) unused native libraries; stripped $($retained.Count) deployment copies."
}

function Remove-StaleApplicationLibraries([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return 0 }
    $rootFull = [IO.Path]::GetFullPath($Root)
    $removed = 0
    foreach ($library in @(Get-ChildItem -LiteralPath $rootFull -Filter 'libBedrockMapQml_*.so' -File)) {
        if (-not $library.FullName.StartsWith($rootFull + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Stale application library outside expected directory: $($library.FullName)"
        }
        Remove-Item -LiteralPath $library.FullName -Force
        ++$removed
    }
    return $removed
}

$requiredPaths = @(
    (Join-Path $qtAndroid 'bin\qt-cmake.bat'),
    (Join-Path $qtHost 'bin\androiddeployqt.exe'),
    (Join-Path $NdkRoot 'build\cmake\android.toolchain.cmake'),
    (Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'),
    (Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe'),
    (Join-Path $SdkRoot "platforms\android-$CompileSdk\android.jar"),
    (Join-Path $SdkRoot 'platform-tools\adb.exe')
)
if ([string]::IsNullOrWhiteSpace($JavaRoot)) { throw 'Pass -JavaRoot or set JAVA_HOME to a JDK installation.' }
$requiredPaths += Join-Path $JavaRoot 'bin\javac.exe'
foreach ($requiredPath in $requiredPaths) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) { throw "Required build dependency not found: $requiredPath" }
}
if (-not (Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'build-tools') -Directory -ErrorAction SilentlyContinue)) {
    throw 'Install Android SDK Build-Tools using Android Studio SDK Manager.'
}

# Limit environment changes to this invocation; desktop builds keep their kit.
$previousEnvironment = @{}
$buildEnvironment = @{
    QT_ROOT = $qtAndroid
    ANDROID_SDK_ROOT = $SdkRoot
    ANDROID_HOME = $SdkRoot
    ANDROID_NDK_ROOT = $NdkRoot
    JAVA_HOME = $JavaRoot
}
if (-not $NativeOnly) {
    if ([string]::IsNullOrWhiteSpace($ProxyUrl)) {
        $ProxyUrl = $env:HTTPS_PROXY
        if ([string]::IsNullOrWhiteSpace($ProxyUrl)) {
            $systemProxy = Get-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Internet Settings' -ErrorAction SilentlyContinue
            if ($systemProxy.ProxyEnable -eq 1 -and $systemProxy.ProxyServer) {
                $servers = $systemProxy.ProxyServer.Split(';')
                $httpsServer = $servers | Where-Object { $_.StartsWith('https=') } | Select-Object -First 1
                if ($httpsServer) { $ProxyUrl = $httpsServer.Substring(6) }
                elseif ($servers.Count -eq 1 -and -not $servers[0].Contains('=')) { $ProxyUrl = $servers[0] }
            }
        }
    }
    if (-not [string]::IsNullOrWhiteSpace($ProxyUrl)) {
        if (-not $ProxyUrl.Contains('://')) { $ProxyUrl = 'http://' + $ProxyUrl }
        $proxyUri = [Uri]$ProxyUrl
        if ($proxyUri.Scheme -ne 'http' -or $proxyUri.UserInfo) { throw 'Use an HTTP proxy without embedded credentials for Java downloads.' }
        $proxyOptions = "-Dhttp.proxyHost=$($proxyUri.Host) -Dhttp.proxyPort=$($proxyUri.Port) -Dhttps.proxyHost=$($proxyUri.Host) -Dhttps.proxyPort=$($proxyUri.Port)"
        $buildEnvironment.JAVA_TOOL_OPTIONS = "$env:JAVA_TOOL_OPTIONS $proxyOptions".Trim()
    }
}
try {
    foreach ($entry in $buildEnvironment.GetEnumerator()) {
        $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
    & (Join-Path $qtAndroid 'bin\qt-cmake.bat') -S $sourceDir -B $BuildDir -G Ninja `
        "-DQT_HOST_PATH=$qtHost" "-DANDROID_SDK_ROOT=$SdkRoot" "-DANDROID_NDK_ROOT=$NdkRoot" `
        '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-28' "-DCMAKE_BUILD_TYPE=$buildType" `
        '-DBEDROCKMAP_FULL_DEBUG=OFF' "-DBEDROCKMAP_ANDROID_COMPILE_SDK=$CompileSdk" `
        '-DBEDROCKMAP_ANDROID_COMPRESS_LIBS=ON' '-DQT_ANDROID_DEPLOYMENT_TYPE=Release' `
        '-DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL=ON'
    if ($LASTEXITCODE -ne 0) { throw "Android configure failed ($LASTEXITCODE)." }

    cmake --build $BuildDir --target BedrockMap --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Android build failed ($LASTEXITCODE)." }

    # Strip the application before deployment; dependencies are stripped on
    # their deployment copies below, without changing the installed SDKs.
    $nativeSo = Join-Path $BuildDir 'libBedrockMap_arm64-v8a.so'
    if (-not (Test-Path -LiteralPath $nativeSo -PathType Leaf)) { throw "Native library not found: $nativeSo" }
    $needsClean = (Remove-StaleApplicationLibraries $BuildDir) -gt 0
    $stripTool = Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'
    & $stripTool --strip-unneeded $nativeSo
    if ($LASTEXITCODE -ne 0) { throw "Stripping native debug symbols failed ($LASTEXITCODE)." }

    if (-not $NativeOnly) {
        # Separate Qt deployment from Gradle so optional plugins can be removed
        # together with their loader metadata before the APK is assembled.
        $deploymentDir = Join-Path $BuildDir 'android-build'
        $deploymentSettings = Join-Path $BuildDir 'android-BedrockMap-deployment-settings.json'
        $deployedNativeDir = Join-Path $deploymentDir 'libs\arm64-v8a'
        [void][IO.Directory]::CreateDirectory($deployedNativeDir)
        if ((Remove-StaleApplicationLibraries $deployedNativeDir) -gt 0) { $needsClean = $true }
        Copy-Item -LiteralPath $nativeSo -Destination (Join-Path $deployedNativeDir ([IO.Path]::GetFileName($nativeSo))) -Force
        & (Join-Path $qtHost 'bin\androiddeployqt.exe') --input $deploymentSettings --output $deploymentDir `
            --android-platform "android-$CompileSdk" --jdk $JavaRoot --release --aux-mode
        if ($LASTEXITCODE -ne 0) { throw "Android deployment failed ($LASTEXITCODE)." }
        $readelfTool = Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-readelf.exe'
        Optimize-AndroidDeployment $deploymentDir $stripTool $readelfTool
        Push-Location -LiteralPath $deploymentDir
        try {
            # Auxiliary deployment may retain gradle.properties from an older
            # build. Compressed libraries require extractNativeLibs=true.
            # Preserve the array for a single task; splatting a scalar string
            # would pass its characters as separate Gradle task arguments.
            $gradleTargets = @(
                if ($needsClean) { 'clean' }
                'assembleRelease'
            )
            & (Join-Path $deploymentDir 'gradlew.bat') --no-daemon "-PbedrockmapNdkPath=$NdkRoot" '-PlegacyPackaging=true' @gradleTargets
            if ($LASTEXITCODE -ne 0) { throw "Android package failed ($LASTEXITCODE)." }
        } finally { Pop-Location }
        $unsignedApk = Join-Path $deploymentDir 'build\outputs\apk\release\android-build-release-unsigned.apk'
        if (-not (Test-Path -LiteralPath $unsignedApk -PathType Leaf)) { throw "Unsigned APK not found: $unsignedApk" }
        $apk = Join-Path $deploymentDir 'BedrockMap.apk'

        # androiddeployqt --release leaves the package unsigned. Sign it with
        # the configured release keystore; credentials stay outside the repo.
        $buildTools = Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'build-tools') -Directory |
            Sort-Object Name -Descending | Select-Object -First 1
        if (-not $buildTools) { throw 'Android SDK Build-Tools directory not found.' }
        $zipalign = Join-Path $buildTools.FullName 'zipalign.exe'
        $apksigner = Join-Path $buildTools.FullName 'apksigner.bat'
        $aapt = Join-Path $buildTools.FullName 'aapt2.exe'
        $keystore = $KeystorePath
        foreach ($tool in @($zipalign, $apksigner, $aapt, $keystore, $KeystoreCredentials)) {
            if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Signing dependency not found: $tool" }
        }
        $manifestDump = & $aapt dump xmltree $unsignedApk --file AndroidManifest.xml
        if ($LASTEXITCODE -ne 0 -or -not ($manifestDump -match 'extractNativeLibs.*=true')) {
            throw 'APK must declare extractNativeLibs=true for compressed native libraries.'
        }
        $credentialValues = ConvertFrom-StringData (Get-Content -LiteralPath $KeystoreCredentials -Raw)
        $storePassword = $credentialValues.storePassword
        $keyPassword = $credentialValues.keyPassword
        if ([string]::IsNullOrWhiteSpace($storePassword) -or [string]::IsNullOrWhiteSpace($keyPassword)) {
            throw "Keystore credentials are incomplete: $KeystoreCredentials"
        }
        foreach ($entry in @{
            BEDROCKMAP_KEYSTORE_STORE_PASS = $storePassword
            BEDROCKMAP_KEYSTORE_KEY_PASS = $keyPassword
        }.GetEnumerator()) {
            $previousEnvironment[$entry.Key] = [Environment]::GetEnvironmentVariable($entry.Key, 'Process')
            [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
        }
        $aligned = Join-Path $BuildDir 'android-build\BedrockMap-aligned.tmp.apk'
        $signed = Join-Path $BuildDir 'android-build\BedrockMap-signed.tmp.apk'
        & $zipalign -f -P 16 4 $unsignedApk $aligned
        if ($LASTEXITCODE -ne 0) { throw "APK alignment failed ($LASTEXITCODE)." }
        & $apksigner sign --ks $keystore --ks-key-alias $KeystoreAlias `
            --ks-pass 'env:BEDROCKMAP_KEYSTORE_STORE_PASS' `
            --key-pass 'env:BEDROCKMAP_KEYSTORE_KEY_PASS' --out $signed $aligned
        if ($LASTEXITCODE -ne 0) { throw "APK signing failed ($LASTEXITCODE)." }
        & $apksigner verify $signed
        if ($LASTEXITCODE -ne 0) { throw "APK signature verification failed ($LASTEXITCODE)." }
        Copy-Item -LiteralPath $signed -Destination $apk -Force
        Remove-Item -LiteralPath $signed -Force
        Remove-Item -LiteralPath $aligned -Force
        Write-Host "APK: $apk"
        Write-Host ('APK size: {0:N2} MiB ({1:N0} bytes)' -f ((Get-Item -LiteralPath $apk).Length / 1MB), (Get-Item -LiteralPath $apk).Length)
    }
} finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
}
