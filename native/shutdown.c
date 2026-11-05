/* Internal shutdown coordinator. Called only with calls+registry+core locks and
 * zero live engines. Never called by DllMain. All engine domains are gone. */
#include "shutdown.h"
int s4_shared_state_shutdown(void) {
    /* Preflight COM/event ownership before changing any state. File slots and
     * find enumerations are raw CRT resources, not arena allocations. */
    if(vc_resources_shutdown()) return -1;
    vc_token_reset();
    /* Allocation headers own every shared lexicon, ref-counted config, inventory,
     * common word and event-pool block. Bulk release avoids traversing already
     * freed shared graphs; no surviving engine can borrow them. */
    s4_shared_shutdown();
    /* This also resets every original image global from the immutable source on
     * the next init, including refs, mode roots, tapes, stage caches and once flags. */
    s4_image_shutdown();
    return 0;
}
