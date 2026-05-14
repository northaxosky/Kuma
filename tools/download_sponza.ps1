# Download Khronos's standard Sponza glTF (Sponza.gltf + Sponza.bin)
# into assets/source/scenes/Sponza/ so the sandbox's scene bake has
# input. Skips the .jpg textures (~50 MB) since the engine doesn't
# wire materials yet - those will be added when the Materials module
# ships. Total download: ~10 MB.
#
# Usage:
#   pwsh tools\download_sponza.ps1
#
# Run once per checkout. The downloaded files are gitignored.

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$destDir  = Join-Path $repoRoot "assets\source\scenes\Sponza"
$base     = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/Sponza/glTF/"

New-Item -ItemType Directory -Path $destDir -Force | Out-Null

foreach ($file in @("Sponza.gltf", "Sponza.bin")) {
    $url  = "$base$file"
    $out  = Join-Path $destDir $file
    if (Test-Path $out) {
        Write-Host "exists, skipping: $file"
        continue
    }
    Write-Host "downloading $file ..."
    Invoke-WebRequest -Uri $url -OutFile $out
}

$gltf = Join-Path $destDir "Sponza.gltf"
$bin  = Join-Path $destDir "Sponza.bin"
$gltfLen = (Get-Item $gltf).Length
$binLen  = (Get-Item $bin).Length
Write-Host ""
Write-Host "Sponza ready:"
Write-Host "  $gltf ($gltfLen bytes)"
Write-Host "  $bin ($binLen bytes)"
Write-Host ""
Write-Host "Re-run cmake --build to bake into build/assets/scenes/Sponza.kscene."
