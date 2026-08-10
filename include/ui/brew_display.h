#pragma once

#include <Print.h>

namespace ui {

/** Draw the first BrewSphere fermentation gauge view. */
void brewDisplayDraw();

/** Redraw the gauge after new Brewfather data arrives. */
void brewDisplayRefresh();

/** Periodic display hook reserved for animations and night mode. */
void brewDisplayTick();

/** Stream the current 240x240 BrewSphere frame as a 24-bit BMP image. */
void brewDisplayWriteBmp(Print& output);

}  // namespace ui
