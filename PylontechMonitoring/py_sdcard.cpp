#include "py_sdcard.h"
#include "config.h"
#include "py_scheduler.h"
#include "py_parser_pwr.h"
#include "py_parser_bat.h"
#include "py_log.h"
#include <ArduinoJson.h>
#include <time.h>

PySdCard sdCard;

// VSPI pins for SD card on CYD (separate from display HSPI)
// VSPI (default SPI): MOSI=23, MISO=19, SCLK=18, CS=5
// Display uses HSPI (SPI3): MOSI=13, MISO=12, SCLK=14, CS=15
// Separate hardware SPI buses - no mutex needed
static const int SD_CS_PIN = 5;
static const int SD_MOSI = 23;
static const int SD_MISO = 19;
static const int SD_SCLK = 18;

// Minimum spacing between two CSV rows of the same kind. The row rate is already
// bounded by the poll interval, this only guards against a misconfigured
// interval (0) turning the card into a write-ammo dump.
static const unsigned long SD_MIN_ROW_INTERVAL_MS = 1000;

// Free space is re-read at most this often (loop() is throttled; the web status
// endpoint shares the same throttle).
static const unsigned long SD_SPACE_REFRESH_MS = 5000;

// A full card must stop the export instead of silently dropping rows. Free
// space is reported in whole clusters (f_getfree), so "no free cluster left" is
// what makes the next row impossible. The margin is a stale-reading guard, not a
// reserve: for clusters of 4 KB and up it is equivalent to zero, and for smaller
// clusters it merely stops a few clusters early.
static const uint64_t SD_FULL_MIN_FREE_BYTES = 4096;

// Datalist chunks buffered for the Task 1 -> Task 2 handoff. The queue now
// carries streamed slices instead of one whole response, so it is deeper.
static const int SD_DATALIST_QUEUE_LEN = 32;

// Bounded block time for the producer's queue send: Task 1 waits (and yields)
// while Task 2 drains the queue, giving backpressure instead of drops.
static const unsigned long SD_DATALIST_SEND_TIMEOUT_MS = 500;

// ---------------------------------------------------------
// Wall-clock stamp for a CSV row / file name. Falls back to the raw epoch time
// while the clock is still invalid (before the first NTP sync), so an export
// never produces a blank timestamp or a "/Module_.csv" file name.
// ---------------------------------------------------------
static String epochStamp(const char* format) {
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    char buf[32];
    strftime(buf, sizeof(buf), format, &t);
    return String(buf);
}

static String rowTimestamp() {
    String dt = config.getCurrentDateTimeString();
    if (dt.length() > 0) return dt;
    return epochStamp("%Y-%m-%d %H:%M:%S");
}

bool PySdCard::initSD() {
    // Use VSPI (default SPI) for SD card - separate from display HSPI
    // Default VSPI pins: MOSI=23, MISO=19, SCLK=18, CS=5
    if (!SD.begin(SD_CS_PIN)) {
        Log(LOG_ERROR, "SD: Mount failed on VSPI");
        return false;
    }

    // Verify FAT32 filesystem
    if (SD.cardType() == CARD_NONE) {
        Log(LOG_ERROR, "SD: No card detected");
        return false;
    }

    // SD library doesn't expose fsType() directly; we assume FAT32 for SDHC/SDXC
    totalBytes = SD.totalBytes();
    usedBytes = SD.usedBytes();
    lastSpaceCheckMs = millis();
    sdReady = true;

    Log(LOG_INFO, "SD: Ready, total=" + String(totalBytes) + ", used=" + String(usedBytes) +
        ", free=" + String(totalBytes > usedBytes ? totalBytes - usedBytes : 0));

    return true;
}

void PySdCard::begin() {
    initSD();

    // Cross-task handoff for datalist responses. Created here (setup(), before
    // the FreeRTOS tasks start) so both tasks see a valid handle.
    if (datalistQueue == nullptr) {
        datalistQueue = xQueueCreate(SD_DATALIST_QUEUE_LEN, sizeof(DatalistChunk));
        if (datalistQueue == nullptr) {
            Log(LOG_ERROR, "SD: datalist queue creation failed");
        }
    }
}

