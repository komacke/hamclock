/* DO NOT HAND EDIT THIS FILE.
 * World Magnetic Model built with ./mkmag.pl Mon Jan 27 02:21:32 2025
 * https://www.ncei.noaa.gov/products/world-magnetic-model
 * 
 * Unit test:
 *    g++ -O2 -Wall -D_TEST_MAIN -o magdecl magdecl.cpp
 */

#if defined(_TEST_MAIN)
#define PROGMEM
#define pgm_read_float(x)  *x
#else
#include "HamClock.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// wmm.cof starting year
static float epoc = 2025.0;

// wmm.cof as two arrays
static const PROGMEM float c0[13][13] = {
    {      0.0, -29351.8,  -2556.6,   1361.0,    895.0,   -233.2,     64.4,     79.5,     23.2,      4.6,     -1.3,      2.9,     -2.0, },
    {   4545.4,  -1410.8,   2951.1,  -2404.1,    799.5,    368.9,     63.8,    -77.0,     10.8,      7.8,     -6.4,     -1.5,     -0.2, },
    {  -3133.6,   -815.1,   1649.3,   1243.8,     55.7,    187.2,     76.9,     -8.8,    -17.5,      3.0,      0.2,     -2.5,      0.3, },
    {    -56.6,    237.5,   -549.5,    453.6,   -281.1,   -138.7,   -115.7,     59.3,      2.0,     -0.2,      2.0,      2.4,      1.2, },
    {    278.6,   -133.9,    212.0,   -375.6,     12.1,   -142.0,    -40.9,     15.8,    -21.7,     -2.5,     -1.0,     -0.6,     -1.3, },
    {     45.4,    220.2,   -122.9,     43.0,    106.1,     20.9,     14.9,      2.5,     16.9,    -13.1,     -0.6,     -0.1,      0.6, },
    {    -18.4,     16.8,     48.8,    -59.8,     10.9,     72.7,    -60.7,    -11.1,     15.0,      2.4,     -0.9,     -0.6,      0.6, },
    {    -48.9,    -14.4,     -1.0,     23.4,     -7.4,    -25.1,     -2.3,     14.2,    -16.8,      8.6,      1.5,     -0.1,      0.5, },
    {      7.1,    -12.6,     11.4,     -9.7,     12.7,      0.7,     -5.2,      3.9,      0.9,     -8.7,      0.9,      1.1,     -0.1, },
    {    -24.8,     12.2,      8.3,     -3.3,     -5.2,      7.2,     -0.6,      0.8,     10.0,    -12.9,     -2.7,     -1.0,     -0.4, },
    {      3.3,      0.0,      2.4,      5.3,     -9.1,      0.4,     -4.2,     -3.8,      0.9,     -9.1,     -3.9,     -0.2,     -0.2, },
    {      0.0,      2.9,     -0.6,      0.2,      0.5,     -0.3,     -1.2,     -1.7,     -2.9,     -1.8,     -2.3,      2.6,     -1.3, },
    {     -1.3,      0.7,      1.0,     -1.4,     -0.0,      0.6,     -0.1,      0.8,      0.1,     -1.0,      0.1,      0.2,     -0.7, },
};
static const PROGMEM float cd0[13][13] = {
    {      0.0,     12.0,    -11.6,     -1.3,     -1.6,      0.6,     -0.2,     -0.0,     -0.1,     -0.0,      0.1,      0.0,      0.0, },
    {    -21.5,      9.7,     -5.2,     -4.2,     -2.4,      1.4,     -0.4,     -0.1,      0.2,     -0.1,      0.0,     -0.0,      0.0, },
    {    -27.7,    -12.1,     -8.0,      0.4,     -6.0,      0.0,      0.9,     -0.1,      0.0,      0.1,      0.1,      0.0,     -0.0, },
    {      4.0,     -0.3,     -4.1,    -15.6,      5.6,      0.6,      1.2,      0.5,      0.5,      0.3,      0.1,      0.0,     -0.0, },
    {     -1.1,      4.1,      1.6,     -4.4,     -7.0,      2.2,     -0.9,     -0.1,     -0.1,     -0.3,     -0.0,      0.0,     -0.0, },
    {     -0.5,      2.2,      0.4,      1.7,      1.9,      0.9,      0.3,     -0.8,      0.3,      0.0,     -0.3,     -0.1,     -0.0, },
    {      0.3,     -1.6,     -0.4,      0.9,      0.7,      0.9,      0.9,     -0.8,      0.2,      0.3,      0.0,      0.0,      0.1, },
    {      0.6,      0.5,     -0.8,      0.0,     -1.0,      0.6,     -0.2,      0.8,     -0.0,     -0.1,     -0.1,     -0.0,     -0.0, },
    {     -0.2,      0.5,     -0.4,      0.4,     -0.5,     -0.6,      0.3,      0.2,      0.2,      0.1,     -0.1,     -0.1,      0.0, },
    {     -0.3,      0.3,     -0.3,      0.3,      0.2,     -0.1,     -0.2,      0.4,      0.1,     -0.1,     -0.0,     -0.1,      0.0, },
    {      0.0,     -0.0,     -0.2,      0.1,     -0.1,      0.1,      0.0,     -0.1,      0.2,     -0.0,     -0.0,     -0.1,     -0.1, },
    {     -0.0,      0.1,     -0.0,      0.1,     -0.0,     -0.0,      0.1,     -0.0,      0.0,      0.0,      0.0,     -0.1,     -0.0, },
    {     -0.0,      0.0,     -0.1,      0.1,     -0.0,     -0.0,     -0.0,      0.0,     -0.0,     -0.0,      0.0,     -0.1,     -0.1, },
};

