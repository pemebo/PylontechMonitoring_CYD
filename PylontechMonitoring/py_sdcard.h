#pragma once
#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "config.h"

// Per-file / per-row type: decides file name, CSV header and row layout.
enum class ExportType {
    EXPORT_NONE,
    EXPORT_PWR,
    EXPORT_BAT,
    EXPORT_HISTORY
};

// What a running session covers. One data session may cover PWR, BAT or both,
// depending on the checkboxes on the Data Export page.
enum class ExportKind {
    KIND_NONE,
    KIND_DATA,      // battery data: PWR and/or BAT per selection
    KIND_HISTORY
};

enum class ExportState {
    IDLE,
    SCHEDULED,  // start time not reached yet: waiting for timeFrom
    RUNNING,
    STOPPED,  // user pressed Stop: terminal, files already closed and flushed
    COMPLETED,
    ERROR,
    TIMEOUT
};

// ---------------------------------------------------------
// One datalist response chunk, handed over from Task 1 (UART) to
// Task 2 (SD writer) through a FreeRTOS queue. "data" is heap allocated by
// the producer and released by the consumer.
// ---------------------------------------------------------
struct DatalistChunk {
    char*  data = nullptr;
    size_t len  = 0;
    bool   last  = false;   // response terminator seen -> export is complete
};

// ---------------------------------------------------------
// PWR/BAT exports keep one CSV file open per module, indexed by module
// number (0 = unused slot). The battery supports up to MAX_MODULES.
// ---------------------------------------------------------
#define SD_EXPORT_MAX_FILES (MAX_MODULES + 1)

class PySdCard {
public:
    void begin();
    void loop();

    bool isReady() const;
    uint8_t getFreeSpacePercent() const;
    uint64_t getTotalBytes() const { return totalBytes; }
    uint64_t getUsedBytes() const { return usedBytes; }

    // Re-read used/total bytes from the card (throttled). Called by loop() and
    // by the web status endpoints so the reported free space is never frozen.
    void refreshSpaceUsage();

    // Battery data export (PWR and/or BAT in one session, from double-buffer
    // data). The selection decides which kinds the session writes.
    bool startDataExport(uint32_t timeFrom, uint32_t timeTo,
                         const std::vector<int>& modules,
                         bool wantPwr, bool wantBat);

    // History TXT export (via datalist command + callback). One-shot:
    // no time range, no module list, the whole response of one module.
    bool startHistoryExport(int module);

    void stopExport();

    // Task 1 -> Task 2 datalist handoff: one streamed chunk at a time. `last`
    // marks the terminator/end of the reply; `data` may be null when len == 0.
    void onDatalistChunk(const char* data, size_t len, bool last);
    // The reply ended without an explicit terminator: complete the export.
    void onDatalistStreamEnd();

    // Status / monitoring
    String getExportStatusJson();

    // Public getters for export state (used by web API)
    ExportState getExportState() const { return exportState; }
    ExportKind getExportKind() const { return exportKind; }
    bool isExporting() const {
        return exportState == ExportState::RUNNING || exportState == ExportState::SCHEDULED;
    }

    // Kinds covered by the last/ running session, e.g. ["pwr","bat"].
    String getKindsJson() const;

private:
    // Internal helpers
    bool initSD();
    String generateFilename(ExportType type, int module = 0) const;
    String buildFilename(ExportType type, int module, int fileNumber) const;
    bool writeCsvHeader(File& file, ExportType type);
    String buildPwrRow(const BatteryModule& module, const String& datetime) const;
    String buildBatRow(const BatData& cell, const std::vector<String>& header, const String& datetime) const;
    bool writePwrFrames();
    bool writeBatFrames();
    bool appendRow(int module, ExportType type, const String& row);
    bool openCsvForAppend(int module, ExportType type, File& f);
    bool writeCsvRow(File& f, int module, ExportType type, const String& row);
    bool moduleSelected(int index) const;
    int  csvSlot(ExportType type, int module) const;
    void rotateFile(int module, ExportType type);
    void checkTimeout();
    void closeCurrentFile();
    void closeAllFiles();
    void finishExport(ExportState endState);
    void failExport(const String& reason);
    bool validateDataExport(uint32_t timeFrom, uint32_t timeTo,
                            bool wantPwr, bool wantBat) const;
    bool drainDatalistQueue();
    void flushDatalistQueue();
    void updateSpaceUsage(bool force);
    uint64_t freeBytes() const;
    void resetBatWriteTimes();
    unsigned long pwrIntervalMs() const;
    unsigned long batIntervalMs() const;

    // State
    bool sdReady = false;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    unsigned long lastSpaceCheckMs = 0;

    // Session state. A data session covers PWR and/or BAT, so the kind and the
    // covered kinds are tracked separately.
    ExportKind exportKind = ExportKind::KIND_NONE;
    ExportState exportState = ExportState::IDLE;
    bool stopRequested = false;
    bool sessionPwr = false;
    bool sessionBat = false;

    // Reason of the last terminal failure, surfaced as the status "error" field.
    // Cleared when a new session starts; a full card is the usual cause.
    String lastError;

    // History export writes a single verbatim TXT file
    File txtFile;
    String txtFilename;
    int currentModule = 0;           // for history export

    // PWR/BAT exports: one CSV file per module *and per kind*, so a session that
    // covers both keeps Module_N.csv and Cells_module_N.csv side by side.
    // A handle is opened for the duration of one batch and closed right after:
    // PWR opens one per row (one row per module), BAT opens one per frame and
    // writes that frame's 15 rows through it. No handle is held between batches;
    // each SD.open allocates a 4096-byte stdio buffer.
    //   slot for PWR = module
    //   slot for BAT = module + SD_EXPORT_MAX_FILES
    String csvFilenames[SD_EXPORT_MAX_FILES * 2];
    int    csvRecords[SD_EXPORT_MAX_FILES * 2];
    int    csvFileNumbers[SD_EXPORT_MAX_FILES * 2];
    // True once the slot's current file has been opened and counted. A rotation
    // clears it so the next file is counted when it is really created, and a
    // rotation that never reaches a write is not counted at all.
    bool   csvCounted[SD_EXPORT_MAX_FILES * 2] = {};
    int    filesCreatedTotal = 0;

    // Last file touched (reported by the status endpoint)
    String lastFilename;
    int lastFileRecords = 0;

    // New-frame detection + interval pacing
    uint32_t lastPwrSeq = 0;
    uint32_t lastBatSeq = 0;
    unsigned long lastPwrWriteMs = 0;
    // BAT pacing is per module: one `bat N` reply carries the cells of a single
    // module, so a shared timestamp lets the first module consume the whole
    // interval and starves every other module of a file.
    unsigned long lastBatWriteMs[SD_EXPORT_MAX_FILES] = {};

    uint32_t exportStartTime = 0;
    uint32_t exportTimeoutMs = 600000;
    uint32_t timeFrom = 0;
    uint32_t timeTo = 0;
    std::vector<int> exportModules;

    // Task 1 -> Task 2 handoff for datalist responses
    QueueHandle_t datalistQueue = nullptr;
};

extern PySdCard sdCard;