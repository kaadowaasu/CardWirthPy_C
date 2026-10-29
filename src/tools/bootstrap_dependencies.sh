#!/usr/bin/env sh
set -eu

source_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
third_party="$source_root/3rdparty"
mkdir -p "$third_party"

clone_if_missing() {
  name=$1
  tag=$2
  url=$3
  if [ ! -d "$third_party/$name" ]; then
    git clone --depth 1 --branch "$tag" "$url" "$third_party/$name"
  fi
}

clone_if_missing SDL release-3.4.14 https://github.com/libsdl-org/SDL.git
clone_if_missing SDL_image release-3.4.4 https://github.com/libsdl-org/SDL_image.git
clone_if_missing SDL_mixer release-3.2.4 https://github.com/libsdl-org/SDL_mixer.git
clone_if_missing SDL_ttf release-3.2.2 https://github.com/libsdl-org/SDL_ttf.git

git -C "$third_party/SDL_image" submodule update --init --depth 1 external/libwebp
git -C "$third_party/SDL_ttf" submodule update --init --depth 1 external/freetype

cmake -P "$source_root/tools/apply_sdl_patches.cmake"
printf 'SDL dependencies are ready in %s\n' "$third_party"
