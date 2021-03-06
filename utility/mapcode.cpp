/*
 * Copyright (C) 2014-2015 Stichting Mapcode Foundation (http://www.mapcode.com)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * This application uses the Mapcode C library to encode and decode Mapcodes.
 * It also serves as an example of how to use this library in a C environment.
 *
 * It also offers additional options to generate "test sets" of coordinates
 * and Mapcodes to check other Mapcode implementations against reference data.
 *
 * These test sets consist of:
 *
 * - a number of "grid distributed" coordinates, which forms a set of coordinates
 *   and their Mapcodes, wrapped as a grid around the Earth;
 *
 * - a number of "random uniformly distributed" coordinates, which forms a set of
 *   random coordinates on the surface of Earth; or
 *
 * - a set which consists of typical Mapcode "boundaries" and "edge cases", based
 *   on the internal implementation of the boundaries database of the Mapcode
 *   implementation.
 *
 * If the executable is named mapcode_debug, the self-checking mechanism is
 * activated. Note, however, that the self checks may fail for certain decodes
 * even though the decodes are OK.
 */

#include <stdio.h>
#include <math.h>
#include <time.h>
#include "../mapcodelib/mapcoder.c"

// Specific define to be able to limit output to microdegrees, for test files.
#undef LIMIT_TO_MICRODEGREES

#define my_isnan(x) (false)
#define my_round(x) ((int) (floor((x) + 0.5)))

static int selfCheckEnabled = 0;

static const int NORMAL_ERROR = 1;
static const int INTERNAL_ERROR = 2;

/**
 * Some global constants to be used.
 */
static const double PI = 3.14159265358979323846;
static const int SHOW_PROGRESS = 125;
static const double DELTA = 0.001;


/**
 * These statistics are stored globally so they can be updated easily by the
 * generateAndOutputMapcodes() method.
 */
static int totalNrOfPoints = 0;
static int totalNrOfResults = 0;
static int largestNrOfResults = 0;
static double latLargestNrOfResults = 0.0;
static double lonLargestNrOfResults = 0.0;


/**
 * The usage() method explains how this application can be used. It is called
 * whenever a incorrect amount or combination of parameters is entered.
 */
