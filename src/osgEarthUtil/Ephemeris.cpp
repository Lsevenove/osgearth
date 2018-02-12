/* -*-c++-*- */
/* osgEarth - Dynamic map generation toolkit for OpenSceneGraph
* Copyright 2016 Pelican Mapping
* http://osgearth.org
*
* osgEarth is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/
#include <osgEarthUtil/Ephemeris>
#include <osg/CoordinateSystemNode>
#include <sstream>

using namespace osgEarth;
using namespace osgEarth::Util;

//------------------------------------------------------------------------

#undef  LC
#define LC "[Ephemeris] "

namespace
{
    // Astronomical Math
    // http://www.stjarnhimlen.se/comp/ppcomp.html

#define d2r(X) osg::DegreesToRadians(X)
#define r2d(X) osg::RadiansToDegrees(X)
#define nrad(X) { while( X >= TWO_PI ) X -= TWO_PI; while( X < 0.0 ) X += TWO_PI; }
#define nrad2(X) { while( X <= -osg::PI ) X += TWO_PI; while( X > osg::PI ) X -= TWO_PI; }

    static const double TWO_PI = (2.0*osg::PI);

    // http://www.stjarnhimlen.se/comp/tutorial.html#4
    // for reference only; same as DateTime::getJulianDay() - 2451544
    double dayNumber(int Y, int M, int D, double hoursUTC)
    {
        int d = 367*Y - (7*(Y+(((M+9)/12))))/4 + ((275*M)/9) + D - 730530;
        return (double)d + hoursUTC/24.0;
    }

    double rev(double a)
    {
        return a - floor(a/360.0)*360.0;
    }

    struct Sun
    {
        // http://www.stjarnhimlen.se/comp/tutorial.html#5
        void getPosition(const DateTime& dt, CelestialBody& sun) const
        {
            static const osg::EllipsoidModel WGS84;

            const double JD_REFTIME = 2451544;
            double d = dt.getJulianDay() - JD_REFTIME;
            //double d = dayNumber(dt.year(), dt.month(), dt.day(), dt.hours());

            double w = 282.9404 + 4.70935E-5 * d;
            const double a = 1.0;

            double e = 0.016709 - 1.151E-9 *d;
            double M = 356.0470 + 0.9856002585 * d;
            double oblecl = 23.4393 - 3.563E-7 * d;
            double L = rev(w + rev(M));

            double E = rev(M + r2d(e*sin(d2r(M))*(1.0 + e * cos(d2r(M)))));
            double x = a*cos(d2r(E)) - e;
            double y = a*sin(d2r(rev(E)))*sqrt(1.0 - e*e);
            double r = sqrt(x*x + y*y);
            double v = r2d(atan2(y, x));
            double sunlon = rev(v + w);

            x = r*cos(d2r(sunlon));
            y = r*sin(d2r(sunlon));
            double z = 0;

            double xequat = x;
            double yequat = y*cos(d2r(oblecl)) + z*sin(d2r(oblecl));
            double zequat = y*sin(d2r(oblecl)) + z*cos(d2r(oblecl));

            double RA_deg = rev(r2d(atan2(yequat, xequat)));
            double DECL_deg = r2d(atan2(zequat, sqrt(xequat*xequat + yequat*yequat)));

            double siteLat_deg = sun._observer.y(); // TODO
            double siteLon_deg = sun._observer.x(); // TODO

            double GMST0_deg = rev(L + 180);
            double UT = d - floor(d);

            sun._rightAscension.set(RA_deg, Units::DEGREES);
            sun._declination.set(DECL_deg, Units::DEGREES);
            sun._latitude.set(DECL_deg, Units::DEGREES);
            sun._longitude.set(rev(0*180+RA_deg-GMST0_deg-UT*360), Units::DEGREES);
            sun._altitude.set(149600000.0 - WGS84.getRadiusEquator(), Units::METERS);
            
            // compute topographic measurements relative to observer position:
            if (sun._observer.isValid())
            {
                double SIDEREAL_deg = GMST0_deg + UT * 360 + siteLon_deg;
                double siteLon_rad = d2r(siteLon_deg);
                double siteLat_rad = d2r(siteLat_deg);
                double hourAngle_rad = d2r(SIDEREAL_deg - RA_deg);
                double DECL_rad = d2r(DECL_deg);
                x = cos(hourAngle_rad) * cos(DECL_rad);
                y = sin(hourAngle_rad) * cos(DECL_rad);
                z = sin(DECL_rad);
                double xhor = x*sin(siteLat_rad) - z*cos(siteLat_rad);
                double yhor = y;
                double zhor = x*cos(siteLat_rad) + z*sin(siteLat_rad);

                double elev = asin(zhor);
                double azim = atan2(yhor, zhor);
                azim = siteLat_deg < 0.0? azim + osg::PI : azim - osg::PI;
                nrad2(azim);

                sun._topoElevation.set(elev, Units::RADIANS);
                sun._topoAzimuth.set(azim, Units::RADIANS);
            }

            /*
            double gclat = siteLat_rad - d2r(-0.1924)*sin(2.0*siteLat_rad);
            double rho = 0.99833 + 0.00167*cos(2.0*siteLat_rad);
            double g = atan(tan(gclat) / cos(hourAngle_rad));
            */

            WGS84.convertLatLongHeightToXYZ(
                sun._latitude.as(Units::RADIANS),
                sun._longitude.as(Units::RADIANS),
                sun._altitude.as(Units::METERS),
                sun._geocentric.x(), sun._geocentric.y(), sun._geocentric.z());

            //OE_DEBUG << "RA = " << RA << ", DECL = " << DECL << ", LAT = " << r2d(out_lat) << ", LON = " << r2d(out_lon) << std::endl;
        }
    };

    struct Moon
    {
        // Math: http://www.stjarnhimlen.se/comp/ppcomp.html
        // More: http://www.stjarnhimlen.se/comp/tutorial.html#7
        // Test: http://www.satellite-calculations.com/Satellite/suncalc.htm
        // Test: http://www.timeanddate.com/astronomy/moon/light.html
        //osg::Vec3d getEarthLonLatRange(int year, int month, int date, double hoursUTC ) const
        void getPosition(const DateTime& dt, CelestialBody& out) const
        {
            static const osg::EllipsoidModel WGS84;

            const double JD_REFTIME = 2451544;
            double d = dt.getJulianDay() - JD_REFTIME;
            //double d = dayNumber(year, month, date, hoursUTC);

            double N = d2r(125.1228 - 0.0529538083 * d);  nrad(N);
            double i = d2r(5.1454);                       
            double w = d2r(318.0634 + 0.1643573223 * d);  nrad(w);
            double a = 60.2666;//  (Earth radii)
            double e = 0.054900;
            double M = d2r(115.3654 + 13.0649929509 * d); nrad(M);

            double E = M + e * sin(M) * ( 1.0 + e * cos(M) );
            nrad(E);

            double Estart = E;
            double Eerror = 9;
            int iterations = 0;
            while (Eerror > 0.0005 && iterations<20)
            {
                iterations++;
                double E0 = E;
                double E1 = E0 - (E0 - e*sin(E0) - M) / (1.0 - e*cos(E0) );
                nrad(E1);
                E = E1;
                Eerror = E<E0? E0-E : E-E0;
            }

            //double E0 = E, E1 = 0.0;
            //double epsilon = d2r(0.001);
            //int count = 0;
            //do {
            //    E1 = E0 - (E0 - e*sin(E0) - M) / (1.0 - e*cos(E0) );
            //    E = E1;
            //    std::swap(E0, E1);
            //    ++count;
            //}
            //while( fabs(E1-E0) > epsilon && count < 10 );
            
            //E = E - (E - e*sin(E) - M) / (1.0 - e*cos(E) );
            
            nrad(E);
            double x = a * ( cos(E) - e );
            double y = a * ( sqrt(1.0 - e*e) * sin(E) );

            double v = atan2( y, x );
            double r = sqrt( x*x + y*y );
            
            //Compute the geocentric (Earth-centered) position of the moon in the ecliptic coordinate system
            double xeclip = r * ( cos(N) * cos(v+w) - sin(N) * sin(v+w) * cos(i) );
            double yeclip = r * ( sin(N) * cos(v+w) + cos(N) * sin(v+w) * cos(i) );
            double zeclip = r * ( sin(v+w) * sin(i) );

            // calculate the ecliptic latitude and longitude here
            double lonEcl = atan2(yeclip, xeclip); nrad(lonEcl);
            double latEcl = atan2(zeclip, sqrt(xeclip*xeclip + yeclip*yeclip));

            // add in the perturbations.
            double Ms = d2r(356.0470 + 0.9856002585 * d); //nrad(Ms); // sun mean anomaly
            double ws = d2r(282.9404 + 4.70935E-5   * d); //nrad(ws); // sun longitude of perihelion
            double Ls = ws + Ms;    nrad(Ls);  // sun mean longitude

            double Mm = M;                     // moon mean anomaly
            double Lm = N + w + Mm; nrad(Lm);  // moon mean longitude
            double D = Lm - Ls;     nrad(D);   // moon mean elongation
            double F = Lm - N;      //nrad(F); // moon argument of latitude

            lonEcl = lonEcl + d2r(
                + (-1.274) * sin(Mm - 2*D)    // (Evection)
                + (+0.658) * sin(2*D)         // (Variation)
                + (-0.186) * sin(Ms)          // (Yearly equation)
                + (-0.059) * sin(2*Mm - 2*D)
                + (-0.057) * sin(Mm - 2*D + Ms)
                + (+0.053) * sin(Mm + 2*D)
                + (+0.046) * sin(2*D - Ms)
                + (+0.041) * sin(Mm - Ms)
                + (-0.035) * sin(D)           // (Parallactic equation)
                + (-0.031) * sin(Mm + Ms)
                + (-0.015) * sin(2*F - 2*D)
                + (+0.011) * sin(Mm - 4*D)
            );

            latEcl = latEcl + d2r(
                + (-0.173) * sin(F - 2*D)
                + (-0.055) * sin(Mm - F - 2*D)
                + (-0.046) * sin(Mm + F - 2*D)
                + (+0.033) * sin(F + 2*D)
                + (+0.017) * sin(2*Mm + F)
            );

            r = r +
                -0.58 * cos(Mm - 2*D)
                -0.46 * cos(2*D);

            // convert to elliptic geocentric (unit)
            double xh = r * cos(lonEcl) * cos(latEcl);
            double yh = r * sin(lonEcl) * cos(latEcl);
            double zh = r * sin(latEcl);

            // and then to rectangular equatorial (unit)
            double ecl = d2r(23.4393 - 3.563E-7 * d); // obliquity of elliptic (tilt of earth)
            double xe = xh;
            double ye = yh*cos(ecl) - zh*sin(ecl);
            double ze = yh*sin(ecl) + zh*cos(ecl);

            // get the ra/decl:
            double RA = atan2(ye, xe); nrad(RA);
            double Decl = atan2(ze, sqrt(xe*xe + ye*ye));
          
            // finally, adjust for the time of day (rotation of the earth).
            double UT = d2r(d - floor(d)); nrad(UT);
            double GMST0 = Ls + d2r(180.0); nrad(GMST0);

            // Note. The paper creates a "correction" called called "topographic RA/DECL"
            // based on an observer location. We're not using that here which is why the
            // longitude doesn't match up exactly with the test site.

            double earthLat = Decl;
            double earthLon = RA - GMST0 - UT;
            nrad(earthLon);

            // since r (distance to moon) is in "earth radius units", resolve it to meters
            r *= WGS84.getRadiusEquator();
            
            out._rightAscension.set(RA, Units::RADIANS);
            out._declination.set(Decl, Units::RADIANS);
            out._latitude.set(earthLat, Units::RADIANS);
            out._longitude.set(earthLon, Units::RADIANS);
            out._altitude.set(r, Units::METERS);

            WGS84.convertLatLongHeightToXYZ(
                out._latitude.as(Units::RADIANS),
                out._longitude.as(Units::RADIANS),
                out._altitude.as(Units::METERS),
                out._geocentric.x(), out._geocentric.y(), out._geocentric.z());
            
            // TODO: topographic data
            out._topoAzimuth.set(0.0, Units::DEGREES);
            out._topoElevation.set(0.0, Units::DEGREES);
        }
    };
}


//------------------------------------------------------------------------

#undef  LC
#define LC "[Ephemeris] "

void
Ephemeris::getSunPosition(const DateTime& dt, CelestialBody& out) const
{
    Sun().getPosition(dt, out);
}

void
Ephemeris::getMoonPosition(const DateTime& dt, CelestialBody& out) const
{
    Moon().getPosition(dt, out);
}

osg::Vec3d
Ephemeris::getECEFfromRADecl(double ra, double decl, double range) const
{
    static const osg::EllipsoidModel WGS84;

    osg::Vec3d out;

    WGS84.convertLatLongHeightToXYZ(
        decl, ra, range,
        out.x(), out.y(), out.z());

    return out;
}