/* return pointer to a malloced 13x13 2d array of float such that caller can use array[i][j]
 */
static float **malloc1313(void)
{
     #define SZ 13

     // room for SZ row pointers plus SZ*SZ floats
     float **arr = (float **) malloc (sizeof(float *) * SZ + sizeof(float) * SZ * SZ);

     // ptr points to first float
     float *ptr = (float *)(arr + SZ);

     // set up row pointers
     for (int i = 0; i < SZ; i++) 
         arr[i] = (ptr + SZ * i);

     return (arr);
}

static int E0000(int *maxdeg, float alt,
float glat, float glon, float t, float *dec, float *mdp, float *ti,
float *gv)
{
      int maxord,n,m,j,D1,D2,D3,D4;
 
      // N.B. not enough stack space in ESP8266 for these
      // float c[13][13],cd[13][13],tc[13][13],dp[13][13];
      // float snorm[169]
      float **c = malloc1313();
      float **cd = malloc1313();
      float **tc = malloc1313();
      float **dp = malloc1313();
      float **k = malloc1313();
      float *snorm = (float *) malloc (169 * sizeof(float));
 
      float sp[13],cp[13],fn[13],fm[13],pp[13],dtr,a,b,re,
          a2,b2,c2,a4,b4,c4,flnmj,
          dt,rlon,rlat,srlon,srlat,crlon,crlat,srlat2,
          crlat2,q,q1,q2,ct,st,r2,r,d,ca,sa,aor,ar,br,bt,bp,bpp,
          par,temp1,temp2,parp,bx,by,bz,bh;
      float *p = snorm;


// GEOMAG:

/* INITIALIZE CONSTANTS */
      maxord = *maxdeg;
      // m = 0 must contribute nothing to the azimuthal field term.
      // This array used to be static in the reference WMM code, where fm[0]
      // was implicitly zero; as a local array it must be initialized explicitly.
      sp[0] = fm[0] = 0.0;
      cp[0] = *p = pp[0] = 1.0;
      dp[0][0] = 0.0;
      a = 6378.137;
      b = 6356.7523142;
      re = 6371.2;
      a2 = a*a;
      b2 = b*b;
      c2 = a2-b2;
      a4 = a2*a2;
      b4 = b2*b2;
      c4 = a4 - b4;

      // N.B. the algorithm modifies c[][] and cd[][] IN PLACE so they must be inited each time upon entry.
      for (int i = 0; i < 13; i++) {
          for (int j = 0; j < 13; j++) {
              c[i][j] = pgm_read_float (&c0[i][j]);
              cd[i][j] = pgm_read_float (&cd0[i][j]);
          }
      }

/* CONVERT SCHMIDT NORMALIZED GAUSS COEFFICIENTS TO UNNORMALIZED */
      *snorm = 1.0;
      for (n=1; n<=maxord; n++)
      {
        *(snorm+n) = *(snorm+n-1)*(float)(2*n-1)/(float)n;
        j = 2;
        for (m=0,D1=1,D2=(n-m+D1)/D1; D2>0; D2--,m+=D1)
        {
          k[m][n] = (float)(((n-1)*(n-1))-(m*m))/(float)((2*n-1)*(2*n-3));
          if (m > 0)
          {
            flnmj = (float)((n-m+1)*j)/(float)(n+m);
            *(snorm+n+m*13) = *(snorm+n+(m-1)*13)*sqrtf(flnmj);
            j = 1;
            c[n][m-1] = *(snorm+n+m*13)*c[n][m-1];
            cd[n][m-1] = *(snorm+n+m*13)*cd[n][m-1];
          }
          c[m][n] = *(snorm+n+m*13)*c[m][n];
          cd[m][n] = *(snorm+n+m*13)*cd[m][n];
        }
        fn[n] = (float)(n+1);
        fm[n] = (float)n;
      }
      k[1][1] = 0.0;

/*************************************************************************/

// GEOMG1:

      dt = t - epoc;
      if (dt < 0.0 || dt > 5.0) {
          *ti = epoc;                   /* pass back base time for diag msg */
          free(c);
          free(cd);
          free(tc);
          free(dp);
          free(k);
          free(snorm);
          return (-1);
      }

      dtr = M_PI/180.0;
      rlon = glon*dtr;
      rlat = glat*dtr;
      srlon = sinf(rlon);
      srlat = sinf(rlat);
      crlon = cosf(rlon);
      crlat = cosf(rlat);
      srlat2 = srlat*srlat;
      crlat2 = crlat*crlat;
      sp[1] = srlon;
      cp[1] = crlon;

/* CONVERT FROM GEODETIC COORDS. TO SPHERICAL COORDS. */
      q = sqrtf(a2-c2*srlat2);
      q1 = alt*q;
      q2 = ((q1+a2)/(q1+b2))*((q1+a2)/(q1+b2));
      ct = srlat/sqrtf(q2*crlat2+srlat2);
      st = sqrtf(1.0-(ct*ct));
      r2 = (alt*alt)+2.0*q1+(a4-c4*srlat2)/(q*q);
      r = sqrtf(r2);
      d = sqrtf(a2*crlat2+b2*srlat2);
      ca = (alt+d)/r;
      sa = c2*crlat*srlat/(r*d);
      for (m=2; m<=maxord; m++)
      {
        sp[m] = sp[1]*cp[m-1]+cp[1]*sp[m-1];
        cp[m] = cp[1]*cp[m-1]-sp[1]*sp[m-1];
      }
      aor = re/r;
      ar = aor*aor;
      br = bt = bp = bpp = 0.0;
      for (n=1; n<=maxord; n++)
      {
        ar = ar*aor;
        for (m=0,D3=1,D4=(n+m+D3)/D3; D4>0; D4--,m+=D3)
        {
/*
   COMPUTE UNNORMALIZED ASSOCIATED LEGENDRE POLYNOMIALS
   AND DERIVATIVES VIA RECURSION RELATIONS
*/
          if (n == m)
          {
            *(p+n+m*13) = st**(p+n-1+(m-1)*13);
            dp[m][n] = st*dp[m-1][n-1]+ct**(p+n-1+(m-1)*13);
            goto S50;
          }
          if (n == 1 && m == 0)
          {
            *(p+n+m*13) = ct**(p+n-1+m*13);
            dp[m][n] = ct*dp[m][n-1]-st**(p+n-1+m*13);
            goto S50;
          }
          if (n > 1 && n != m)
          {
            if (m > n-2) *(p+n-2+m*13) = 0.0;
            if (m > n-2) dp[m][n-2] = 0.0;
            *(p+n+m*13) = ct**(p+n-1+m*13)-k[m][n]**(p+n-2+m*13);
            dp[m][n] = ct*dp[m][n-1] - st**(p+n-1+m*13)-k[m][n]*dp[m][n-2];
          }
S50:
/*
    TIME ADJUST THE GAUSS COEFFICIENTS
*/
          tc[m][n] = c[m][n]+dt*cd[m][n];
          if (m != 0) tc[n][m-1] = c[n][m-1]+dt*cd[n][m-1];
/*
    ACCUMULATE TERMS OF THE SPHERICAL HARMONIC EXPANSIONS
*/
          par = ar**(p+n+m*13);
          if (m == 0)
          {
            temp1 = tc[m][n]*cp[m];
            temp2 = tc[m][n]*sp[m];
          }
          else
          {
            temp1 = tc[m][n]*cp[m]+tc[n][m-1]*sp[m];
            temp2 = tc[m][n]*sp[m]-tc[n][m-1]*cp[m];
          }
          bt = bt-ar*temp1*dp[m][n];
          bp += (fm[m]*temp2*par);
          br += (fn[n]*temp1*par);
/*
    SPECIAL CASE:  NORTH/SOUTH GEOGRAPHIC POLES
*/
          if (st == 0.0 && m == 1)
          {
            if (n == 1) pp[n] = pp[n-1];
            else pp[n] = ct*pp[n-1]-k[m][n]*pp[n-2];
            parp = ar*pp[n];
            bpp += (fm[m]*temp2*parp);
          }
        }
      }
      if (st == 0.0) bp = bpp;
      else bp /= st;
/*
    ROTATE MAGNETIC VECTOR COMPONENTS FROM SPHERICAL TO
    GEODETIC COORDINATES
*/
      bx = -bt*ca-br*sa;
      by = bp;
      bz = bt*sa-br*ca;
/*
    COMPUTE DECLINATION (DEC), INCLINATION (DIP) AND
    TOTAL INTENSITY (TI)
*/
      bh = sqrtf((bx*bx)+(by*by));
      *ti = sqrtf((bh*bh)+(bz*bz));
      *dec = atan2f(by,bx)/dtr;
      *mdp = atan2f(bz,bh)/dtr;
/*
    COMPUTE MAGNETIC GRID VARIATION IF THE CURRENT
    GEODETIC POSITION IS IN THE ARCTIC OR ANTARCTIC
    (I.E. GLAT > +55 DEGREES OR GLAT < -55 DEGREES)

    OTHERWISE, SET MAGNETIC GRID VARIATION TO -999.0
*/
      *gv = -999.0;
      if (fabs(glat) >= 55.)
      {
        if (glat > 0.0 && glon >= 0.0) *gv = *dec-glon;
        if (glat > 0.0 && glon < 0.0) *gv = *dec+fabs(glon);
        if (glat < 0.0 && glon >= 0.0) *gv = *dec+glon;
        if (glat < 0.0 && glon < 0.0) *gv = *dec-fabs(glon);
        if (*gv > +180.0) *gv -= 360.0;
        if (*gv < -180.0) *gv += 360.0;
      }

     free(c);
     free(cd);
     free(tc);
     free(dp);
     free(k);
     free(snorm);

     return (0);
}


