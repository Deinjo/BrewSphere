#pragma once

namespace ui {

/** Draw the first BrewSphere fermentation gauge view. */
void brewDisplayDraw();

/** Redraw the gauge after new Brewfather data arrives. */
void brewDisplayRefresh();

/** Periodic display hook reserved for animations and night mode. */
void brewDisplayTick();

}  // namespace ui
