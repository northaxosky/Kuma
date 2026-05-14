# Download Khronos's standard Sponza glTF (Sponza.gltf + Sponza.bin
# + every referenced .jpg/.png texture) into
# assets/source/scenes/Sponza/ so the sandbox's scene bake has full
# material data. Total download: ~50-60 MB.
#
# Usage:
#   pwsh tools\download_sponza.ps1
#
# Run once per checkout. The downloaded files are gitignored.
# Re-running is safe - existing files are skipped.

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$destDir  = Join-Path $repoRoot "assets\source\scenes\Sponza"
$base     = "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/Sponza/glTF/"

New-Item -ItemType Directory -Path $destDir -Force | Out-Null

# Geometry: the .gltf scene description and the .bin buffer it
# references for vertex/index data.
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

# Textures: the .gltf references a fixed set of .jpg / .png files
# by hash-named URI. List them out explicitly so this script doesn't
# need to parse the .gltf at runtime - if Khronos updates Sponza
# with different filenames, we update this list in lockstep.
$textures = @(
    "5061699253647017043.png",
    "7268504077753552595.jpg",
    "white.png",
    "8006627369776289000.png",
    "8750083169368950601.jpg",
    "5792855332885324923.jpg",
    "14650633544276105767.jpg",
    "15295713303328085182.jpg",
    "6047387724914829168.jpg",
    "5823059166183034438.jpg",
    "7441062115984513793.jpg",
    "11490520546946913238.jpg",
    "6151467286084645207.jpg",
    "4975155472559461469.jpg",
    "4675343432951571524.jpg",
    "2775690330959970771.jpg",
    "2185409758123873465.jpg",
    "17876391417123941155.jpg",
    "11474523244911310074.jpg",
    "9288698199695299068.jpg",
    "16275776544635328252.png",
    "755318871556304029.jpg",
    "8481240838833932244.jpg",
    "6772804448157695701.jpg",
    "2969916736137545357.jpg"
)
foreach ($tex in $textures) {
    $url = "$base$tex"
    $out = Join-Path $destDir $tex
    if (Test-Path $out) {
        Write-Host "exists, skipping: $tex"
        continue
    }
    Write-Host "downloading $tex ..."
    Invoke-WebRequest -Uri $url -OutFile $out
}

$gltf = Join-Path $destDir "Sponza.gltf"
$bin  = Join-Path $destDir "Sponza.bin"
$gltfLen = (Get-Item $gltf).Length
$binLen  = (Get-Item $bin).Length
$texCount = (Get-ChildItem $destDir -Filter "*.jpg").Count + (Get-ChildItem $destDir -Filter "*.png").Count
Write-Host ""
Write-Host "Sponza ready:"
Write-Host "  $gltf ($gltfLen bytes)"
Write-Host "  $bin ($binLen bytes)"
Write-Host "  $texCount texture(s)"
Write-Host ""
Write-Host "Re-run cmake --build to bake into build/assets/scenes/Sponza.kscene."
