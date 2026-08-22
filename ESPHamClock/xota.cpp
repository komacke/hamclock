/* xota.cpp -- see xota.h.
 */

#include "HamClock.h"

#include <cctype>
#include <cstring>

#include "xota.h"

// one entry per recognized "extra" xOTA program, in match priority order.
// order matters only in the sense that the first label found in the comment
// wins if a comment somehow mentions more than one -- not expected to matter
// in practice.
typedef struct {
    const char *label;         // token to search for in the comment, eg "WCA"
    const char *org;           // canonical org name to report, usually == label
} XOTALabel;

static const XOTALabel xota_labels[] = {
    { "WWBOTA", "WWBOTA" },
    { "ARLHS",  "ARLHS"  },
    { "ILLW",   "ILLW"   },
    { "SIOTA",  "SIOTA"  },
    { "SILO",   "SIOTA"  },     // SiOTA's own materials say "CQ SiOTA" or "CQ SILO" are
                                // both valid on-air calls -- catch either
    { "WCA",    "WCA"    },
    { "WAB",    "WAB"    },
};

/* true if c can be part of a reference token: alnum, or the separators that
 * legitimately appear inside one (eg "B/US-0001", "DL-01063", "VK-ARN2").
 */
static bool isRefChar (char c)
{
    return (isalnum ((unsigned char)c) || c == '/' || c == '-');
}

/* starting at comment[from], skip up to a few separator characters (space,
 * colon, dash, equals) that typically sit between a label and its reference,
 * eg "WCA: F-07849" or "WCA F-07849" or "WCA=F-07849". return the index of
 * the first non-separator character, which may be == from if there was
 * nothing to skip, or may run off the end of the string.
 */
static size_t skipLabelSeparator (const char *comment, size_t from)
{
    size_t i = from;
    int n_skipped = 0;
    while (comment[i] && n_skipped < 3
                    && (comment[i] == ' ' || comment[i] == ':' || comment[i] == '-' || comment[i] == '=')) {
        i++;
        n_skipped++;
    }
    return (i);
}

/* WWBOTA references are self-identifying -- always "B/" followed by a short country/prefix
 * code, a dash, and digits, eg "B/US-0001", "B/G-0042" -- distinctive enough (the literal "B/"
 * is not a shape anything else here produces) to trust without requiring the "WWBOTA" label
 * nearby, unlike the rest of xota_labels[]. This matters in practice: real WWBOTA spot comments
 * often lead with the reference itself rather than following a "WWBOTA: ..." convention, which
 * a label-anchored-only scan would miss entirely. Same character-walk style as findIOTARef().
 */
static bool findWWBOTAShape (const char *comment, char *ref, size_t ref_len)
{
    size_t len = strlen (comment);
    for (size_t i = 0; i + 4 <= len; i++) {
        const char *p = comment + i;
        if (toupper((unsigned char)p[0]) != 'B' || p[1] != '/')
            continue;
        // must not be glued to more alnum chars before the 'B'
        if (i > 0 && isalnum ((unsigned char)comment[i-1]))
            continue;

        size_t j = i + 2;                                  // just past "B/"
        size_t code_start = j;
        while (comment[j] && isalpha ((unsigned char)comment[j]) && j - code_start < 4)
            j++;
        if (j == code_start || comment[j] != '-')
            continue;                                       // no letters, or no dash where expected
        j++;                                                 // past the dash
        size_t digit_start = j;
        while (comment[j] && isdigit ((unsigned char)comment[j]) && j - digit_start < 5)
            j++;
        if (j == digit_start)
            continue;                                        // no digits
        if (isalnum ((unsigned char)comment[j]))
            continue;                                        // glued to more alnum after -- not a clean end

        size_t n = j - i;
        if (n >= ref_len)
            continue;
        for (size_t k = 0; k < n; k++)
            ref[k] = toupper ((unsigned char)p[k]);
        ref[n] = '\0';
        return (true);
    }
    return (false);
}

/* see xota.h
 */
bool findXOTARef (const char *comment, char *org, size_t org_len, char *ref, size_t ref_len)
{
    if (!comment || org_len < 8 || ref_len < 12)
        return (false);

    if (findWWBOTAShape (comment, ref, ref_len)) {
        quietStrncpy (org, "WWBOTA", org_len);
        return (true);
    }

    for (const XOTALabel &xl : xota_labels) {

        size_t label_len = strlen (xl.label);
        const char *search_from = comment;

        // a given label might appear more than once in weird comments (rare); only the
        // first occurrence with a plausible reference following it is used
        const char *hit;
        while ((hit = strcistr (search_from, xl.label)) != NULL) {

            size_t i = hit - comment;

            // must not be glued to more alnum chars on either side, else eg "SIOTATION"
            // (not a real word, but the spirit) could false-match
            bool left_ok  = (i == 0) || !isalnum ((unsigned char)comment[i-1]);
            bool right_ok = !isalnum ((unsigned char)hit[label_len]);
            if (left_ok && right_ok) {

                size_t ref_start = skipLabelSeparator (comment, i + label_len);

                // capture a contiguous run of reference-shaped characters
                char candidate[16];
                size_t n = 0;
                while (comment[ref_start + n] && isRefChar (comment[ref_start + n])
                                && n + 1 < sizeof(candidate)) {
                    candidate[n] = comment[ref_start + n];
                    n++;
                }
                // trim a dangling separator left at the end, eg captured "DL-" with
                // nothing after because the comment ended or hit whitespace mid-token
                while (n > 0 && (candidate[n-1] == '-' || candidate[n-1] == '/'))
                    n--;
                candidate[n] = '\0';

                // require something plausible: at least one digit (every one of these
                // programs' reference schemes ends in a number) and reasonable length
                bool has_digit = false;
                for (size_t k = 0; k < n; k++)
                    if (isdigit ((unsigned char)candidate[k]))
                        has_digit = true;

                if (n >= 2 && n < ref_len && has_digit) {
                    quietStrncpy (org, xl.org, org_len);
                    for (size_t k = 0; k < n; k++)
                        ref[k] = toupper ((unsigned char)candidate[k]);
                    ref[n] = '\0';
                    return (true);
                }
            }

            // try again in case this occurrence was a false boundary match or had no
            // usable reference following it -- move just past this hit
            search_from = hit + label_len;
        }
    }

    return (false);
}