static void usage(const char *appName) {
    printf("MAPCODE (version %s)\n", mapcode_cversion);
    printf("Copyright (C) 2014-2015 Stichting Mapcode Foundation\n");
    printf("\n");
#ifndef SUPPORT_HIGH_PRECISION
    printf("Warning: High precision support is disabled in this build.)\n\n");
#endif
#ifdef LIMIT_TO_MICRODEGREES
    printf("Warning: This build is limited to using microdegrees.\n\n");
#endif
    printf("Usage:\n");
    printf("    %s [-d| --decode] <default-territory> <mapcode> [<mapcode> ...]\n", appName);
    printf("\n");
    printf("       Decode a mapcode to a lat/lon. The default territory code is used if\n");
    printf("       the mapcode is a shorthand local code\n");
    printf("\n");
    printf("    %s [-e[0-8] | --encode[0-8]] <lat:-90..90> <lon:-180..180> [territory]>\n", appName);
    printf("\n");
    printf("       Encode a lat/lon to a mapcode. If the territory code is specified, the\n");
    printf("       encoding will only succeeed if the lat/lon is located in the territory.\n");
    printf("       You can specify the number of additional digits, 0, 1 or 2 (default 0)\n");
    printf("       for high-precision mapcodes.\n");
    printf("\n");
    printf("    %s [-b[XYZ] | --boundaries[XYZ]] [<extraDigits>]\n", appName);
    printf("    %s [-g[XYZ] | --grid[XYZ]]   <nrOfPoints> [<extraDigits>]\n", appName);
    printf("    %s [-r[XYZ] | --random[XYZ]] <nrOfPoints> [<extraDigits>] [<seed>]\n", appName);
    printf("\n");
    printf("       Create a test set of lat/lon pairs based on the mapcode boundaries database\n");
    printf("       as a fixed 3D grid or random uniformly distributed set of lat/lons with their\n");
    printf("       (x, y, z) coordinates and all mapcode aliases.\n");
    printf("\n");
    printf("       <extraDigits>: 0-8; specifies additional accuracy, use 0 for standard.\n");
    printf("       <seed> is an optional random seed, use 0 for arbitrary>.\n");
    printf("       (You may wish to specify a specific seed to regenerate test cases).\n");
    printf("\n");
    printf("       The output format is:\n");
    printf("           <number-of-aliases> <lat-deg> <lon-deg> [<x> <y> <z>]\n");
    printf("           <territory> <mapcode>      (repeated 'number-of-aliases' times)\n");
    printf("                                      (empty lines and next record)\n");
    printf("       Ranges:\n");
    printf("           number-of-aliases : >= 1\n");
    printf("           lat-deg, lon-deg  : [-90..90], [-180..180]\n");
    printf("           x, y, z           : [-1..1]\n");
    printf("\n");
    printf("       The lat/lon pairs will be distributed over the 3D surface of the Earth\n");
    printf("       and the (x, y, z) coordinates are placed on a sphere with radius 1.\n");
    printf("       The (x, y, z) coordinates are primarily meant for visualization of the data set.\n");
    printf("\n");
    printf("       Example:\n");
    printf("       %s -g    100 : produces a grid of 100 points as lat/lon pairs\n", appName);
    printf("       %s -gXYZ 100 : produces a grid of 100 points as (x, y, z) sphere coordinates)\n", appName);
    printf("\n");
    printf("       Notes on the use of stdout and stderr:\n");
    printf("       stdout: used for outputting 3D point data; stderr: used for statistics.\n");
    printf("       You can redirect stdout to a destination file, while stderr will show progress.\n");
    printf("\n");
    printf("       The result code is 0 when no error occurred, 1 if an input error occurred and 2\n");
    printf("       if an internal error occurred.\n");
}


/**
 * The method radToDeg() converts radians to degrees.
 */
static double radToDeg(double rad) {
    return (rad / PI) * 180.0;
}


/**
 * The method degToRad() converts degrees to radians.
 */
static double degToRad(double deg) {
    return (deg / 180.0) * PI;
}


/**
 * Given a single number between 0..1, generate a latitude, longitude (in degrees) and a 3D
 * (x, y, z) point on a sphere with a radius of 1.
 */
static void unitToLatLonDeg(
        const double unit1, const double unit2, double *latDeg, double *lonDeg) {

    // Calculate uniformly distributed 3D point on sphere (radius = 1.0):
    // http://mathproofs.blogspot.co.il/2005/04/uniform-random-distribution-on-sphere.html
    const double theta0 = (2.0 * PI) * unit1;
    const double theta1 = acos(1.0 - (2.0 * unit2));
    double x = sin(theta0) * sin(theta1);
    double y = cos(theta0) * sin(theta1);
    double z = cos(theta1);

    // Convert Carthesian 3D point into lat/lon (radius = 1.0):
    // http://stackoverflow.com/questions/1185408/converting-from-longitude-latitude-to-cartesian-coordinates
    const double latRad = asin(z);
    const double lonRad = atan2(y, x);

    // Convert radians to degrees.
    *latDeg = my_isnan(latRad) ? 90.0 : radToDeg(latRad);
    *lonDeg = my_isnan(lonRad) ? 180.0 : radToDeg(lonRad);
}


/**
 * The method convertLatLonToXYZ() convertes a lat/lon pair to a (x, y, z) coordinate
 * on a sphere with radius 1.
 */
static void convertLatLonToXYZ(double latDeg, double lonDeg, double *x, double *y, double *z) {
    double latRad = degToRad(latDeg);
    double lonRad = degToRad(lonDeg);
    *x = cos(latRad) * cos(lonRad);
    *y = cos(latRad) * sin(lonRad);
    *z = sin(latRad);
}


/**
 * This methods provides a self check for encoding lat/lon to Mapcode.
 */
