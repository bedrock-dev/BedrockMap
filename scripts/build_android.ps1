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
# Android packages are Release builds. Only debug symbols are stripped from
# the app module; no Qt libraries or plugins are removed.
$buildType = 'Release'

$requiredPaths = @(
    (Join-Path $qtAndroid 'bin\qt-cmake.bat'),
    (Join-Path $qtHost 'bin\androiddeployqt.exe'),
    (Join-Path $NdkRoot 'build\cmake\android.toolchain.cmake'),
    (Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'),
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
        '-DBEDROCKMAP_ANDROID_COMPRESS_LIBS=OFF' '-DQT_NO_GLOBAL_APK_TARGET_PART_OF_ALL=ON'
    if ($LASTEXITCODE -ne 0) { throw "Android configure failed ($LASTEXITCODE)." }

    cmake --build $BuildDir --target BedrockMapQml --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw "Android build failed ($LASTEXITCODE)." }

    # Strip only debug symbols from our own module. Qt's deployment libraries
    # are already stripped; this does not remove any library or plugin.
    $nativeSo = Join-Path $BuildDir 'libBedrockMapQml_arm64-v8a.so'
    if (-not (Test-Path -LiteralPath $nativeSo -PathType Leaf)) { throw "Native library not found: $nativeSo" }
    $stripTool = Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'
    & $stripTool --strip-unneeded $nativeSo
    if ($LASTEXITCODE -ne 0) { throw "Stripping native debug symbols failed ($LASTEXITCODE)." }

    if (-not $NativeOnly) {
        cmake --build $BuildDir --target BedrockMapQml_make_apk --parallel $Jobs
        if ($LASTEXITCODE -ne 0) { throw "Android package failed ($LASTEXITCODE)." }
        $apk = Join-Path $BuildDir 'android-build\BedrockMapQml.apk'
        if (-not (Test-Path -LiteralPath $apk -PathType Leaf)) { throw "APK not found after packaging: $apk" }

        # androiddeployqt --release leaves the package unsigned. Sign it with
        # the configured release keystore; credentials stay outside the repo.
        $buildTools = Get-ChildItem -LiteralPath (Join-Path $SdkRoot 'build-tools') -Directory |
            Sort-Object Name -Descending | Select-Object -First 1
        if (-not $buildTools) { throw 'Android SDK Build-Tools directory not found.' }
        $zipalign = Join-Path $buildTools.FullName 'zipalign.exe'
        $apksigner = Join-Path $buildTools.FullName 'apksigner.bat'
        $keystore = $KeystorePath
        foreach ($tool in @($zipalign, $apksigner, $keystore, $KeystoreCredentials)) {
            if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Signing dependency not found: $tool" }
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
        $aligned = Join-Path $BuildDir 'android-build\BedrockMapQml-aligned.tmp.apk'
        $signed = Join-Path $BuildDir 'android-build\BedrockMapQml-signed.tmp.apk'
        & $zipalign -f -p 4 $apk $aligned
        if ($LASTEXITCODE -ne 0) { throw "APK alignment failed ($LASTEXITCODE)." }
        & $apksigner sign --ks $keystore --ks-key-alias $KeystoreAlias `
            --ks-pass 'env:BEDROCKMAP_KEYSTORE_STORE_PASS' `
            --key-pass 'env:BEDROCKMAP_KEYSTORE_KEY_PASS' --out $signed $aligned
        if ($LASTEXITCODE -ne 0) { throw "APK signing failed ($LASTEXITCODE)." }
        & $apksigner verify $signed
        if ($LASTEXITCODE -ne 0) { throw "APK signature verification failed ($LASTEXITCODE)." }
        Move-Item -LiteralPath $signed -Destination $apk -Force
        Remove-Item -LiteralPath $aligned -Force
        Write-Host "APK: $apk"
    }
} finally {
    foreach ($entry in $previousEnvironment.GetEnumerator()) {
        [Environment]::SetEnvironmentVariable($entry.Key, $entry.Value, 'Process')
    }
}