void PySdCard::loop() {
    if (!sdReady) return;

    // Free space must follow the card, not the value read at mount time.
    updateSpaceUsage(false);

    // A scheduled data export still needs to be serviced (Stop, start/end of
    // the window), so SCHEDULED is not an idle state here.
    if (exportState != ExportState::RUNNING && exportState != ExportState::SCHEDULED) return;

    // Check stop request (must also cancel a scheduled data export)
    if (stopRequested) {
        Log(LOG_INFO, "SD: Export stop requested");
        finishExport(ExportState::STOPPED);
        return;
    }

    // Export timeout applies to the History export only. Data exports
    // are bounded by their From/To window and by Stop, never by a timeout.
    if (exportKind == ExportKind::KIND_HISTORY) {
        checkTimeout();
        if (exportState != ExportState::RUNNING) {
            finishExport(ExportState::TIMEOUT);
            return;
        }
    }

    bool progress = false;

    switch (exportKind) {
        case ExportKind::KIND_DATA: {
            // Schedule window: finish once To is reached, wait silently until
            // From is reached, otherwise write rows. Only the SCHEDULED ->
            // RUNNING transition is allowed here, so a backward clock
            // correction can never send a live export back to waiting.
            uint32_t nowEpoch = (uint32_t)time(nullptr);
            if (timeTo > 0 && nowEpoch >= timeTo) {
                Log(LOG_INFO, "SD: Data export reached its end time");
                finishExport(ExportState::COMPLETED);
                return;
            }
            if (exportState == ExportState::SCHEDULED) {
                if (timeFrom == 0 || nowEpoch >= timeFrom) {
                    exportState = ExportState::RUNNING;
                } else {
                    vTaskDelay(10);
                    return;
                }
            }

            // One session can cover both kinds; each writes its own per-module
            // file on its own pacing interval. Once one writer has failed the
            // session the other must not run again.
            if (sessionPwr && writePwrFrames()) progress = true;
            if (sessionBat && exportState == ExportState::RUNNING && writeBatFrames()) progress = true;
            break;
        }

        case ExportKind::KIND_HISTORY:
            progress = drainDatalistQueue();
            break;

        default:
            break;
    }

    // If no progress was made this cycle, yield
    if (!progress) {
        vTaskDelay(10);
    }
}

// ---------------------------------------------------------
// PWR / BAT exports
//
// The double buffers only ever hold the *latest* frame, so a row is written
// when (and only when) the parser published a new one (frame sequence counter)
// and the configured Battery data / Cells query interval has elapsed.
// ---------------------------------------------------------
unsigned long PySdCard::pwrIntervalMs() const {
    unsigned long ms = config.battery.intervalPwr;
    return ms < SD_MIN_ROW_INTERVAL_MS ? SD_MIN_ROW_INTERVAL_MS : ms;
}

unsigned long PySdCard::batIntervalMs() const {
    unsigned long ms = config.battery.intervalBat;
    return ms < SD_MIN_ROW_INTERVAL_MS ? SD_MIN_ROW_INTERVAL_MS : ms;
}

bool PySdCard::moduleSelected(int index) const {
    if (exportModules.empty()) return true;
    for (int mod : exportModules) {
        if (mod == index) return true;
    }
    return false;
}

bool PySdCard::writePwrFrames() {
    uint32_t seq = pwrFrameSeq;
    if (seq == lastPwrSeq) return false;   // no new frame yet

    unsigned long now = millis();
    if (lastPwrWriteMs != 0 && (now - lastPwrWriteMs) < pwrIntervalMs()) {
        // Paced: keep the sequence unconsumed so the next frame is exported as
        // soon as the interval has elapsed.
        return false;
    }
    lastPwrWriteMs = now;
    lastPwrSeq = seq;

    PwrBuffer* pwr = pwrUseA ? &pwrA : &pwrB;
    String dt = rowTimestamp();
    bool progress = false;

    for (const auto& m : pwr->modules) {
        if (!m.present) continue;
        if (!moduleSelected(m.index)) continue;

        // Checked before every row, not once per cycle: a single row can consume
        // the last free cluster, and the rows behind it in the same cycle would
        // then be buffered, counted and lost. Stopping here also means the row is
        // never attempted, so no truncated CSV line is left behind.
        updateSpaceUsage(true);
        if (freeBytes() < SD_FULL_MIN_FREE_BYTES) {
            failExport("SD card full");
            break;
        }

        String row = buildPwrRow(m, dt);
        if (appendRow(m.index, ExportType::EXPORT_PWR, row)) {
            progress = true;
        } else if (exportState != ExportState::RUNNING) {
            // appendRow() already failed the session; leave the rest alone.
            break;
        }

        int slot = csvSlot(ExportType::EXPORT_PWR, m.index);
        if ((uint32_t)csvRecords[slot] >= sdCardConfig.recordsPerFile) {
            rotateFile(m.index, ExportType::EXPORT_PWR);
        }

        vTaskDelay(1);
    }

    if (progress) updateSpaceUsage(true);
    return progress;
}