static void selfCheckLatLonToMapcode(const double lat, double lon, const char *territory, const char *mapcode,
                                     int extraDigits) {
    int context = convertTerritoryIsoNameToCode(territory, 0);
    char *results[2 * MAX_NR_OF_MAPCODE_RESULTS];
    const double limitLat = (lat < -90.0) ? -90.0 : ((lat > 90.0) ? 90.0 : lat);
    const double limitLon = (lon < -180.0) ? -180.0 : ((lon > 180.0) ? 180.0 : lon);
    const int nrResults = encodeLatLonToMapcodes_Deprecated(results, limitLat, limitLon, context, extraDigits);
    if (nrResults <= 0) {
        fprintf(stderr, "error: encoding lat/lon to mapcode failure; "
                        "cannot encode lat=%.12g, lon=%.12g (default territory=%s)\n",
                lat, lon, territory);
        if (selfCheckEnabled) {
            exit(INTERNAL_ERROR);
        }
        return;
    }
    int found = 0;
    for (int i = 0; !found && (i < nrResults); ++i) {

        /* Check if the territory and code were found in results. Note that the territory
         * may be a minimal code, like IN (which may indicate US-IN or RU-IN).
         */
        const char *foundMapcode = results[(i * 2)];
        const char *foundTerritory = results[(i * 2) + 1];
        char *foundTerritoryMin = strstr(foundTerritory, "-");
        if (foundTerritoryMin && (strlen(foundTerritoryMin) > 0)) {
            ++foundTerritoryMin;
        }

        found = (((strcmp(territory, foundTerritory) == 0) ||
                  (strcmp(territory, foundTerritoryMin) == 0)) &&
                 (strcmp(mapcode, foundMapcode) == 0));
    }
    if (!found) {
        fprintf(stderr, "error: encoding lat/lon to mapcode failure; "
                        "mapcode '%s %s' decodes to lat=%.12g(%.12g), lon=%.12g(%.12g), "
                        "which does not encode back to '%s %s'\n",
                territory, mapcode, lat, limitLat, lon, limitLon, territory, mapcode);
        if (selfCheckEnabled) {
            exit(INTERNAL_ERROR);
        }
        return;
    }
}


/**
 * This method provides a self-check for decoding a Mapcode to lat/lon.
 */
static void selfCheckMapcodeToLatLon(const char *territory, const char *mapcode,
                                     const double lat, const double lon) {
    double foundLat;
    double foundLon;
    int foundContext = convertTerritoryIsoNameToCode(territory, 0);
    int err = decodeMapcodeToLatLon(&foundLat, &foundLon, mapcode, foundContext);
    if (err != 0) {
        fprintf(stderr, "error: decoding mapcode to lat/lon failure; "
                "cannot decode '%s %s')\n", territory, mapcode);
        if (selfCheckEnabled) {
            exit(INTERNAL_ERROR);
        }
        return;
    }
    double deltaLat = ((foundLat - lat) >= 0.0 ? (foundLat - lat) : -(foundLat - lat));
    double deltaLon = ((foundLon - lon) > -0.0 ? (foundLon - lon) : -(foundLon - lon));
    if (deltaLon > 180.0) {
        deltaLon = 360.0 - deltaLon;
    }
    if ((deltaLat > DELTA) || (deltaLon > DELTA)) {
        fprintf(stderr, "error: decoding mapcode to lat/lon failure; "
                        "lat=%.12g, lon=%.12g produces mapcode %s %s, "
                        "which decodes to lat=%.12g (delta=%.12g), lon=%.12g (delta=%.12g)\n",
                lat, lon, territory, mapcode, foundLat, deltaLat, foundLon, deltaLon);
        if (selfCheckEnabled) {
            exit(INTERNAL_ERROR);
        }
        return;
    }
}

