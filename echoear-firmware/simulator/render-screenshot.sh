#!/usr/bin/env bash
# Builds the dashboard simulator and renders a 360x360 PNG with the round panel mask
# applied (corners outside the glass shown black).
#   ./render-screenshot.sh out.png [--stress | --unknown | --confirm] [--mood NAME] [--after-ms N]
# Requires: cmake, pkg-config, SDL2, ImageMagick (`magick`).
set -euo pipefail

out=${1:?usage: render-screenshot.sh out.png [--stress | --unknown | --confirm] [--mood NAME] [--after-ms N]}
shift
here=$(cd "$(dirname "$0")" && pwd)
bin_dir="$here/cmake-out"

cmake -S "$here" -B "$bin_dir" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$bin_dir" -j >/dev/null

ppm=$(mktemp -t dashboard).ppm
trap 'rm -f "$ppm"' EXIT
"$bin_dir/dashboard_sim" --shot "$ppm" "$@"
magick "$ppm" \( -size 360x360 xc:black -fill white -draw "circle 179.5,179.5 179.5,0" \) \
    -alpha off -compose CopyOpacity -composite -background black -alpha remove "$out"
echo "wrote $out"