bool PySdCard::writeBatFrames() {
    // Never fabricate BAT rows while the Bat parser is switched off: the buffer
    // would just repeat the last (stale) frame over and over.
    if (!config.battery.enableBat) return false;

    uint32_t seq = batFrameSeq;
    if (seq == lastBatSeq) return false;   // no new frame yet

    // One BAT frame carries exactly ONE module's cells: the scheduler sends a
    // separate `bat N` per present module, unlike PWR where a single frame holds
    // the whole stack. The interval therefore has to be tracked per module - a
    // shared timestamp let the first module claim it for the whole cycle, so only
    // whichever module happened to be in the buffer when the gate reopened (the
    // last one polled) ever got a file.
    BatBuffer* bat = batUseA ? &batA : &batB;
    if (bat->cells.empty()) return false;

    int frameModule = bat->cells[0].moduleIndex;
    if (frameModule < 0 || frameModule >= SD_EXPORT_MAX_FILES) {
        Log(LOG_WARN, "SD: BAT frame with invalid module index " + String(frameModule));
        return false;
    }

    // lastBatSeq stays unconsumed while the gate is closed, so a _bat N_ that
    // arrives a fraction early is retried when the gate opens instead of costing
    // that module a whole poll cycle.
    unsigned long now = millis();
    if (lastBatWriteMs[frameModule] != 0 &&
        (now - lastBatWriteMs[frameModule]) < batIntervalMs()) {
        return false;
    }
    lastBatWriteMs[frameModule] = now;
    lastBatSeq = seq;

    String dt = rowTimestamp();
    bool progress = false;

    // One BAT frame carries ONE module's 15 cells: write the whole frame behind
    // a single open/close instead of one per cell. Each SD.open allocates a
    // 4096-byte stdio buffer, so opening once removes 14 of the 15 allocations
    // per frame and the heap churn that fragmented the heap.
    File f;
    bool opened = false;

    for (const auto& c : bat->cells) {
        if (!moduleSelected(c.moduleIndex)) continue;

        // Same per-row guard as the PWR writer (see writePwrFrames).
        updateSpaceUsage(true);
        if (freeBytes() < SD_FULL_MIN_FREE_BYTES) {
            failExport("SD card full");
            break;
        }

        if (!opened) {
            if (!openCsvForAppend(c.moduleIndex, ExportType::EXPORT_BAT, f)) return false;
            opened = true;
        }

        String row = buildBatRow(c, bat->header, dt);
        if (!writeCsvRow(f, c.moduleIndex, ExportType::EXPORT_BAT, row)) {
            f.close();
            return false;
        }
        progress = true;

        int slot = csvSlot(ExportType::EXPORT_BAT, c.moduleIndex);
        if ((uint32_t)csvRecords[slot] >= sdCardConfig.recordsPerFile) {
            f.close();
            rotateFile(c.moduleIndex, ExportType::EXPORT_BAT);
            if (!openCsvForAppend(c.moduleIndex, ExportType::EXPORT_BAT, f)) return false;
            opened = true;
        }

        vTaskDelay(1);
    }

    if (opened) f.close();

    if (progress) updateSpaceUsage(true);
    return progress;
}

// ---------------------------------------------------------
// One CSV file per (module, kind). A short-lived handle is opened for one batch
// and closed immediately after: the PWR writer opens one per row, the BAT writer
// opens one per frame and pushes that frame's 15 rows through it. No handle is
// held between batches (FATFS only provides a few file objects). Every row is
// appended; the CSV header is written only when the file is still empty.
// ---------------------------------------------------------
int PySdCard::csvSlot(ExportType type, int module) const {
    // PWR uses the lower half of the arrays, BAT the upper half, so one session
    // can keep both kinds for the same module apart.
    return (type == ExportType::EXPORT_BAT) ? module + SD_EXPORT_MAX_FILES : module;
}

// Create the file name lazily on first use, then open it in append mode. Leaves
// the handle open (and the header written) on success; the caller must close it.
bool PySdCard::openCsvForAppend(int module, ExportType type, File& f) {
    if (module < 0 || module >= SD_EXPORT_MAX_FILES) {
        Log(LOG_ERROR, "SD: Invalid module index " + String(module));
        return false;
    }

    int slot = csvSlot(type, module);

    if (csvFilenames[slot].length() == 0) {
        csvFilenames[slot] = buildFilename(type, module, 1);
        csvRecords[slot] = 0;
        csvFileNumbers[slot] = 1;
        Log(LOG_INFO, "SD: Export file created " + csvFilenames[slot]);
    }

    // FILE_APPEND ("a") keeps every row; FILE_WRITE ("w") would truncate the
    // file on every row and only the last row would survive. The mode also
    // creates the file when it does not exist yet.
    f = SD.open(csvFilenames[slot].c_str(), FILE_APPEND);
    if (!f) {
        failExport("cannot open " + csvFilenames[slot]);
        return false;
    }

    // Count a physical file only once it is really open. rotateFile() stages the
    // next name but does not create it: if the session ends before the next
    // write, that file never exists on the card and must not be counted.
    if (!csvCounted[slot]) {
        csvCounted[slot] = true;
        filesCreatedTotal++;
    }

    // Header and rows are verified before either is trusted. On this core the
    // write runs behind a stdio buffer and the flush/close result is discarded,
    // so the free-space guard in the writers - not this check - is what reliably
    // stops a full card; this catches open and I/O failures.
    if (f.size() == 0) {
        if (!writeCsvHeader(f, type)) {
            Log(LOG_ERROR, "SD: Header write failed on " + csvFilenames[slot]);
            f.close();
            failExport("write failed on " + csvFilenames[slot]);
            return false;
        }
    }

    return true;
}

