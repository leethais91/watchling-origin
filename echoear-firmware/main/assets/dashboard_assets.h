/*
 * Generated dashboard assets.
 *
 * Fonts: Montserrat (SIL OFL 1.1, see Montserrat-OFL.txt), converted with
 *   npx lv_font_conv@1.5.3 --no-compress --bpp 4 --format lvgl --lv-include lvgl.h
 *   bold 30: --symbols '0123456789:$.,-'   (clock, cost)
 *   bold 20: --symbols '0123456789%-'      (usage percentages)
 *   semibold 14: -r 0x20-0x7E               (provider names)
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
LV_IMAGE_DECLARE(img_robot_head);
