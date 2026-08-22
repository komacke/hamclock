/* xota.h -- comment-scan detection for the "extra" xOTA programs that have
 * neither a native live-spot API nor a Spothole-visible feed: WCA (World
 * Castles Award), ARLHS and ILLW (lighthouses), SiOTA (Silos), WAB (Worked
 * All Britain), and WWBOTA (bunkers, pending confirmation it's never
 * actually live on Spothole either). See dxcluster.cpp/spots.cpp for how
 * POTA/SOTA/WWFF/GMA/IOTA/LLOTA/WWTOTA differ -- those all have SOME
 * server-reachable feed and show up in the ONTA pane via OHB's backend
 * scripts. These six have no such feed; the only place their activity
 * exists at all is as free text in an ordinary DX Cluster spot's comment,
 * so unlike IOTA (findIOTARef() in iota.h) there is no companion reference
 * database here -- there's nowhere to download one from. A match only ever
 * carries the raw code, never a resolved name.
 *
 * Because several of these programs' reference codes are shape-ambiguous
 * with each other and with POTA/WWFF (eg WCA's "F-07849" is indistinguishable
 * from a park reference by shape alone), matching requires the program's own
 * label token (eg "WCA") to appear in the comment, not just a plausible-
 * looking code. This is stricter, and catches less, than findIOTARef()'s
 * bare shape match -- that's a deliberate tradeoff, not an oversight.
 */

#ifndef _XOTA_H
#define _XOTA_H

// scan comment for a recognized "extra" xOTA program label (WCA, ARLHS, ILLW,
// SIOTA, WAB, WWBOTA) followed by a reference-shaped token; if found, copy
// the label into org (>= 8 bytes) and the reference into ref (>= 12 bytes),
// both upper-cased, and return true. Tries each label in a fixed priority
// order and stops at the first match -- a comment naming two of these at
// once is vanishingly rare and not worth the extra complexity of reporting
// more than one.
extern bool findXOTARef (const char *comment, char *org, size_t org_len, char *ref, size_t ref_len);

#endif // _XOTA_H