// Append one already-built row through an open handle and update the per-slot
// bookkeeping. Does not close the handle.
bool PySdCard::writeCsvRow(File& f, int module, ExportType type, const String& row) {
    if (module < 0 || module >= SD_EXPORT_MAX_FILES) {
        Log(LOG_ERROR, "SD: Invalid module index " + String(module));
        return false;
    }

    int slot = csvSlot(type, module);

    size_t want = (size_t)row.length();
    size_t written = f.print(row);
    if (written != want) {
        Log(LOG_ERROR, "SD: Row write failed on " + csvFilenames[slot] + " (" +
                       String((unsigned long)written) + "/" +
                       String((unsigned long)want) + " bytes)");
        failExport("write failed on " + csvFilenames[slot]);
        return false;
    }

    csvRecords[slot]++;
    lastFilename = csvFilenames[slot];
    lastFileRecords = csvRecords[slot];
    return true;
}

// Thin wrapper kept for the PWR writer: one open/write/close per row.
bool PySdCard::appendRow(int module, ExportType type, const String& row) {
    File f;
    if (!openCsvForAppend(module, type, f)) return false;
    bool ok = writeCsvRow(f, module, type, row);
    f.close();
    return ok;
}

void PySdCard::rotateFile(int module, ExportType type) {
    if (module < 0 || module >= SD_EXPORT_MAX_FILES) return;

    // The current file keeps its records; the next batch of this module and kind
    // starts a new one. Nothing to close here - the caller closes its handle
    // before rotating, and a new file is opened lazily on the next write.
    int slot = csvSlot(type, module);
    csvFileNumbers[slot]++;
    csvFilenames[slot] = buildFilename(type, module, csvFileNumbers[slot]);
    csvRecords[slot] = 0;
    // Not counted yet: the new file is counted when it is actually opened, so a
    // rotation that is never followed by a write leaves filesCreated untouched.
    csvCounted[slot] = false;

    Log(LOG_INFO, "SD: Rotated to file " + csvFilenames[slot]);
}

bool PySdCard::isReady() const {
    return sdReady;
}

void PySdCard::updateSpaceUsage(bool force) {
    if (!sdReady) return;

    unsigned long now = millis();
    if (!force && (now - lastSpaceCheckMs) < SD_SPACE_REFRESH_MS) return;
    lastSpaceCheckMs = now;

    usedBytes = SD.usedBytes();
}

void PySdCard::refreshSpaceUsage() {
    updateSpaceUsage(false);
}

uint8_t PySdCard::getFreeSpacePercent() const {
    if (!sdReady || totalBytes == 0) return 0;
    uint64_t free = freeBytes();
    return (uint8_t)((free * 100) / totalBytes);
}

// Free bytes on the card, guarded against a used reading that is momentarily
// ahead of total (mount in progress, card swapped).
uint64_t PySdCard::freeBytes() const {
    return totalBytes > usedBytes ? totalBytes - usedBytes : 0;
}

// Forget every module's last BAT write time, so a new session writes each module
// on its first frame instead of inheriting the previous session's interval.
void PySdCard::resetBatWriteTimes() {
    for (int i = 0; i < SD_EXPORT_MAX_FILES; i++) lastBatWriteMs[i] = 0;
}

// ---------------------------------------------------------
// Guards for a data export. The Export button is the activation: the
// "enabled" / "logPwr" / "logBat" config keys are selection state, not gates, so
// they must never block an export the user started.
// ---------------------------------------------------------
bool PySdCard::validateDataExport(uint32_t timeFrom_, uint32_t timeTo_,
                                  bool wantPwr, bool wantBat) const {
    if (!sdReady) {
        Log(LOG_ERROR, "SD: Export failed - card not ready");
        return false;
    }
    if (!wantPwr && !wantBat) {
        Log(LOG_ERROR, "SD: Export failed - neither PWR nor BAT selected");
        return false;
    }
    if (wantBat && !config.battery.enableBat) {
        Log(LOG_ERROR, "SD: Export failed - Bat parser is disabled (Cells page)");
        return false;
    }
    if (timeFrom_ > 0 && timeTo_ > 0 && timeFrom_ > timeTo_) {
        Log(LOG_ERROR, "SD: Export failed - time range start is after the end");
        return false;
    }
    // A From/To window needs a valid clock to compare against. Mirrors the
    // rejections of POST /api/sdcard/export/data.
    if ((timeFrom_ > 0 || timeTo_ > 0) && !config.isSystemTimeValid()) {
        Log(LOG_ERROR, "SD: Export failed - system time is not set");
        return false;
    }
    if (timeTo_ > 0 && config.isSystemTimeValid() && timeTo_ <= (uint32_t)time(nullptr)) {
        Log(LOG_ERROR, "SD: Export failed - end of the time range is in the past");
        return false;
    }
    return true;
}

