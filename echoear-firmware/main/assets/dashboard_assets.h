/*
 * Generated dashboard assets.
 *
 * Fonts: Montserrat (SIL OFL 1.1, see Montserrat-OFL.txt), converted with
 *   npx lv_font_conv@1.5.3 --no-compress --bpp 4 --format lvgl --lv-include lvgl.h
 *   bold 30: --symbols '0123456789:$.,-'   (clock, cost)
 *   bold 20: --symbols '0123456789%-'      (usage percentages)
 *   semibold 14: -r 0x20-0x7E               (provider names)
 * Font: JetBrains Mono Medium (SIL OFL 1.1, see JetBrainsMono-OFL.txt; the variable font
 *   instanced at wght=500), 15 px, ASCII + Vietnamese + a few typographic marks, for agent text:
 *   -r 0x20-0x7E,0xA0-0xFF,0x102-0x103,0x110-0x111,0x128-0x129,0x168-0x169,0x1A0-0x1A1,
 *      0x1AF-0x1B0,0x1EA0-0x1EF9,0x2013-0x2014,0x2018-0x201D,0x2022,0x2026,0x2192
 * Image: robot_head.png (164x158, alpha; head with an empty visor, generated with Codex image
 *   generation, prompt in plans/261017-2138-animated-robot-face/robot-head-prompt.md). The visor
 *   interior is blurred so its faint noise does not band into blotches in RGB565:
 *     magick robot-head-source.png -trim +repage -resize 164x -strip base.png
 *     magick base.png -background white -alpha remove -colorspace gray -threshold 22% -negate \
 *         -morphology Erode Disk:4 -blur 0x1.5 mask.png
 *     magick base.png \( base.png -alpha off -blur 0x8 \) mask.png -compose over -composite rgb.png
 *     magick rgb.png base.png -compose CopyOpacity -composite robot_head.png
 *   then lvgl/scripts/LVGLImage.py --ofmt C --cf RGB565A8 --name img_robot_head
 */
#pragma once

#include "lvgl.h"

LV_FONT_DECLARE(font_dash_bold_30);
LV_FONT_DECLARE(font_dash_bold_20);
LV_FONT_DECLARE(font_dash_semibold_14);
LV_FONT_DECLARE(font_mono_15);
LV_IMAGE_DECLARE(img_robot_head);
