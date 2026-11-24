#pragma once

/*
 * Adapted from molgino/synth-city, src/main.cpp at 7f2258f.
 * Copyright (c) 2026 NOXED
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <dirent.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

// Battery gauge from the original Synth City R36S implementation.
struct Battery {
    int percent = -1, chipPercent = -1, mv = 0, ma = 0;
    bool plugged = false, full = false;

    Battery() = default;
    Battery(const Battery&) = delete;
    Battery& operator=(const Battery&) = delete;
    ~Battery() { if (dcFd >= 0) close(dcFd); }

    // A custom sysfs root keeps fixtures off the physical charger pin.
    void poll(uint32_t now, const char* sysfsOverride = nullptr) {
        if (readOnce && now - lastPoll < 5000) return;
        const bool first = !readOnce;
        readOnce = true;
        lastPoll = now;
        if (first) scan(sysfsOverride ? sysfsOverride : "/sys/class/power_supply");
        if (!path[0]) return;

        long value;
        chipPercent = readInt(path, "capacity", value) && value >= 0 && value <= 100 ? (int)value : -1;
        mv = readInt(path, "voltage_now", value) ? (int)(value / 1000) : 0;
        if (mv <= 0 && readInt(path, "voltage_avg", value)) mv = (int)(value / 1000);
        ma = readInt(path, "current_now", value) ? (int)(value / 1000) : 0;
        char status[32];
        readLine(path, "status", status, sizeof status);
        status[strcspn(status, "\r\n")] = 0;

        bool online = false;
        for (int i = 0; i < chargerCount; i++) {
            if (readInt(chargers[i], "online", value) && value > 0) online = true;
        }
        const int dc = sysfsOverride ? -1 : readDcDetect();
        if (dc >= 0) {
            if (ma < -150) idleLevel = dc;
            else if (ma > 150) idleLevel = !dc;
        }
        full = !strcmp(status, "Full");
        // "Not charging" alone does not signal a connected charger on the R36S.
        plugged = full || !strcmp(status, "Charging") || online || (dc >= 0 && dc != idleLevel) || ma > 50;
        if (mv > 2500 && mv < 4600) {
            const float current = fabsf((float)ma) / 1000.f;
            const float compensation = current <= 3.f ? 0.15f * current : 0.f;
            const float charge = voltagePercent(mv / 1000.f + (plugged ? -compensation : compensation));
            if (estimate < 0.f) estimate = charge;
            else if (plugged) {
                if (charge > estimate) estimate += 0.05f * (charge - estimate);
            } else estimate += (charge > estimate ? 0.01f : 0.1f) * (charge - estimate);
            if (full) estimate = 100.f;
            percent = (int)(estimate + 0.5f);
        } else percent = chipPercent;

        if (first || now - lastLog >= 60000) {
            lastLog = now;
            printf("Akku: %d %% (Treiber %d %%), %d mV, %d mA, Status %s, Netz %s, dc-det %d, Lade-Eingang %s\n",
                   percent, chipPercent, mv, ma, status[0] ? status : "?", plugged ? "an" : "aus", dc,
                   online ? "online" : "-");
        }
    }

private:
    bool readOnce = false;
    uint32_t lastPoll = 0, lastLog = 0;
    int dcFd = -2, idleLevel = 0;
    float estimate = -1.f;
    char path[512] = "", chargers[8][512];
    int chargerCount = 0;

    static bool readLine(const char* dir, const char* name, char* out, size_t size) {
        out[0] = 0;
        char namePath[544];
        if (snprintf(namePath, sizeof namePath, "%s/%s", dir, name) >= (int)sizeof namePath) return false;
        FILE* f = fopen(namePath, "r");
        if (!f) return false;
        const bool ok = fgets(out, size, f) != nullptr;
        fclose(f);
        if (!ok) out[0] = 0;
        return ok;
    }
    static bool readInt(const char* dir, const char* name, long& value) {
        char b[40];
        if (!readLine(dir, name, b, sizeof b)) return false;
        char* end;
        value = strtol(b, &end, 10);
        return end != b;
    }
    void scan(const char* root) {
        DIR* dir = opendir(root);
        if (!dir) return;
        while (const dirent* entry = readdir(dir)) {
            if (entry->d_name[0] == '.') continue;
            char supply[sizeof path], type[32];
            if (snprintf(supply, sizeof supply, "%s/%s", root, entry->d_name) >= (int)sizeof supply) continue;
            if (!readLine(supply, "type", type, sizeof type)) continue;
            if (!path[0] && !strncmp(type, "Battery", 7)) strcpy(path, supply);
            else if (chargerCount < 8 && (!strncmp(type, "Mains", 5) || !strncmp(type, "USB", 3)))
                strcpy(chargers[chargerCount++], supply);
        }
        closedir(dir);
    }
    static float voltagePercent(float voltage) {
        static const float curve[][2] = {
            {3.30f, 0}, {3.61f, 5}, {3.69f, 10}, {3.71f, 15}, {3.73f, 20}, {3.75f, 25},
            {3.77f, 30}, {3.79f, 35}, {3.80f, 40}, {3.82f, 45}, {3.84f, 50}, {3.85f, 55},
            {3.87f, 60}, {3.91f, 65}, {3.95f, 70}, {3.98f, 75}, {4.02f, 80}, {4.08f, 85},
            {4.11f, 90}, {4.15f, 95}, {4.19f, 100}
        };
        if (voltage <= curve[0][0]) return 0.f;
        for (unsigned i = 1; i < sizeof curve / sizeof *curve; i++) {
            if (voltage >= curve[i][0]) continue;
            const float t = (voltage - curve[i - 1][0]) / (curve[i][0] - curve[i - 1][0]);
            return curve[i - 1][1] + t * (curve[i][1] - curve[i - 1][1]);
        }
        return 100.f;
    }
    int readDcDetect() {
        // GPIO0_B3 reads the separate R36S charging socket on kernels without a supply entry.
        if (dcFd == -2) {
            dcFd = -1;
            for (int i = 0; i < 8 && dcFd < 0; i++) {
                char name[32];
                snprintf(name, sizeof name, "/dev/gpiochip%d", i);
                const int fd = open(name, O_RDONLY | O_CLOEXEC);
                if (fd < 0) continue;
                gpiochip_info info{};
                if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0 &&
                    (!strcmp(info.label, "gpio0") || strstr(info.label, "ff040000"))) {
                    gpiohandle_request req{};
                    req.lineoffsets[0] = 11;
                    req.flags = GPIOHANDLE_REQUEST_INPUT;
                    req.lines = 1;
                    snprintf(req.consumer_label, sizeof req.consumer_label, "groovebox-dcdet");
                    if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &req) == 0) dcFd = req.fd;
                }
                close(fd);
            }
        }
        gpiohandle_data state{};
        return dcFd >= 0 && ioctl(dcFd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &state) == 0 ? state.values[0] : -1;
    }
};
