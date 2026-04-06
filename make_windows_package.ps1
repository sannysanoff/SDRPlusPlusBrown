$build_dir = $args[0]
$root_dir = $args[1]

if ([string]::IsNullOrWhiteSpace($build_dir) -or [string]::IsNullOrWhiteSpace($root_dir)) {
    throw "Usage: .\\make_windows_package.ps1 <build_dir> <root_dir>"
}

$ErrorActionPreference = "Stop"
$RELDIR = "RelWithDebInfo"
$packageDir = Join-Path (Get-Location) "sdrpp_windows_x64"
$zipPath = Join-Path (Get-Location) "sdrpp_windows_x64.zip"

function Ensure-Directory($path) {
    if (!(Test-Path $path)) {
        New-Item -ItemType Directory -Path $path | Out-Null
    }
}

function Copy-IfExists($source, $destinationDir) {
    if (Test-Path $source) {
        Copy-Item $source $destinationDir -Force
        return $true
    }
    return $false
}

function Copy-ModuleCategory($category) {
    $categoryDir = Join-Path $build_dir $category
    if (!(Test-Path $categoryDir)) {
        return
    }

    Get-ChildItem $categoryDir -Directory | ForEach-Object {
        $configDir = Join-Path $_.FullName $RELDIR
        if (!(Test-Path $configDir)) {
            return
        }

        $moduleDll = Join-Path $configDir ($_.Name + ".dll")
        if (Test-Path $moduleDll) {
            Copy-Item $moduleDll (Join-Path $packageDir "modules") -Force
        }

        Get-ChildItem $configDir -Filter *.dll | ForEach-Object {
            if ($_.Name -ne ($_.Directory.Parent.Name + ".dll")) {
                Copy-Item $_.FullName $packageDir -Force
            }
        }
    }
}

if (Test-Path $packageDir) {
    Remove-Item $packageDir -Recurse -Force
}
if (Test-Path $zipPath) {
    Remove-Item $zipPath -Force
}

Ensure-Directory $packageDir
Ensure-Directory (Join-Path $packageDir "modules")

Copy-Item (Join-Path $root_dir "*") $packageDir -Recurse -Force
Copy-Item (Join-Path (Join-Path $build_dir $RELDIR) "*") $packageDir -Include *.exe,*.dll -Force
Copy-IfExists (Join-Path (Join-Path $build_dir "core") (Join-Path $RELDIR "sdrpp_core.dll")) $packageDir | Out-Null

Copy-ModuleCategory "source_modules"
Copy-ModuleCategory "sink_modules"
Copy-ModuleCategory "decoder_modules"
Copy-ModuleCategory "misc_modules"

$dependencyCandidates = @(
    "C:\vcpkg\installed\x64-windows\bin\itpp.dll",
    "C:\vcpkg\installed\x64-windows\bin\rtaudio.dll",
    "C:\vcpkg\installed\x64-windows\bin\portaudio.dll",
    "C:\vcpkg\installed\x64-windows\bin\zstd.dll",
    "C:\Program Files\PothosSDR\bin\airspy.dll",
    "C:\Program Files\PothosSDR\bin\airspyhf.dll",
    "C:\Program Files\PothosSDR\bin\bladeRF.dll",
    "C:\Program Files\PothosSDR\bin\hackrf.dll",
    "C:\Program Files\PothosSDR\bin\LimeSuite.dll",
    "C:\Program Files\PothosSDR\bin\libiio.dll",
    "C:\Program Files\PothosSDR\bin\libad9361.dll",
    "C:\Program Files\PothosSDR\bin\libusb-1.0.dll",
    "C:\Program Files\PothosSDR\bin\pthreadVC2.dll",
    "C:\Program Files\PothosSDR\bin\rtlsdr.dll",
    "C:\Program Files\PothosSDR\bin\sdrplay_api.dll",
    "C:\Program Files\codec2\lib\libcodec2.dll",
    "C:\Program Files\SDRplay\API\x64\sdrplay_api.dll",
    "C:\Program Files\RFNM\bin\rfnm.dll",
    "C:\Program Files\RFNM\bin\spdlog.dll",
    "C:\Program Files\RFNM\bin\fmt.dll",
    "C:\Program Files (x86)\hydrasdr_all\bin\hydrasdr.dll"
)

$dependencyCandidates | ForEach-Object {
    Copy-IfExists $_ $packageDir | Out-Null
}

Compress-Archive -Path $packageDir -DestinationPath $zipPath
Write-Host "Created package: $zipPath"
