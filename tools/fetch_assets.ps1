<#
    fetch_assets.ps1
    ------------------------------------------------------------------
    Downloads the free (CC0) third-party asset packs used by this game
    and copies the handful of files the game actually loads into
    ./assets. Run it from the repository root:

        powershell -ExecutionPolicy Bypass -File tools\fetch_assets.ps1

    All packs are Creative Commons Zero (CC0) - public domain.
    See assets/CREDITS.md for the full attribution list.
#>

[CmdletBinding()]
param(
    [switch]$Force   # re-download even if the archives are already cached
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root    = Split-Path -Parent $PSScriptRoot
$cache   = Join-Path $root 'assets\_download'
$extract = Join-Path $cache 'extracted'

# ---------------------------------------------------------------------------
# Pack definitions: name -> download URL
# ---------------------------------------------------------------------------
$packs = [ordered]@{
    'pixel-shmup'    = 'https://kenney.nl/media/pages/assets/pixel-shmup/640246b9cc-1677495782/kenney_pixel-shmup.zip'
    'sci-fi-sounds'  = 'https://kenney.nl/media/pages/assets/sci-fi-sounds/6b296f9ecf-1677589334/kenney_sci-fi-sounds.zip'
}

New-Item -ItemType Directory -Force -Path $cache, $extract | Out-Null

foreach ($name in $packs.Keys) {
    $zip = Join-Path $cache "$name.zip"
    if ((Test-Path $zip) -and -not $Force) {
        Write-Host "cached  $name.zip"
    } else {
        Write-Host "fetch   $name.zip"
        Invoke-WebRequest -Uri $packs[$name] -OutFile $zip -UserAgent 'Mozilla/5.0'
    }

    $dest = Join-Path $extract $name
    if (-not (Test-Path $dest)) {
        Write-Host "extract $name"
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [System.IO.Compression.ZipFile]::ExtractToDirectory($zip, $dest)
    }
}

# ---------------------------------------------------------------------------
# Curate: copy only what the game loads into ./assets
# ---------------------------------------------------------------------------
function Copy-Asset {
    param([string]$From, [string]$To)
    $dir = Split-Path -Parent $To
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    Copy-Item -Force -LiteralPath $From -Destination $To
    Write-Host ("  {0}  ->  {1}" -f (Split-Path $From -Leaf), $To.Replace($root, '.'))
}

$shmup  = Join-Path $extract 'pixel-shmup'
$snd    = Join-Path $extract 'sci-fi-sounds\Audio'
$a      = Join-Path $root 'assets'

# The UI font (Departure Mono, SIL OFL) is committed directly under
# assets/fonts/ and is not part of the download step.

Write-Host "`ncurating sprites"
Copy-Asset "$shmup\Tilemap\ships_packed.png" "$a\sprites\ships.png"   # 4x6 grid of 32x32 ships
Copy-Asset "$shmup\Tilemap\tiles_packed.png" "$a\sprites\tiles.png"   # 12x10 grid of 16x16 tiles

Write-Host "curating audio"
$sounds = [ordered]@{
    'shoot.ogg'         = 'laserSmall_000.ogg'
    'shoot_heavy.ogg'   = 'laserLarge_002.ogg'
    'enemy_shoot.ogg'   = 'laserRetro_003.ogg'
    'explosion.ogg'     = 'explosionCrunch_001.ogg'
    'explosion_big.ogg' = 'lowFrequency_explosion_000.ogg'
    'hit.ogg'           = 'impactMetal_002.ogg'
    'pickup.ogg'        = 'forceField_000.ogg'
    'shield.ogg'        = 'forceField_003.ogg'
    'engine.ogg'        = 'spaceEngineSmall_000.ogg'
}
foreach ($out in $sounds.Keys) {
    Copy-Asset "$snd\$($sounds[$out])" "$a\audio\$out"
}

Write-Host "`nDone. Assets are in $a"
