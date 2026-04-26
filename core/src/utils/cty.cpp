#define _USE_MATH_DEFINES
#include <cmath>
#include <core.h>
#include "cty.h"
#include <fstream>
#include <utils/strings.h>

namespace utils {

    static bool isSomeWeirdName(const std::string &basicString) {
        int countDashes = 0;
        for(auto c: basicString) {
            if (c == '-') {
                countDashes++;
            }
        }
        return countDashes > 1;
    }


    static bool isValidLocatorString(const std::string &locatorString) {
        return tolower(locatorString[0]) >= 'a' && tolower(locatorString[0]) <= 'r' && tolower(locatorString[1]) >= 'a' && tolower(locatorString[1]) <= 'r'
               && locatorString[2] >= '0' && locatorString[2] <= '9' && locatorString[3] >= '0' && locatorString[3] <= '9'
               && (locatorString.length() == 4 || (locatorString.length() == 6 && locatorString[4] >= 'a' && locatorString[4] <= 'x' && locatorString[5] >= 'a' && locatorString[5] <= 'x'));
    }

    static const int CHAR_CODE_OFFSET = 65;


    static int charToNumber(char c) {
        return std::toupper(c) - CHAR_CODE_OFFSET;
    }



    LatLng gridToLatLng(std::string locatorString) {
        if (locatorString.length() == 4) {
            locatorString = locatorString + "ll";
        }
        if (locatorString.length() > 6) {
            locatorString = locatorString.substr(0, 6);
        }
        if (!isValidLocatorString(locatorString)) {
            return LatLng::invalid();
        }

        auto fieldLng = charToNumber(locatorString[0]) * 20;
        auto fieldLat = charToNumber(locatorString[1]) * 10;
        auto squareLng = std::stod(std::string()+locatorString[2]) * 2;
        auto squareLat = std::stod(std::string()+locatorString[3]);
        auto subsquareLng = (charToNumber(locatorString[4]) + 0.5) / 12;
        auto subsquareLat = (charToNumber(locatorString[5]) + 0.5) / 24;

        LatLng ll;
        ll.lat = fieldLat + squareLat + subsquareLat - 90;
        ll.lon = fieldLng + squareLng + subsquareLng - 180;
        return ll;
    }

    static double degToRad(double d) {
        while(d < 0) {
            d += 360;
        }
        while(d >= 360) {
            d -= 360;
        }
        return d * M_PI / 180;
    }

    static double radToDeg(double rad) {
        double d = rad / M_PI * 180;
        while(d < 0) {
            d += 360;
        }
        while(d >= 360) {
            d -= 360;
        }
        return d;
    }

    BearingDistance bearingDistance(LatLng fromCoords, LatLng toCoords) {
        // fi = lat
        // lam = lon
        auto dLat = degToRad(toCoords.lat - fromCoords.lat); // dfi
        auto dLon = degToRad(toCoords.lon - fromCoords.lon); // dlam
        auto fromLat = degToRad(fromCoords.lat); // fi 1
        auto toLat = degToRad(toCoords.lat); // fi 2
        auto a = pow(sin(dLat / 2), 2) + pow(sin(dLon / 2), 2) * cos(fromLat) * cos(toLat);
        if (a < 0) {
            a = 0;
        }
        if (a > 1) {
            a = 1;
        }
        auto b = 2 * atan2(sqrt(a), sqrt(1 - a));

        auto y = sin(dLon) * cos(toLat);
        auto x = cos(fromLat) * sin(toLat) - sin(fromLat) * cos(toLat) * cos(dLon);

        auto az = atan2(y, x);

        if (az < 0) {
            az += 2 * M_PI;
        }

        return { az, b * 6371};
    }

    static std::string getText(const std::string &txt, int &i, char end) {
        std::string part;
        i++;
        for(i++; i<txt.length(); i++) {
            if (txt[i] == end) {
                break;
            }
            part += txt[i];
        }
        return part;
    }


