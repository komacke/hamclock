/* iota.h -- lookup for IOTA group reference -> name, resolved from DX Cluster
 * spot comments. Cache built server-side by fetchIOTA.py, same pattern as
 * ontheair.cpp's onta_parks.txt.
 */

#ifndef _IOTA_H
#define _IOTA_H

// call periodically (eg alongside retrieveONTAParks()); cheap no-op most of
// the time since openCachedFile() enforces its own refresh interval.
extern void retrieveIOTACache (void);

// look for an IOTA-shaped reference (eg "EU-005") anywhere in comment; if
// found copy it upper-cased into out (>= 8 bytes) and return true.
extern bool findIOTARef (const char *comment, char *out, size_t out_len);

// look up the group name for a reference already extracted with findIOTARef().
// returns NULL if not (yet) in the cache -- caller should still show the raw
// reference in that case, same fallback spirit as ontaStateOrCountry().
extern const char *lookupIOTAName (const char *ref);

#endif // _IOTA_H
