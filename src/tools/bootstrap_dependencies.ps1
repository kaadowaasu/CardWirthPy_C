$ErrorActionPreference = "Stop"

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
$thirdParty = Join-Path $sourceRoot "3rdparty"
New-Item -ItemType Directory -Path $thirdParty -Force | Out-Null

$dependencies = @(
    @{ Name = "SDL";       Tag = "release-3.4.14"; Url = "https://github.com/libsdl-org/SDL.git" },
    @{ Name = "SDL_image"; Tag = "release-3.4.4";  Url = "https://github.com/libsdl-org/SDL_image.git" },
    @{ Name = "SDL_mixer"; Tag = "release-3.2.4";  Url = "https://github.com/libsdl-org/SDL_mixer.git" },
    @{ Name = "SDL_ttf";   Tag = "release-3.2.2";  Url = "https://github.com/libsdl-org/SDL_ttf.git" }
)

foreach ($dependency in $dependencies) {
    $destination = Join-Path $thirdParty $dependency.Name
    if (-not (Test-Path -LiteralPath $destination)) {
        & git clone --depth 1 --branch $dependency.Tag $dependency.Url $destination
        if ($LASTEXITCODE -ne 0) { throw "git clone failed: $($dependency.Name)" }
    }
}

& git -C (Join-Path $thirdParty "SDL_image") submodule update --init --depth 1 external/libwebp
if ($LASTEXITCODE -ne 0) { throw "libwebp submodule setup failed" }
& git -C (Join-Path $thirdParty "SDL_ttf") submodule update --init --depth 1 external/freetype
if ($LASTEXITCODE -ne 0) { throw "freetype submodule setup failed" }

& cmake -P (Join-Path $PSScriptRoot "apply_sdl_patches.cmake")
if ($LASTEXITCODE -ne 0) { throw "SDL platform patch setup failed" }
Write-Output "SDL dependencies are ready in $thirdParty"