static void generateAndOutputMapcodes(double lat, double lon, int iShowError, int extraDigits, int useXYZ) {

    char *results[2 * MAX_NR_OF_MAPCODE_RESULTS];
    int context = 0;

    while (lon > 180.0) {
        lon -= 360.0;
    }
    while (lon < -180.0) {
        lon += 360.0;
    }
    while (lat > 90.0) {
        lat -= 180.0;
    }
    while (lat < -90.0) {
        lat += 180.0;
    }

#ifdef LIMIT_TO_MICRODEGREES
    {
        // Need to truncate lat/lon to microdegrees.
        long lon32 = lon * 1000000.0;
        long lat32 = lat * 1000000.0;
        lon = (lon32 / 1000000.0);
        lat = (lat32 / 1000000.0);
    }
#endif

    const int nrResults = encodeLatLonToMapcodes_Deprecated(results, lat, lon, context, extraDigits);
    if (nrResults <= 0) {
        if (iShowError) {
            fprintf(stderr, "error: cannot encode lat=%.12g, lon=%.12g)\n", lat, lon);
            exit(NORMAL_ERROR);
        }
    }

    if (useXYZ) {
        double x;
        double y;
        double z;
        convertLatLonToXYZ(lat, lon, &x, &y, &z);
        printf("%d %.12g %.12g %.12g %.12g %.12g\n", nrResults, lat, lon, x, y, z);
    }
    else {
        printf("%d %.12g %.12g\n", nrResults, lat, lon);
    }
    for (int j = 0; j < nrResults; ++j) {
        const char *foundMapcode = results[(j * 2)];
        const char *foundTerritory = results[(j * 2) + 1];

        // Output result line.
        printf("%s %s\n", foundTerritory, foundMapcode);

        // Self-checking code to see if encoder produces this Mapcode for the lat/lon.
        if (selfCheckEnabled) {
            selfCheckLatLonToMapcode(lat, lon, foundTerritory, foundMapcode, extraDigits);
            selfCheckMapcodeToLatLon(foundTerritory, foundMapcode, lat, lon);
        }
    }

    // Add empty line.
    printf("\n");

    if (nrResults > largestNrOfResults) {
        largestNrOfResults = nrResults;
        latLargestNrOfResults = lat;
        lonLargestNrOfResults = lon;
    }
    totalNrOfResults += nrResults;
}


/**
 * This method resets the statistics counters.
 */
static void resetStatistics(int nrOfPoints) {
    totalNrOfPoints = nrOfPoints;
    largestNrOfResults = 0;
    latLargestNrOfResults = 0.0;
    lonLargestNrOfResults = 0.0;
}


/**
 * This method outputs the statistics.
 */
static void outputStatistics() {
    fprintf(stderr, "\nStatistics:\n");
    fprintf(stderr, "Total number of 3D points generated     = %d\n", totalNrOfPoints);
    fprintf(stderr, "Total number of mapcodes generated      = %d\n", totalNrOfResults);
    fprintf(stderr, "Average number of mapcodes per 3D point = %.12g\n",
            ((float) totalNrOfResults) / ((float) totalNrOfPoints));
    fprintf(stderr, "Largest number of results for 1 mapcode = %d at (%.12g, %.12g)\n",
            largestNrOfResults, latLargestNrOfResults, lonLargestNrOfResults);
}


/**
 * This method shows a progress indication.
 */
static void showProgress(int i) {
    fprintf(stderr, "[%d%%] Processed %d of %d regions (generated %d mapcodes)...\r",
            (int) ((((float) i / ((float) totalNrOfPoints)) * 100.0) + 0.5),
            i, totalNrOfPoints, totalNrOfResults);
}


/**
 * This is the main() method which is called from the command-line.
 * Return code 0 means success. Any other values means some sort of error occurred.
 */