String PySdCard::generateFilename(ExportType type, int module) const {
    String dt = config.getCurrentDateTimeString();
    dt.replace("-", "");
    dt.replace(":", "");
    dt.replace(" ", "_");

    // Clock not yet valid (before the first NTP sync): derive the stamp from the
    // epoch so the file is never named "/Module_.csv".
    if (dt.length() == 0) {
        dt = epochStamp("%Y%m%d_%H%M%S");
    }

    String prefix;
    String ext;

    switch (type) {
        case ExportType::EXPORT_PWR:
            prefix = "Module_";
            ext = ".csv";
            break;
        case ExportType::EXPORT_BAT:
            prefix = "Cells_module_";
            ext = ".csv";
            break;
        case ExportType::EXPORT_HISTORY:
            prefix = "History_module_";
            ext = ".txt";
            break;
        default:
            prefix = "Export_";
            ext = ".txt";
            break;
    }

    if (module > 0) {
        return "/" + prefix + String(module) + "_" + dt + ext;
    } else {
        return "/" + prefix + dt + ext;
    }
}

// ---------------------------------------------------------
// buildFilename() = generateFilename() + rotation suffix + collision guard.
// Two exports started within the same second (or a rotation inside one second)
// would otherwise append to the same file forever.
// ---------------------------------------------------------
String PySdCard::buildFilename(ExportType type, int module, int fileNumber) const {
    String base = generateFilename(type, module);
    if (fileNumber <= 1 && !SD.exists(base.c_str())) return base;

    int dot = base.lastIndexOf('.');
    String stem = dot > 0 ? base.substring(0, dot) : base;
    String ext  = dot > 0 ? base.substring(dot) : String("");

    int n = fileNumber > 1 ? fileNumber : 1;
    String candidate;
    do {
        candidate = stem + "_" + String(n) + ext;
        n++;
    } while (SD.exists(candidate.c_str()));

    return candidate;
}

bool PySdCard::writeCsvHeader(File& file, ExportType type) {
    // Kept as one String so the write is measured: the header is only trusted
    // when every byte landed. The terminator stays CRLF (Print::println), while
    // data rows end in LF - that asymmetry is the existing file format.
    String header;
    if (type == ExportType::EXPORT_PWR) {
        // temperature is the raw battery value in milli-degrees Celsius (0.001 C);
        // the Fahrenheit flag only affects MQTT payloads, never this buffer.
        header = "datetime,voltage_mV,current_mA,temperature_mC,soc,module_status,cell_min_mV,cell_max_mV\r\n";
    } else if (type == ExportType::EXPORT_BAT) {
        header = "datetime,cell_number,cell_voltage,current_mA,temperature_mC,voltage_state,current_state,temperature_state,soc,coulomb_mAh\r\n";
    } else {
        return true;   // this kind has no header
    }

    size_t written = file.print(header);
    return written == (size_t)header.length();
}

String PySdCard::buildPwrRow(const BatteryModule& module, const String& datetime) const {
    String status = "";
    for (const auto& h : health.modules) {
        if (h.index == module.index) {
            status = h.status;
            break;
        }
    }

    // Cell min/max voltage of this module, taken from the same PWR frame
    // (Vlow/Vhigh are reported in mV). Same validity range the health
    // evaluation uses, so a missing/placeholder field stays 0.
    int cellMin_mV = 0;
    int cellMax_mV = 0;
    String vlow  = module.fields.asString("Vlow");
    String vhigh = module.fields.asString("Vhigh");
    if (vlow != "-" && vhigh != "-") {
        int low  = vlow.toInt();
        int high = vhigh.toInt();
        if (low > 1000 && low < 5000 && high > 1000 && high < 5000) {
            cellMin_mV = low;
            cellMax_mV = high;
        }
    }

    String row = datetime;
    row += "," + String(module.voltage_mV);
    row += "," + String(module.current_mA);
    row += "," + String(module.temperature);
    row += "," + String(module.soc);
    row += "," + status;
    row += "," + String(cellMin_mV);
    row += "," + String(cellMax_mV);
    row += "\n";
    return row;
}