/* compute magnetic declination for given location, elevation and time.
 * sign is such that true az = mag + declination.
 * if ok return true, else return false and, since out of date range is the only cause for failure,
 *    *mdp is set to the beginning year of valid 5 year period.
 */
bool magdecl (
    float l, float L,                   // geodesic lat, +N, long, +E, degrees
    float e,                            // elevation, m
    float y,                            // time, decimal year
    float *mdp                          // return magnetic declination, true degrees E of N 
)
{
        float alt = e/1000.;
        float dp, ti, gv;
        int maxdeg = 12;

        bool ok = E0000(&maxdeg,alt,l,L,y,mdp,&dp,&ti,&gv) == 0;

#ifdef _TEST_MAIN
        if (ok) {
            printf ("inclination %g\n", dp);
            printf ("total field %g nT\n", ti);
        }
#endif // _TEST_MAIN

        if (!ok)
            *mdp = ti;                  // return start of valid date range
        return (ok);
}

#ifdef _TEST_MAIN


/* stand-alone test program
 */

int main (int ac, char *av[])
{
        if (ac != 5) {
            char *slash = strrchr (av[0], '/');
            char *base = slash ? slash+1 : av[0];
            fprintf (stderr, "Purpose: test stand-alone magnetic declination model.\n");
            fprintf (stderr, "Usage: %s lat_degsN lng_degsE elevation_m decimal_year\n", base);
            exit(1);
        }

        float l = atof (av[1]);
        float L = atof (av[2]);
        float e = atof (av[3]);
        float y = atof (av[4]);
        float mdp;

        if (magdecl (l, L, e, y, &mdp))
            printf ("declination %g\n", mdp);
        else
            printf ("model only value from %g to %g\n", mdp, mdp+5);

        return (0);
}

#endif // _TEST_MAIN