int main(const int argc, const char **argv) {
    // Assume no extra digits (unless overridden later.
    int extraDigits = 0;

    // If XYZ is added to -b, -r or -g, print x, y, z coordinates
    int useXYZ = 0;

    // Provide usage message if no arguments specified.
    const char *appName = argv[0];
    selfCheckEnabled = (strstr(appName, "debug") != 0);
    if (selfCheckEnabled) {
        fprintf(stderr, "(debug mode: self checking enabled)\n");
    }
    if (argc < 2) {
        usage(appName);
        return NORMAL_ERROR;
    }

    // First argument: command.
    const char *cmd = argv[1];
    if ((strcmp(cmd, "-d") == 0) || (strcmp(cmd, "--decode") == 0)) {

        // ------------------------------------------------------------------
        // Decode: [-d | --decode] <default-territory> <mapcode> [<mapcode> ...]
        // ------------------------------------------------------------------
        if (argc < 4) {
            fprintf(stderr, "error: incorrect number of arguments\n\n");
            usage(appName);
            return NORMAL_ERROR;
        }

        const char *defaultTerritory = argv[2];
        double lat;
        double lon;

        // Get the territory context.
        int context = convertTerritoryIsoNameToCode(defaultTerritory, 0);

        // Decode every Mapcode.
        for (int i = 3; i < argc; ++i) {

            // Decode the Mapcode to a lat/lon.
            const char *mapcode = argv[i];
            int err = decodeMapcodeToLatLon(&lat, &lon, mapcode, context);
            if (err != 0) {
                fprintf(stderr, "error: cannot decode '%s %s'\n", defaultTerritory, mapcode);
                return NORMAL_ERROR;
            }

            // Output the decoded lat/lon.
            printf("%.12g %.12g\n", lat, lon);

            // Self-checking code to see if encoder produces this Mapcode for the lat/lon.
            if (selfCheckEnabled) {
                const char *suffix = strstr(mapcode, "-");
                extraDigits = 0;
                if (suffix != 0) {
                    extraDigits = (int) (strlen(suffix) - 1);
                }
                selfCheckLatLonToMapcode(lat, lon, defaultTerritory, mapcode, extraDigits);
            }
        }
    }
    else if ((strcmp(cmd, "-e") == 0) || (strcmp(cmd, "-e0") == 0) ||
             (strcmp(cmd, "-e1") == 0) || (strcmp(cmd, "-e2") == 0) ||
             (strcmp(cmd, "-e3") == 0) || (strcmp(cmd, "-e4") == 0) ||
             (strcmp(cmd, "-e5") == 0) || (strcmp(cmd, "-e6") == 0) ||
             (strcmp(cmd, "-e7") == 0) || (strcmp(cmd, "-e8") == 0) ||
             (strcmp(cmd, "--encode") == 0) || (strcmp(cmd, "--encode0") == 0) ||
             (strcmp(cmd, "--encode1") == 0) || (strcmp(cmd, "--encode2") == 0) ||
             (strcmp(cmd, "--encode3") == 0) || (strcmp(cmd, "--encode4") == 0) ||
             (strcmp(cmd, "--encode5") == 0) || (strcmp(cmd, "--encode5") == 0) ||
             (strcmp(cmd, "--encode7") == 0) || (strcmp(cmd, "--encode8") == 0)) {

        // ------------------------------------------------------------------
        // Encode: [-e[0-8] | --encode[0-8]] <lat:-90..90> <lon:-180..180> [territory]>
        // ------------------------------------------------------------------
        if ((argc != 4) && (argc != 5)) {
            fprintf(stderr, "error: incorrect number of arguments\n\n");
            usage(appName);
            return NORMAL_ERROR;
        }
        if ((!isdigit(*argv[2]) && (*argv[2] != '-')) || (!isdigit(*argv[3]) && (*argv[3] != '-'))) {
            fprintf(stderr, "error: latitude and longitude must be numeric\n");
            usage(appName);
            return NORMAL_ERROR;
        }
        const double lat = atof(argv[2]);
        const double lon = atof(argv[3]);

        if (strstr(cmd, "-e1") || strstr(cmd, "--encode1")) {
            extraDigits = 1;
        }
        else if (strstr(cmd, "-e2") || strstr(cmd, "--encode2")) {
            extraDigits = 2;
        }
        else if (strstr(cmd, "-e3") || strstr(cmd, "--encode3")) {
            extraDigits = 3;
        }
        else if (strstr(cmd, "-e4") || strstr(cmd, "--encode4")) {
            extraDigits = 4;
        }
        else if (strstr(cmd, "-e5") || strstr(cmd, "--encode5")) {
            extraDigits = 5;
        }
        else if (strstr(cmd, "-e6") || strstr(cmd, "--encode6")) {
            extraDigits = 6;
        }
        else if (strstr(cmd, "-e7") || strstr(cmd, "--encode7")) {
            extraDigits = 7;
        }
        else if (strstr(cmd, "-e8") || strstr(cmd, "--encode8")) {
            extraDigits = 8;
        }
        else {
            extraDigits = 0;
        }

        // Get territory context.
        int context = 0;
        const char *defaultTerritory = "AAA";
        if (argc == 5) {
            context = convertTerritoryIsoNameToCode(argv[4], 0);
            defaultTerritory = argv[4];
        }

        // Encode the lat/lon to a set of Mapcodes.
        char *results[2 * MAX_NR_OF_MAPCODE_RESULTS];
        const int nrResults = encodeLatLonToMapcodes_Deprecated(results, lat, lon, context, extraDigits);
        if (nrResults <= 0) {
            fprintf(stderr, "error: cannot encode lat=%.12g, lon=%.12g (default territory=%s)\n",
                    lat, lon, defaultTerritory);
            return NORMAL_ERROR;
        }

        // Output the Mapcode.
        for (int i = 0; i < nrResults; ++i) {
            const char *foundMapcode = results[(i * 2)];
            const char *foundTerritory = results[(i * 2) + 1];
            printf("%s %s\n", foundTerritory, foundMapcode);

            // Self-checking code to see if decoder produces the lat/lon for all of these Mapcodes.
            if (selfCheckEnabled) {
                selfCheckMapcodeToLatLon(foundTerritory, foundMapcode, lat, lon);
            }
        }
    }
    else if ((strcmp(cmd, "-b") == 0) || (strcmp(cmd, "-bXYZ") == 0) ||
             (strcmp(cmd, "--boundaries") == 0) || (strcmp(cmd, "--boundariesXYZ") == 0)) {

        // ------------------------------------------------------------------
        // Generate a test set based on the Mapcode boundaries.
        // ------------------------------------------------------------------
        if ((argc < 2) || (argc > 3)) {
            fprintf(stderr, "error: incorrect number of arguments\n\n");
            usage(appName);
            return NORMAL_ERROR;
        }
        if (argc == 3) {
            extraDigits = atoi(argv[2]);
            if ((extraDigits < 0) || (extraDigits > 8)) {
                fprintf(stderr, "error: parameter extraDigits must be in [0..8]\n\n");
                usage(appName);
                return NORMAL_ERROR;
            }
        }
        useXYZ = (strstr(cmd, "XYZ") != 0);

        resetStatistics(NR_BOUNDARY_RECS);
        for (int i = 0; i < totalNrOfPoints; ++i) {
            double minLon;
            double maxLon;
            double minLat;
            double maxLat;
            double lat;
            double lon;

            const mminforec *mm = boundaries(i);
            minLon = ((double) mm->minx) / 1.0E6;
            maxLon = ((double) mm->maxx) / 1.0E6;
            minLat = ((double) mm->miny) / 1.0E6;
            maxLat = ((double) mm->maxy) / 1.0E6;

            // Try center.
            lat = (maxLat - minLat) / 2.0;
            lon = (maxLon - minLon) / 2.0;
            generateAndOutputMapcodes(lat, lon, 0, extraDigits, useXYZ);

            // Try corners.
            generateAndOutputMapcodes(minLat, minLon, 0, extraDigits, useXYZ);
            generateAndOutputMapcodes(minLat, maxLon, 0, extraDigits, useXYZ);
            generateAndOutputMapcodes(maxLat, minLon, 0, extraDigits, useXYZ);
            generateAndOutputMapcodes(maxLat, maxLon, 0, extraDigits, useXYZ);

            // Try JUST inside.
            const double d = 0.000001;
            generateAndOutputMapcodes(minLat + d, minLon + d, 0, extraDigits, useXYZ);
            generateAndOutputMapcodes(minLat + d, maxLon - d, 0, extraDigits, useXYZ);
            generateAndOutputMapcodes(maxLat - d, minLon + d, 0, extraDigits, useXYZ);
            generateAndOutputMapcodes(maxLat - d, maxLon - d, 0, extraDigits, useXYZ);

            // Try JUST outside.
            generateAndOutputMapcodes(minLat - d, minLon - d, 0, extraDigits, useXYZ);
            generateAndOutputMapcodes(minLat - d, maxLon + d, 0, extraDigits, useXYZ);
            generateAndOutputMapcodes(maxLat + d, minLon - d, 0, extraDigits, useXYZ);
            generateAndOutputMapcodes(maxLat + d, maxLon + d, 0, extraDigits, useXYZ);

            if ((i % SHOW_PROGRESS) == 0) {
                showProgress(i);
            }
        }
        outputStatistics();
    }
    else if ((strcmp(cmd, "-g") == 0) || (strcmp(cmd, "-gXYZ") == 0) ||
             (strcmp(cmd, "--grid") == 0) || (strcmp(cmd, "--gridXYZ") == 0) ||
             (strcmp(cmd, "-r") == 0) || (strcmp(cmd, "-rXYZ") == 0) ||
             (strcmp(cmd, "--random") == 0) || (strcmp(cmd, "--randomXYZ") == 0)) {

        // ------------------------------------------------------------------
        // Generate grid test set:    [-g | --grid]   <nrOfPoints> [<extradigits>]
        // Generate uniform test set: [-r | --random] <nrOfPoints> [<seed>]
        // ------------------------------------------------------------------
        if ((argc < 3) || (argc > 5)) {
            fprintf(stderr, "error: incorrect number of arguments\n\n");
            usage(appName);
            return NORMAL_ERROR;
        }
        int nrOfPoints = atoi(argv[2]);
        if (nrOfPoints < 1) {
            fprintf(stderr, "error: total number of points to generate must be >= 1\n\n");
            usage(appName);
            return NORMAL_ERROR;
        }
        if (argc >= 4) {
            extraDigits = atoi(argv[3]);
            if ((extraDigits < 0) || (extraDigits > 8)) {
                fprintf(stderr, "error: parameter extraDigits must be in [0..8]\n\n");
                usage(appName);
                return NORMAL_ERROR;
            }
        }
        int random = (strcmp(cmd, "-r") == 0) || (strcmp(cmd, "--random") == 0);
        if (random) {
            if (argc == 5) {
                const int seed = atoi(argv[4]);
                srand((unsigned int) seed);
            }
            else {
                srand((unsigned int) time(0));
            }
        }
        useXYZ = (strstr(cmd, "XYZ") != 0);

        // Statistics.
        resetStatistics(nrOfPoints);

        int gridX = 0;
        int gridY = 0;
        int line = my_round(sqrt((double) totalNrOfPoints));
        for (int i = 0; i < totalNrOfPoints; ++i) {
            double lat;
            double lon;
            double unit1;
            double unit2;

            if (random) {
                unit1 = ((double) rand()) / RAND_MAX;
                unit2 = ((double) rand()) / RAND_MAX;
            }
            else {
                unit1 = ((double) gridX) / line;
                unit2 = ((double) gridY) / line;

                if (gridX < line) {
                    ++gridX;
                }
                else {
                    gridX = 0;
                    ++gridY;
                }
            }

            unitToLatLonDeg(unit1, unit2, &lat, &lon);
            generateAndOutputMapcodes(lat, lon, 1, extraDigits, useXYZ);

            if ((i % SHOW_PROGRESS) == 0) {
                showProgress(i);
            }
        }
        outputStatistics();
    }
    else {

        // ------------------------------------------------------------------
        // Usage.
        // ------------------------------------------------------------------
        usage(appName);
        return NORMAL_ERROR;
    }
    return 0;
}