// BAT cell column lookup. The BAT frame header is the battery's own spelling
// (live-captured: Battery, Volt, Curr, Tempr, Base State, Volt. State,
// Curr. State, Temp. State, SOC, Coulomb, BAL). Exact name first, then a
// punctuation- and case-insensitive pass so "Volt. State", "Volt.State" and
// "volt state" all resolve. Returns an empty String when the column is absent -
// a missing value must be written as an empty CSV field, never as a 0 that
// reads like a real measurement.
static String cellColumn(const std::vector<String>& values, const std::vector<String>& header, const char* name) {
    for (size_t i = 0; i < header.size(); i++) {
        if (header[i] == name && i < values.size()) return values[i];
    }

    String want = name;
    want.replace(" ", "");
    want.replace(".", "");
    want.toLowerCase();

    for (size_t i = 0; i < header.size(); i++) {
        String have = header[i];
        have.replace(" ", "");
        have.replace(".", "");
        have.toLowerCase();
        if (have == want && i < values.size()) return values[i];
    }

    return String();
}

// Numeric BAT columns arrive with their unit attached (live values: "82%",
// "60304 mAH"), while Volt/Curr/Tempr are already bare digits. Keep the leading
// numeric run so the CSV stays parseable as numbers. A value with no numeric
// prefix is written verbatim, so a placeholder like "-" is never turned into 0.
static String numericPart(const String& raw) {
    int n = raw.length();
    int i = 0;
    if (i < n && (raw[i] == '-' || raw[i] == '+')) i++;
    int digits = 0;
    while (i < n && isDigit(raw[i])) { i++; digits++; }
    return digits > 0 ? raw.substring(0, i) : raw;
}

String PySdCard::buildBatRow(const BatData& cell, const std::vector<String>& header, const String& datetime) const {
    // Find the cell voltage field
    int cellVoltage = 0;
    int vi = batColumnIndex(header, "V");
    if (vi < 0) vi = batColumnIndex(header, "Volt");
    if (vi < 0) vi = batColumnIndex(header, "Voltage");
    if (vi >= 0 && vi < (int)cell.values.size()) cellVoltage = cell.values[vi].toInt();
    if (cellVoltage == 0) {
        for (size_t k = 0; k < header.size() && k < cell.values.size(); k++) {
            if (header[k].startsWith("V") || header[k].startsWith("v")) {
                int v = cell.values[k].toInt();
                if (v > 0 && v < 5000) { cellVoltage = v; break; }
            }
        }
    }

    String row = datetime;
    row += "," + String(cell.cellIndex + 1);
    row += "," + String(cellVoltage);
    // Per-cell detail from the same frame. Numeric columns lose their unit
    // suffix ("82%" -> "82", "60304 mAH" -> "60304") and carry it in the header
    // instead, so the CSV parses as numbers; the state columns stay verbatim.
    row += "," + numericPart(cellColumn(cell.values, header, "Curr"));
    row += "," + numericPart(cellColumn(cell.values, header, "Tempr"));
    row += "," + cellColumn(cell.values, header, "Volt. State");
    row += "," + cellColumn(cell.values, header, "Curr. State");
    row += "," + cellColumn(cell.values, header, "Temp. State");
    row += "," + numericPart(cellColumn(cell.values, header, "SOC"));
    row += "," + numericPart(cellColumn(cell.values, header, "Coulomb"));
    row += "\n";
    return row;
}

void PySdCard::checkTimeout() {
    if (millis() - exportStartTime > exportTimeoutMs) {
        Log(LOG_WARN, "SD: Export timeout (" + String(exportTimeoutMs) + "ms)");
        exportState = ExportState::TIMEOUT;
    }
}

void PySdCard::closeCurrentFile() {
    if (txtFile) {
        txtFile.flush();
        txtFile.close();
        Log(LOG_INFO, "SD: Closed file " + txtFilename);
    }
    txtFile = File();
    txtFilename = "";
}

void PySdCard::closeAllFiles() {
    closeCurrentFile();

    // CSV handles are closed at the end of every batch; only the bookkeeping is
    // reset here.
    for (int i = 0; i < SD_EXPORT_MAX_FILES * 2; i++) {
        csvFilenames[i] = "";
        csvRecords[i] = 0;
        csvFileNumbers[i] = 0;
        csvCounted[i] = false;
    }
}

void PySdCard::finishExport(ExportState endState) {
    closeAllFiles();
    flushDatalistQueue();

    stopRequested = false;
    lastPwrSeq = 0;
    lastBatSeq = 0;
    lastPwrWriteMs = 0;
    resetBatWriteTimes();
    // Do not keep a stale schedule window on a finished session.
    timeFrom = 0;
    timeTo = 0;
    exportState = endState;

    updateSpaceUsage(true);
}

// ---------------------------------------------------------
// Terminal failure with a reason the web page can show. Every write path that
// cannot store its bytes funnels through here, so a failing session always ends
// the same way: files left on the card, options unlocked, reason reported.
// ---------------------------------------------------------
void PySdCard::failExport(const String& reason) {
    lastError = reason;
    Log(LOG_ERROR, "SD: Export aborted - " + reason);
    finishExport(ExportState::ERROR);
}

