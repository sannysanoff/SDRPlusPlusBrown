#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "gpx.hpp"

#define GPX_TIME_FORMAT "%Y-%m-%dT%H:%M:%SZ"

bool GPXWriter::init(const char* fname) {
    if (!fname || !fname[0]) { return false; }
    if (m_fd) { deinit(); }

    m_fd = fopen(fname, "wb");
    if (!m_fd) { return false; }

    m_lat = m_lon = m_alt = 0.0f;
    m_time = 0;
    m_trackActive = false;
    sondeSerial[0] = '\0';

    fprintf(m_fd,
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\" ?>\n"
            "<gpx xmlns=\"http://www.topografix.com/GPX/1/1\" version=\"1.1\" creator=\"SDR++\">\n");
    long pos = ftell(m_fd);
    m_offset = (pos >= 0) ? pos : 0;
    terminateFile();
    return true;
}

void GPXWriter::deinit() {
    if (!m_fd) { return; }
    terminateFile();
    fclose(m_fd);
    m_fd = nullptr;
    m_trackActive = false;
}

void GPXWriter::startTrack(const char* name) {
    if (!m_fd || !name || !name[0]) { return; }
    if (m_trackActive && !strcmp(name, sondeSerial)) { return; }
    for (int i = 0; name[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (!isgraph(ch) || ch == '<' || ch == '>' || ch == '&') { return; }
    }

    if (m_trackActive) {
        stopTrack();
    }

    snprintf(sondeSerial, sizeof(sondeSerial), "%s", name);

    fseek(m_fd, m_offset, SEEK_SET);
    fprintf(m_fd, "<trk>\n<name>%s</name>\n<trkseg>\n", sondeSerial);
    long pos = ftell(m_fd);
    if (pos >= 0) { m_offset = pos; }
    m_trackActive = true;

    terminateFile();
}

void GPXWriter::stopTrack() {
    if (!m_fd || !m_trackActive) { return; }
    stopTrackInternal();
    m_trackActive = false;
    terminateFile();
}

void GPXWriter::addTrackPoint(time_t time, float lat, float lon, float alt, float spd, float hdg) {
    if (!m_fd || !m_trackActive) { return; }
    if (!std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(alt)) { return; }
    if (lat == 0.0f && lon == 0.0f && alt == 0.0f) { return; }
    if (time == m_time || (lat == m_lat && lon == m_lon && alt == m_alt)) { return; }

    struct tm* tmInfo = gmtime(&time);
    if (!tmInfo) { return; }
    char timestr[sizeof("YYYY-MM-DDThh:mm:ssZ") + 8];
    if (!strftime(timestr, sizeof(timestr), GPX_TIME_FORMAT, tmInfo)) { return; }

    m_lat = lat;
    m_lon = lon;
    m_alt = alt;
    m_time = time;

    fseek(m_fd, m_offset, SEEK_SET);
    fprintf(m_fd, "<trkpt lat=\"%f\" lon=\"%f\">\n", lat, lon);
    fprintf(m_fd, "<time>%s</time>\n", timestr);
    fprintf(m_fd, "<ele>%f</ele>\n", alt);
    fprintf(m_fd, "<speed>%f</speed>\n", std::isfinite(spd) ? spd : 0.0f);
    fprintf(m_fd, "<course>%f</course>\n", std::isfinite(hdg) ? hdg : 0.0f);
    fprintf(m_fd, "</trkpt>\n");
    long pos = ftell(m_fd);
    if (pos >= 0) { m_offset = pos; }

    terminateFile();
}

void GPXWriter::terminateFile() {
    long offset = m_offset;
    if (!m_fd) { return; }

    fseek(m_fd, m_offset, SEEK_SET);
    if (m_trackActive) { stopTrackInternal(); }

    fprintf(m_fd, "</gpx>\n");
    fflush(m_fd);
    m_offset = offset;
}

void GPXWriter::stopTrackInternal() {
    if (!m_fd) { return; }
    fseek(m_fd, m_offset, SEEK_SET);
    fprintf(m_fd, "</trkseg>\n</trk>\n");
    long pos = ftell(m_fd);
    if (pos >= 0) { m_offset = pos; }
}
