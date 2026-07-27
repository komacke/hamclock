/* maidenhead conversion functions.
 *
 * unit test: gcc -D_UNIT_TEST -o maidenhead maidenhead.cpp -lm
 *
 * verify grid to lat/lon and lat/log back to same grid
 *
 * ./maidenhead em12ne
 * em12ne:   32.1875  -96.8750
 * ./maidenhead 32.1875 -96.875
 * 32.1875  -96.8750: EM12ne
 *
 * verify boundary conditions
 *
 * ./maidenhead 90.0 180.0
 * 90.0000  180.0000: RR99xx
 * ./maidenhead -90.0 -180.0
 * -90.0000 -180.0000: AA00aa
 * ./maidenhead RR99xx
 * RR99xx:   89.9792  179.9584
 * ./maidenhead AA00aa
 * AA00aa:  -89.9792 -179.9583
 *
 * verify center of grid is returned in lat lon
 *
 * ./maidenhead em12
 * em12:   32.5000  -97.0000
 *
 */

#ifdef _UNIT_TEST

// stand-alone test program

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

#define M_PIF   3.14159265F
#define deg2rad(d)      ((M_PIF/180)*(d))
#define rad2deg(d)      ((180/M_PIF)*(d))


/* capture lat, lng with some handy related tools.
 *
 * only thing I don't like about this is that the need to set the unit vector prevents using const LatLong
 * when the intent is to promise not to change location.
 */
class LatLong
{
	public:

		LatLong(void) {
			lat_set = lng_set = -9.75e10;               // any unlikely value
			lat = lat_d = lng = lng_d = 0;              // just to be nice
		}

		LatLong (float latitude_d, float longitude_d) : LatLong() {
			lat_d = latitude_d;
			lng_d = longitude_d;
			normalize();
		}

		/* direct access -- too late for setters/getters -- that's why we need lat/lng_set
		 */
		float lat, lat_d;                               // rads and degrees N
		float lng, lng_d;                               // rads and degrees E

		/* great circle distance between us and ll in rads; mult by ERAD_M to get miles
		 */
		float GSD (LatLong &ll) {
			setXYZ();
			ll.setXYZ();
			float dx = x - ll.x;
			float dy = y - ll.y;
			float dz = z - ll.z;
			float chord = sqrtf (dx*dx + dy*dy + dz*dz);
			return (2 * asinf (chord/2));               // convert chord to great circle
		}

        /* given the _d degree members:
         *   clamp lat to [-90,90];
         *   modulo lng to [-180,180).
         * then fill in the radian members and set up the unit vector too.
         */
        void normalize (void) {
            lat_d = fmaxf(fminf(lat_d,90),-90);         // clamp lat
            lat = deg2rad(lat_d);
            lng_d = fmodf(lng_d+(2*360+180),360)-180;   // wrap lng
            lng = deg2rad(lng_d);
            setXYZ();
        }


	private:

		// insure vector on unit sphere at lat/lng is ready
		void setXYZ(void) {
			if (lat_set != lat || lng_set != lng) {
				float clat = cosf (lat);
				x = clat * cosf (lng);
				y = clat * sinf (lng);
				z = sinf (lat);
				lat_set = lat;
				lng_set = lng;
			}
		}

		float lat_set, lng_set;                 // xyz valid iff these match the public values
		float x, y, z;                          // location on unit sphere
};

#define MAID_CHARLEN    7       // maidenhead string length, including EOS

#else

// part of HamClock

#include "HamClock.h"

#endif // !_UNIT_TEST



/* convert lat_d,lng_d to the containing maidenhead designation string.
 * grids grow northward from -90 and westward from -180
 */
void ll2maidenhead (char maid[MAID_CHARLEN], const LatLong &ll)
{

    // normalize using same algorithm as LatLong::normalize() for consistency
    float lat = fmaxf(fminf(ll.lat_d, 90.0F), -90.0F);
    float lng = fmodf(ll.lng_d+(2*360+180), 360) - 180;

    // clamp to avoid array overflow at +180/+90 boundaries after normalization
    float lg = (lng < 179.9999F ? lng : 179.9999F) + 180.0F;  // 0 .. <360
    float lt = (lat <  89.9999F ? lat :  89.9999F) + 90.0F;   // 0 .. <180
	
	uint16_t o;

	o = lg/20;                          // 20 deg steps
	maid[0] = 'A' + o;
	lg -= o*20;
	o = lg/2;                           // 2 deg steps
	maid[2] = '0' + o;
	lg -= o*2;
	o = lg/(5.0F/60.0F);                // 5 minute steps
	maid[4] = 'a' + o;

	o = lt/10;                          // 10 deg steps
	maid[1] = 'A' + o;
	lt -= o*10;
	o = lt/1;                           // 1 deg steps
	maid[3] = '0' + o;
	lt -= o*1;
	o = lt/(2.5F/60.0F);                // 2.5 minute steps
	maid[5] = 'a' + o;

	maid[6] = '\0';
}