// ---------------------------------------------------------
// Task 1 -> Task 2 datalist handoff
// ---------------------------------------------------------
void PySdCard::onDatalistChunk(const char* data, size_t len, bool last) {
    if (datalistQueue == nullptr) return;
    if (exportState != ExportState::RUNNING) return;
    if (exportKind != ExportKind::KIND_HISTORY) return;

    DatalistChunk chunk;
    if (data != nullptr && len > 0) {
        char* buf = (char*)malloc(len);
        if (!buf) {
            Log(LOG_ERROR, "SD: datalist chunk allocation failed");
            return;
        }
        memcpy(buf, data, len);
        chunk.data = buf;
        chunk.len  = len;
    } else {
        chunk.data = nullptr;
        chunk.len  = 0;
    }
    chunk.last = last;

    // Bounded blocking send: Task 1 waits (and yields) while Task 2 drains the
    // queue, so a streamed reply is never dropped.
    if (xQueueSend(datalistQueue, &chunk, pdMS_TO_TICKS(SD_DATALIST_SEND_TIMEOUT_MS)) != pdTRUE) {
        if (chunk.data) free(chunk.data);
        Log(LOG_WARN, "SD: datalist chunk dropped (queue full)");
    }
}

void PySdCard::onDatalistStreamEnd() {
    // End of a streamed reply without the explicit terminator: push an empty
    // last-chunk marker so the writer completes the export.
    onDatalistChunk(nullptr, 0, true);
}

bool PySdCard::drainDatalistQueue() {
    if (datalistQueue == nullptr) return false;

    DatalistChunk chunk;
    bool progress = false;
    bool finished = false;

    while (xQueueReceive(datalistQueue, &chunk, 0) == pdTRUE) {
        if (chunk.data && chunk.len > 0 && txtFile) {
            // Same rule as the CSV writers: stop on a full card instead of
            // streaming into a dead file. loop() already refreshed the reading,
            // and the write check below catches a failure between two reads.
            if (freeBytes() < SD_FULL_MIN_FREE_BYTES) {
                free(chunk.data);
                failExport("SD card full");
                return false;
            }
            size_t written = txtFile.write((const uint8_t*)chunk.data, chunk.len);
            if (written != chunk.len) {
                Log(LOG_ERROR, "SD: History write failed (" +
                               String((unsigned long)written) + "/" +
                               String((unsigned long)chunk.len) + " bytes)");
                free(chunk.data);
                failExport("SD card full or write error");
                return false;
            }
            progress = true;
        }
        if (chunk.data) free(chunk.data);
        if (chunk.last) finished = true;
    }

    if (finished) {
        // Response terminator received: the payload is complete.
        Log(LOG_INFO, "SD: datalist response complete (" + String(txtFilename) + ")");
        finishExport(ExportState::COMPLETED);
    }

    return progress;
}

void PySdCard::flushDatalistQueue() {
    if (datalistQueue == nullptr) return;

    DatalistChunk chunk;
    while (xQueueReceive(datalistQueue, &chunk, 0) == pdTRUE) {
        if (chunk.data) free(chunk.data);
    }
}

// ---------------------------------------------------------
// One battery data session. The two page checkboxes decide which kinds it
// writes; a single Export press can cover PWR, BAT or both at once. Each kind
// keeps its own per-module files and its own rotation.
// ---------------------------------------------------------
bool PySdCard::startDataExport(uint32_t timeFrom_, uint32_t timeTo_,
                               const std::vector<int>& modules,
                               bool wantPwr, bool wantBat) {
    if (isExporting()) {
        Log(LOG_WARN, "SD: Export already in progress");
        return false;
    }
    if (!validateDataExport(timeFrom_, timeTo_, wantPwr, wantBat)) return false;

    // Reset any leftover CSV bookkeeping (e.g. from a session that ended in
    // ERROR). Safe here: a data start implies no History session is active.
    closeAllFiles();
    lastError = "";

    exportKind = ExportKind::KIND_DATA;
    sessionPwr = wantPwr;
    sessionBat = wantBat;
    currentModule = 0;
    timeFrom = timeFrom_;
    timeTo = timeTo_;
    exportModules = modules;
    // No timeout for data exports: they are bounded by the From/To window and
    // by Stop. exportTimeoutMs stays armed for the History export only.

    filesCreatedTotal = 0;
    lastFilename = "";
    lastFileRecords = 0;
    lastPwrSeq = 0;
    lastBatSeq = 0;
    lastPwrWriteMs = 0;
    resetBatWriteTimes();
    flushDatalistQueue();

    exportStartTime = millis();
    stopRequested = false;
    // A From time in the future keeps the session in SCHEDULED until the clock
    // reaches it; loop() then switches it to RUNNING.
    if (timeFrom > 0 && config.isSystemTimeValid() && (uint32_t)time(nullptr) < timeFrom) {
        exportState = ExportState::SCHEDULED;
    } else {
        exportState = ExportState::RUNNING;
    }

    // CSV files are created lazily, one per module and kind, on the first row.
    Log(LOG_INFO, "SD: Started data export (" + getKindsJson() + ")");
    return true;
}

