/* dxcluster_popup.h — Detail popup shown when a spot triangle is clicked. */
#ifndef _DXCLUSTER_POPUP_H_
#define _DXCLUSTER_POPUP_H_

#include "dxcluster.h"

/* Show a popup with details for a single spot. If the spot is on a band
 * other than the active receiver's, asks for confirmation before tuning.
 *
 * `parent_x`, `parent_y` are the screen coordinates near the click, used
 * to position the popup. */
void dxcluster_popup_show_single(const DX_SPOT *spot, int parent_x, int parent_y);

/* Show a list popup when multiple spots are stacked at the same location.
 * Each row has its own Tune button. */
void dxcluster_popup_show_group(const DX_SPOT *spots, int n_spots,
                                int parent_x, int parent_y);

#endif