/* convert maidenhead string to ll at SW corner.
 * accept 4 or 6 char version and allow either case.
 */
bool maidenhead2ll (LatLong &ll, const char maid[MAID_CHARLEN])
{
	// work in all upper-case
	char uc_maid[MAID_CHARLEN];
	for (int i = 0; i < MAID_CHARLEN; i++)
		uc_maid[i] = toupper(maid[i]);

	// check first four chars, always required
	if (uc_maid[0] < 'A' || uc_maid[0] > 'R'
				  || uc_maid[1] < 'A' || uc_maid[1] > 'R'
				  || !isdigit(uc_maid[2]) || !isdigit(uc_maid[3]))
		return (false);

	// determine if 4 or 6 char grid
	bool is4char = (uc_maid[4] == '\0' ||
				   (uc_maid[4] == ' ' && (uc_maid[5] == '\0' || uc_maid[5] == ' ')));

	if (is4char) {
		// center of a 4-char square: half of 2deg lon, half of 1deg lat
		ll.lng_d = 20.0F*(uc_maid[0]-'A') + 2.0F*(uc_maid[2]-'0') - 180.0F
				   + 1.0F;          // half of 2deg square width
		ll.lat_d = 10.0F*(uc_maid[1]-'A') + 1.0F*(uc_maid[3]-'0') - 90.0F
				   + 0.5F;          // half of 1deg square height
	} else {
		// validate subsquare chars
		if (uc_maid[4] < 'A' || uc_maid[4] > 'X' || uc_maid[5] < 'A' || uc_maid[5] > 'X')
			return (false);
		// center of a 6-char subsquare: half of 5' lon, half of 2.5' lat
		ll.lng_d = 20.0F*(uc_maid[0]-'A') + 2.0F*(uc_maid[2]-'0') + (5.0F/60.0F)*(uc_maid[4]-'A') - 180.0F
				   + (2.5F/60.0F);  // half of 5' subsquare width
		ll.lat_d = 10.0F*(uc_maid[1]-'A') + 1.0F*(uc_maid[3]-'0') + (2.5F/60.0F)*(uc_maid[5]-'A') - 90.0F
				   + (1.25F/60.0F); // half of 2.5' subsquare height
	}

	ll.normalize();  // lat and long are already normalized, so just calc radians and XYZ coords
	return (true);
}


#if !defined(_UNIT_TEST)


/* set NVRAM nv to the maidenhead location for ll
 */
void setNVMaidenhead(NV_Name nv, LatLong &ll)
{
	char maid[MAID_CHARLEN];
	ll2maidenhead (maid, ll);
	NVWriteString (nv, maid);
}

/* return the given maidenhead value from NV.
 */
void getNVMaidenhead (NV_Name nv, char maid[MAID_CHARLEN])
{
	if (!NVReadString (nv, maid))
		fatalError ("getNVMaidenhead invalid %d", (int)nv);
}

#endif // !_UNIT_TEST

#ifdef _UNIT_TEST

int main (int ac, char *av[])
{
	if (ac == 2) {
		// given maidenhead, find ll
		LatLong ll;
		char *maid = av[1];
		if (!maidenhead2ll (ll, maid))
			printf ("Bad maidenhead: %s\n", maid);
		else
			printf ("%s: %9.4f %9.4f\n", maid, ll.lat_d, ll.lng_d);
	} else if (ac == 3) {
		// given ll, find maidenhead
		LatLong ll;
		ll.lat_d = atof(av[1]);
		ll.lng_d = atof(av[2]);
		char maid[MAID_CHARLEN];
		ll2maidenhead (maid, ll);
		printf ("%9.4f %9.4f: %s\n", ll.lat_d, ll.lng_d, maid);
	} else {
		fprintf (stderr, "Purpose: comvert between lat/long and maidenhead grid square.\n");
		fprintf (stderr, "Usage 1: %s <grid>\n", av[0]);
		fprintf (stderr, "Usage 2: %s <lat> <long>\n", av[0]);
		exit (1);
	}

	return (0);
}

#endif // _UNIT_TEST