bool PySdCard::startHistoryExport(int module) {
    if (isExporting()) {
        Log(LOG_WARN, "SD: Export already in progress");
        return false;
    }
    if (!sdReady) {
        Log(LOG_ERROR, "SD: Export failed - card not ready");
        return false;
    }
    if (module <= 0) {
        Log(LOG_ERROR, "SD: History export failed - invalid module number");
        return false;
    }

    exportKind = ExportKind::KIND_HISTORY;
    sessionPwr = false;
    sessionBat = false;
    currentModule = module;
    exportTimeoutMs = sdCardConfig.exportTimeoutMs;

    txtFilename = buildFilename(ExportType::EXPORT_HISTORY, module, 1);
    txtFile = SD.open(txtFilename.c_str(), FILE_WRITE);
    if (!txtFile) {
        // Same visible failure as the CSV paths: the page can show the reason.
        failExport("cannot create History export file");
        return false;
    }
    // The battery's datalist payload is written verbatim - no metadata header.
    flushDatalistQueue();

    filesCreatedTotal = 1;
    lastFilename = txtFilename;
    lastFileRecords = 0;
    lastError = "";

    exportStartTime = millis();
    stopRequested = false;
    exportState = ExportState::RUNNING;

    // Enqueue the datalist command via scheduler (Task 1)
    py_scheduler.enqueue("datalist history " + String(module));

    Log(LOG_INFO, "SD: Started History export for module " + String(module) + " to " + txtFilename);
    return true;
}

void PySdCard::stopExport() {
    if (isExporting()) {
        stopRequested = true;
        Log(LOG_INFO, "SD: Stop requested for current export");
    }
}

// ---------------------------------------------------------
// Kinds covered by the current (or last finished) session, as a JSON array:
// [] idle, ["pwr"] / ["bat"] / ["pwr","bat"] data session, ["history"].
// The data page shows this list to tell the user what is running.
// ---------------------------------------------------------
String PySdCard::getKindsJson() const {
    switch (exportKind) {
        case ExportKind::KIND_DATA: {
            String kinds = "[";
            if (sessionPwr) kinds += "\"pwr\"";
            if (sessionBat) {
                if (sessionPwr) kinds += ",";
                kinds += "\"bat\"";
            }
            kinds += "]";
            return kinds;
        }
        case ExportKind::KIND_HISTORY:
            return "[\"history\"]";
        default:
            return "[]";
    }
}

String PySdCard::getExportStatusJson() {
    // Keep the reported free space in step with the card.
    updateSpaceUsage(false);

    String json = "{";
    json += "\"state\":\"" + String(exportState == ExportState::IDLE ? "idle" :
                       exportState == ExportState::SCHEDULED ? "scheduled" :
                       exportState == ExportState::RUNNING ? "running" :
                       exportState == ExportState::STOPPED ? "stopped" :
                       exportState == ExportState::COMPLETED ? "completed" :
                       exportState == ExportState::ERROR ? "error" : "timeout") + "\",";
    json += "\"kind\":\"" + String(exportKind == ExportKind::KIND_DATA ? "data" :
                       exportKind == ExportKind::KIND_HISTORY ? "history" : "none") + "\",";
    json += "\"kinds\":" + getKindsJson() + ",";
    json += "\"module\":" + String(currentModule) + ",";
    json += "\"filesCreated\":" + String(filesCreatedTotal) + ",";
    json += "\"currentFile\":\"" + lastFilename + "\",";
    json += "\"recordsInCurrentFile\":" + String(lastFileRecords) + ",";
    json += "\"progressPercent\":" + String(lastFileRecords * 100 / (sdCardConfig.recordsPerFile > 0 ? sdCardConfig.recordsPerFile : 1)) + ",";
    json += "\"error\":\"" + lastError + "\",";
    json += "\"sdReady\":" + String(sdReady ? "true" : "false") + ",";
    json += "\"freeSpacePercent\":" + String(getFreeSpacePercent()) + ",";
    json += "\"buttonText\":\"" + String(isExporting() ? "Stop export" : "Export") + "\",";
    json += "\"optionsLocked\":" + String(isExporting() ? "true" : "false") + ",";
    json += "\"exportKind\":" + String((int)exportKind) + ",";
    json += "\"scheduledFrom\":" + String(timeFrom) + ",";
    json += "\"scheduledTo\":" + String(timeTo) + ",";
    json += "\"queryIntervalInfo\":\"Note: PWR/BAT exports use the query interval configured in Battery data / Cells pages (currently: PWR=" + String(config.battery.intervalPwr / 1000) + "s, BAT=" + String(config.battery.intervalBat / 1000) + "s)\",";
    json += "\"batParserRequired\":" + String(config.battery.enableBat ? "false" : "true");
    json += "}";
    return json;
}