    static void parseCallsign(const std::string &txt, CTY::Callsign* pCallsign) {
        std::string part;
        std::vector<std::string> parts;
        int i=0;
        if (txt.empty()) {
            return;
        }
        if (txt[i] == '=') {
            i++;
            pCallsign->exact = true;
        } else {
        }
        bool endValue = false;
        for(; i<txt.length(); i++) {
            switch(txt[i]) {
            case '{':
                endValue = true;
                part = getText(txt, i, '}');
                pCallsign->continent = part;
                break;
            case '<':
                part = getText(txt, i, '>');
                splitStringV(part, "/", parts);
                if (parts.size() == 2) {
                    pCallsign->ll.lat = std::stod(parts[0]);
                    pCallsign->ll.lon = -std::stod(parts[1]);
                }
                endValue = true;
                continue;
            case '[': // ITU
                getText(txt, i, ']');
                endValue = true;
                continue;
            case '(': // CQ
                getText(txt, i, ')');
                endValue = true;
                continue;
            }
            if (!endValue) {
                pCallsign->value += txt[i];
            }
        }
    }


    void loadCTY(const char * filename, const char *region, CTY& cty) {
        bool excludeWeirdThings = strlen(region);
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error(std::string("Unable to open CTY file: ")+filename);
        }
        std::string line;
        std::vector<std::string> lineSplit;
        bool isWeirdSection = false;
        while (std::getline(file, line)) {
            if (line.empty()) continue;
            if (line[0] != ' ') {
                splitStringV(line, ":", lineSplit);
                for(auto &s: lineSplit) {
                    trimString(s);
                }
                if (lineSplit.size() >= 8) {
                    auto& locName = lineSplit[0];
                    isWeirdSection = isSomeWeirdName(locName);
                    if (!excludeWeirdThings || !isWeirdSection) {
                        cty.dxcc.emplace_back(CTY::DXCC{ std::stod(lineSplit[4]), -std::stod(lineSplit[5]), locName + region, lineSplit[3] });
                    }
                }
                continue;
            }
            if (!excludeWeirdThings || !isWeirdSection) {
                splitStringV(line, " ,;\r\n", lineSplit);
                for (auto s : lineSplit) {
                    trimString(s);
                    if (!cty.dxcc.empty()) {
                        CTY::Callsign cs;
                        parseCallsign(s, &cs);
                        if (cs.value.empty()) {
                            continue;
                        }
                        cty.dxcc.back().prefixes.emplace_back(cs);
                    }
                }
            }
        }
        file.close();
    }

    CTY::Callsign CTY::findCallsign(const std::string& callsign) const {
        bool found = false;
        CTY::Callsign rv;
        for(auto & dxcc1 : dxcc) {
            for(auto &prefix: dxcc1.prefixes) {
                if (prefix.exact) {
                    if (callsign == prefix.value) {
                        rv = prefix;
                        rv.ll = dxcc1.ll;
                        rv.continent = dxcc1.continent;
                        rv.dxccname = dxcc1.name;
                        found = true;
                    }
                }
            }
        }
        if (found) { // exact
            return rv;
        }
        for(auto & dxcc1 : dxcc) {
            for(auto &prefix: dxcc1.prefixes) {
                if (!prefix.exact) {
                    if (callsign.find(prefix.value) == 0)  {    // last one is more important
                        if (prefix.value.length() >= rv.value.length()) {
                            rv = prefix;
                            rv.ll = dxcc1.ll;
                            rv.continent = dxcc1.continent;
                        }
                        if (rv.dxccname.empty() || !isSomeWeirdName(dxcc1.name)) {
                            rv.dxccname = dxcc1.name;
                        }
                    }
                }
            }
        }
        return rv;
    }

    CTY globalCty;

    void loadAllCty() {
        std::string resDir = core::getResourcesDirectory();
        loadCTY((resDir + "/cty/cty.dat").c_str(), "", globalCty);
        loadCTY((resDir + "/cty/AF_cty.dat").c_str(), ", AF", globalCty);
        loadCTY((resDir + "/cty/BY_cty.dat").c_str(), ", CN", globalCty);
        loadCTY((resDir + "/cty/EU_cty.dat").c_str(), ", EU", globalCty);
        loadCTY((resDir + "/cty/NA_cty.dat").c_str(), ", NA", globalCty);
        loadCTY((resDir + "/cty/SA_cty.dat").c_str(), ", SA", globalCty);
        loadCTY((resDir + "/cty/VK_cty.dat").c_str(), ", VK", globalCty);
        loadCTY((resDir + "/cty/cty_rus.dat").c_str(), ", RUS", globalCty);

    }


}