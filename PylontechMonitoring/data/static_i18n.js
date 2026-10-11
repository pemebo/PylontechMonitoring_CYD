// -------------------------------------------------------------
// Client-side i18n for the Pylontech Monitor web UI.
//
// Mechanism:
//   - window.I18N = { lang, dict: { en, es } }
//   - window.t(key) -> dict[lang][key] || dict.en[key] || key
//   - window.applyI18n(root) translates the DOM attributes:
//       data-i18n       -> textContent
//       data-i18n-html  -> innerHTML (trusted static strings only)
//       data-i18n-ph    -> placeholder
//       data-i18n-title -> title
//       data-i18n-aria  -> aria-label
//   - window.setLang(lang) normalises, persists the "pylang" cookie and
//     translates the whole document.
//
// Language resolution order: window.I18N_LANG (injected server-side into the
// SPA shell) -> "pylang" cookie -> "en". Anything that is not "es" is "en".
//
// Key convention: namespaced, lowercase, dot-separated:
//   nav.*     sidebar navigation labels / section headers
//   layout.*  SPA shell strings (title, dynamic messages, MQTT indicator)
//   conn.*    Settings page (pages_connection)
// The same key must be used in the HTML data-i18n attribute and in JS t().
// -------------------------------------------------------------
(function () {
    'use strict';

    var COOKIE_NAME = 'pylang';

    var dict = {
        en: {
            // ---- SPA shell -----------------------------------------------
            'layout.title': 'Pylontech Dashboard',
            'layout.h1': 'Pylontech Monitor',
            'layout.toggle_menu': 'Toggle menu',
            'layout.auth_required': 'Authentication required.',
            'layout.error_loading': 'Error loading: ',
            'layout.mqtt_disabled': 'Disabled',
            'layout.mqtt_active': 'Active',
            'layout.mqtt_disconnected': 'Disconnected',

            // ---- Sidebar navigation --------------------------------------
            'nav.section_info': 'Info',
            'nav.section_mqtt': 'MQTT',
            'nav.section_config': 'Config',
            'nav.section_service': 'Service',
            'nav.dashboard': 'Dashboard',
            'nav.modules': 'Modules',
            'nav.health': 'Health',
            'nav.battery_data': 'Battery data',
            'nav.cells': 'Cells',
            'nav.statistics': 'Statistics',
            'nav.settings': 'Settings',
            'nav.screen': 'Screen',
            'nav.log': 'Log',
            'nav.data_export': 'Data export',
            'nav.sd_card': 'SD card',
            'nav.file_manager': 'File manager',
            'nav.console': 'Console',
            'nav.frame_dump': 'Frame Dump',
            'nav.update': 'Update',

            // ---- Settings page: generic ----------------------------------
            'conn.settings': 'Settings',
            'conn.save': 'Save',
            'conn.password': 'Password:',
            'conn.server': 'Server:',
            'conn.server_heading': 'Server',

            // ---- Settings page: WiFi -------------------------------------
            'conn.wifi': 'WiFi',
            'conn.wifi_loading': 'Loading WiFi status…',
            'conn.wifi_disable': 'Disable WiFi',
            'conn.wifi_enable': 'Enable WiFi',
            'conn.wifi_boot_hint': 'Press the Boot button of the device to re-enable the WiFi',
            'conn.wifi_scan': 'Scan',
            'conn.wifi_manual': 'Manual Mode',
            'conn.wifi_ssid': 'SSID:',
            'conn.connect': 'Connect',
            'conn.connected': 'Connected',
            'conn.enter_password': 'Enter password',
            'conn.wifi_disabling': 'WiFi is being disabled — this page will become unreachable.',
            'conn.wifi_enabled': 'WiFi enabled',
            'conn.wifi_enable_failed': 'Failed to enable WiFi',

            // ---- Settings page: MQTT -------------------------------------
            'conn.mqtt': 'MQTT',
            'conn.mqtt_enabled': 'Enabled:',
            'conn.port': 'Port:',
            'conn.user': 'User:',
            'conn.mqtt_topic_prefix': 'Topic Prefix:',

            // ---- Settings page: NTP / Time -------------------------------
            'conn.ntp_time': 'NTP / Time',
            'conn.use_gateway_ntp': 'Use gateway NTP',
            'conn.manual_ntp': 'Manual NTP server',
            'conn.manual_time': 'Manual time',
            'conn.ntp_server': 'NTP Server:',
            'conn.date': 'Date:',
            'conn.time': 'Time:',
            'conn.timezone': 'Timezone',
            'conn.region': 'Region:',
            'conn.city': 'City:',
            'conn.manual_time_no_ntp': 'Manual time (no NTP)',

            // ---- Settings page: IP configuration -------------------------
            'conn.ip_config': 'IP Configuration',
            'conn.dhcp': 'DHCP',
            'conn.ip_address': 'IP Address:',
            'conn.subnet_mask': 'Subnet Mask:',
            'conn.gateway': 'Gateway:',
            'conn.dns': 'DNS:',

            // ---- Settings page: Automatic restart ------------------------
            'conn.automatic_restart': 'Automatic restart',
            'conn.automatic_reboot': 'Automatic reboot',
            'conn.reboot_time': 'Reboot time:',
            'conn.reboot_every': 'Reboot every:',
            'conn.days': 'day(s)',
            'conn.restart_hint': 'Reboots at the configured time every N days. Requires a valid system time (NTP or manual). Default: daily at 03:00.',

            // ---- Settings page: Config password --------------------------
            'conn.config_password': 'Config password',
            'conn.password_hint': 'Max. 20 alphanumeric characters. Leave blank to keep the current password. Default: 12345.',

            // ---- Settings page: Language ---------------------------------
            'conn.language': 'Language',
            'conn.language_saved': 'Language saved',

            // ---- Settings page: server response mapping ------------------
            'conn.msg_connecting': 'Connecting…',
            'conn.msg_mqtt_saved': 'MQTT saved',
            'conn.msg_time_saved': 'Time saved',
            'conn.msg_network_saved': 'Network saved',
            'conn.msg_password_saved': 'Password saved',
            'conn.msg_password_unchanged': 'Password unchanged',
            'conn.msg_restart_saved': 'Automatic restart saved',
            'conn.msg_missing_body': 'Missing body',
            'conn.msg_invalid_json': 'Invalid JSON',
            'conn.msg_ssid_missing': 'SSID missing',
            'conn.msg_max_20': 'Max 20 characters',
            'conn.msg_alnum': 'Only alphanumeric characters (A-Z a-z 0-9)',
            'conn.msg_time_format': 'Time must be in HH:MM format (00-23 / 00-59)',
            'conn.msg_days_range': 'Days must be between 1 and 365',
            'conn.msg_unsupported_language': 'Unsupported language',

            // ---- Settings page: local validation -------------------------
            'conn.err_password_charset': 'Max. 20 characters, only letters and numbers',
            'conn.err_time_format': 'Time must be in HH:MM format',
            'conn.err_days_range': 'Days must be between 1 and 365',

            // ---- Dashboard page (pages_dashboard) ------------------------
            'dash.title': 'Dashboard',
            'dash.battery': 'Battery',
            'dash.battery_overview': 'Battery Overview',
            'dash.health': 'Health',
            'dash.modules': 'Modules',
            'dash.wifi': 'WiFi',
            'dash.mqtt': 'MQTT',
            'dash.cpu_memory': 'CPU & Memory',
            'dash.system': 'System',
            'dash.status': 'Status:',
            'dash.soc': 'SOC:',
            'dash.voltage': 'Voltage:',
            'dash.current': 'Current:',
            'dash.power': 'Power:',
            'dash.temperature': 'Temperature:',
            'dash.cell_min': 'Cell Min:',
            'dash.cell_max': 'Cell Max:',
            'dash.max_delta': 'Max Delta:',
            'dash.avg_soh': 'Average SOH:',
            'dash.events_history': 'Events in history:',
            'dash.detected_modules': 'Detected modules:',
            'dash.last_update': 'Last update:',
            'dash.mode': 'Mode:',
            'dash.ssid': 'SSID:',
            'dash.ip': 'IP:',
            'dash.rssi': 'RSSI:',
            'dash.quality': 'Quality:',
            'dash.server': 'Server:',
            'dash.last_contact': 'Last contact:',
            'dash.core0': 'Core 0:',
            'dash.core1': 'Core 1:',
            'dash.heap': 'Heap:',
            'dash.psram': 'PSRAM:',
            'dash.sd_free': 'SD free:',
            'dash.time': 'Time:',
            'dash.uptime': 'Uptime:',
            'dash.firmware': 'Firmware:',
            'dash.qual_high': 'High',
            'dash.qual_medium': 'Medium',
            'dash.qual_low': 'Low',
            'dash.qual_very_low': 'Very low',
            'dash.no_card': 'No card',
            'dash.free': 'free',

            // ---- Modules page (pages_modules) ----------------------------
            'mod.stack_title': 'Modules stack',
            'mod.modules': 'Modules',
            'mod.module': 'Module',
            'mod.status': 'Status',
            'mod.soc': 'SOC',
            'mod.voltage': 'Voltage',
            'mod.current': 'Current',
            'mod.temperature': 'Temperature',
            'mod.cell_delta': 'Cell Delta',
            'mod.statistics': 'Statistics',
            'mod.soh': 'SOH',
            'mod.cycles': 'Cycles',
            'mod.total_discharged': 'Total discharged',
            'mod.ov': 'OV',
            'mod.hv': 'HV',
            'mod.lv': 'LV',
            'mod.uv': 'UV',
            'mod.bms_warnings': 'BMS warnings',
            'mod.bms_alarms': 'BMS alarms',
            'mod.cell_voltages': 'Cell voltages',
            'mod.cell_socs': 'Cell SOCs',
            'mod.legend_stats': 'SOH: State of Health &middot; OV: Overvoltage count &middot; HV: High voltage count &middot; LV: Low voltage count &middot; UV: Undervoltage count',
            'mod.legend_voltage': '<span style="color:#ff0000; font-weight:bold;">Red</span> = highest cell of the module &middot; <span style="color:#0000ff; font-weight:bold;">Blue</span> = lowest cell of the module',
            'mod.stat_disabled': 'STAT parser disabled. Click on refresh to get updated values.',
            'mod.refresh': 'Refresh',
            'mod.cells_interval_hint': 'Values will be updated according to Query interval value in the Cells page',
            'mod.hdr_batt_soc': 'Battery SOC:',
            'mod.hdr_status': 'Status:',
            'mod.hdr_voltage': 'Voltage:',
            'mod.hdr_current': 'Current:',
            'mod.hdr_modules': 'Modules:',
            'mod.hdr_update': 'Update:',
            'mod.err_bat_load': 'BAT load failed: ',
            'mod.err_cell_read': 'Cell read failed: ',
            'mod.err_cells_read': 'Cells read failed: ',
            'mod.err_statistics_read': 'Statistics read failed: ',
            'mod.err_modules_read': 'Modules read failed: ',
            'mod.err_stat_load': 'STAT load failed: ',

            // ---- Health page (pages_health) ------------------------------
            'health.title': 'Health Status',
            'health.modules': 'Modules',
            'health.history': 'History',
            'health.module': 'Module',
            'health.status': 'Status',
            'health.temp': 'Temp',
            'health.cell_min': 'Cell Min',
            'health.cell_max': 'Cell Max',
            'health.delta': 'Delta',
            'health.cell_min_today': 'Cell Min Today',
            'health.cell_max_today': 'Cell Max Today',
            'health.delta_max_today': 'Delta Max Today',
            'health.date': 'Date',
            'health.event': 'Event',
            'health.reset': 'Reset',
            'health.thresholds_title': 'Delta thresholds settings',
            'health.warning_threshold': 'Warning Threshold (V):',
            'health.error_threshold': 'Error Threshold (V):',
            'health.save': 'Save',
            'health.prefix': 'Health: ',
            'health.err_reset_auth': 'Authentication required. Reset not applied.',
            'health.err_save_auth': 'Authentication required. Save not applied.',

            // ---- Screen page (pages_screen) ------------------------------
            'screen.title': 'Screen',
            'screen.display': 'Display',
            'screen.enabled': 'Screen enabled:',
            'screen.touch': 'Touch enabled:',
            'screen.dark': 'Dark mode:',
            'screen.brightness': 'Brightness:',
            'screen.refresh': 'Min refresh (s):',
            'screen.refresh_hint': 'seconds; 0 = refresh on every update. Controls the flickering of the screen',
            'screen.wifi_warn': 'WiFi is not connected and the device is not in AP mode — the screen cannot be turned off.',
            'screen.save': 'Save',
            'screen.power_control': 'Power Control',
            'screen.schedule': 'Schedule:',
            'screen.screen_off': 'Screen off:',
            'screen.screen_on': 'Screen on:',
            'screen.time_warn': 'Scheduling is active but the system time is not valid (NTP not synced or no manual time set) — the timed power control will not activate.',
            'screen.auto_off': 'Auto power-off:',
            'screen.auto_off_hint': 'The screen turns off automatically after the set time without any touch. A touch wakes it back up, showing the view it had when it turned off.',
            'screen.msg_saved': 'Screen config saved',
            'screen.msg_missing_body': 'Missing body',
            'screen.msg_invalid_json': 'Invalid JSON',
            'screen.msg_wifi_unavailable': 'Screen cannot be turned off while WiFi is unavailable',

            // ---- Log page (pages_log) ------------------------------------
            'log.title': 'Log',
            'log.loading': 'loading...',
            'log.reload': 'Reload',
            'log.level_filter': 'Log Level Filter',
            'log.info': 'Info',
            'log.warn': 'Warn',
            'log.error': 'Error',
            'log.debug': 'Debug',
            'log.save': 'Save',
            'log.err_loading': 'Error loading log',
            'log.msg_saved': 'Log level updated',
            'log.msg_missing_body': 'Missing body',
            'log.msg_invalid_json': 'Invalid JSON',

            // ---- Battery data page (pages_basevalue) ---------------------
            'bv.title': 'Battery data',
            'bv.mqtt': 'MQTT:',
            'bv.temp_settings': 'Temperature Settings',
            'bv.fahrenheit': 'Send Temperature in Fahrenheit',
            'bv.mqtt_subtopics': 'MQTT Subtopics',
            'bv.topic_pwr': 'Topic PWR',
            'bv.topic_stack': 'Topic Stack',
            'bv.query_interval': 'Query Interval',
            'bv.pwr_seconds': 'PWR (seconds)',
            'bv.pwr_parser_fields': 'PWR Parser Fields',
            'bv.field_name': 'Field Name',
            'bv.display_name': 'Display Name',
            'bv.raw_data': 'Raw Data',
            'bv.factor': 'Factor',
            'bv.unit': 'Unit',
            'bv.mqtt_col': 'MQTT',
            'bv.save': 'Save',
            'bv.msg_saved': 'PWR settings saved',
            'bv.msg_missing_body': 'Missing body',
            'bv.msg_invalid_json': 'Invalid JSON',

            // ---- Cell data page (pages_celldata) -------------------------
            'cell.title': 'Cell data',
            'cell.mqtt': 'MQTT:',
            'cell.parser': 'Cells Parser (BAT)',
            'cell.enable': 'Enable',
            'cell.mqtt_subtopics': 'MQTT Subtopics',
            'cell.topic_bat': 'Topic BAT',
            'cell.cell_prefix': 'Cell Prefix',
            'cell.query_interval': 'Query Interval',
            'cell.bat_seconds': 'BAT (seconds)',
            'cell.cell_values': 'Cell Values',
            'cell.field_name': 'Field Name',
            'cell.display_name': 'Display Name',
            'cell.raw_data': 'Raw Data',
            'cell.factor': 'Factor',
            'cell.unit': 'Unit',
            'cell.mqtt_col': 'MQTT',
            'cell.save': 'Save',
            'cell.interval_hint_one': 'Recommended: at least {sec} s (1 module x 5 s).',
            'cell.interval_hint_many': 'Recommended: at least {sec} s ({n} modules x 5 s).',
            'cell.msg_saved': 'BAT settings saved',
            'cell.msg_saved_warning': 'BAT settings saved. Warning: with {n} module(s) the BAT interval should be at least {sec} s (5 s per module); a shorter interval can fragment the heap during an SD export.',
            'cell.msg_missing_body': 'Missing body',
            'cell.msg_invalid_json': 'Invalid JSON',
            'cell.msg_export_running': 'Cannot disable the BAT parser while a BAT export is running',
            'cell.msg_interval_min': 'BAT query interval must be at least 10 seconds',

            // ---- Statistics page (pages_statistic) -----------------------
            'stat.title': 'Battery statistics',
            'stat.mqtt': 'MQTT:',
            'stat.parser': 'Statistics Parser (STAT)',
            'stat.mqtt_subtopic': 'MQTT Subtopic',
            'stat.topic_stat': 'Topic STAT',
            'stat.query_interval': 'Query Interval',
            'stat.stat_seconds': 'STAT (seconds)',
            'stat.save': 'Save',
            'stat.field_name': 'Field name',
            'stat.display_name': 'Display name',
            'stat.raw_data': 'Raw data',
            'stat.factor': 'Factor',
            'stat.unit': 'Unit',
            'stat.mqtt_col': 'MQTT',
            'stat.msg_saved': 'STAT settings saved',
            'stat.msg_missing_body': 'Missing body',
            'stat.msg_stale': 'Stale Statistics page: reload the UI (Ctrl+F5)',
            'stat.msg_invalid_json': 'Invalid JSON: ',

            // ---- SD card page (pages_sdcard) -----------------------------
            'sd.title': 'SD Card',
            'sd.upload': 'Upload',
            'sd.refresh': 'Refresh',
            'sd.no_card': 'No SD card inserted or not mounted.',
            'sd.col_name': 'Name',
            'sd.col_size': 'Size',
            'sd.col_action': 'Action',
            'sd.download': 'Download',
            'sd.delete': 'Delete',
            'sd.bytes': ' bytes',
            'sd.progress_file': 'File {i}/{n}: {p}%',
            'sd.done': 'Done!',
            'sd.confirm_delete': 'Delete {path}?',
            'sd.err_invalid_response': 'invalid response',
            'sd.err_load_files': 'Failed to load files: ',
            'sd.err_load_status': 'Failed to load SD card status: ',
            'sd.err_network': 'network error',
            'sd.err_upload_failed': 'Upload failed: ',
            'sd.err_delete_failed': 'Delete failed: ',
            'sd.delete_all': 'Delete all',
            'sd.confirm_delete_all': 'Permanently delete ALL files in {path}?\n\nThis cannot be undone. Sub-directories are kept.',
            'sd.err_delete_all_failed': 'Delete all failed: ',
            'sd.err_delete_all_some': 'Delete all: {n} file(s) could not be removed',

            // ---- File manager page (pages_filemanager) -------------------
            'fm.title': 'ESP32 File Manager',
            'fm.upload': 'Upload',
            'fm.col_name': 'Name',
            'fm.col_size': 'Size',
            'fm.col_action': 'Action',
            'fm.download': 'Download',
            'fm.delete': 'Delete',
            'fm.bytes': ' bytes',
            'fm.progress_file': 'File {i}/{n}: {p}%',
            'fm.done': 'Done!',
            'fm.delete_all': 'Delete all',
            'fm.confirm_delete_all': 'Permanently delete ALL files in {path}?\n\nThis cannot be undone. At the SPIFFS root it removes the web UI until the data/ folder is uploaded again. Sub-directories are kept.',
            'fm.err_delete_all_failed': 'Delete all failed: ',
            'fm.err_delete_all_some': 'Delete all: {n} file(s) could not be removed',

            // ---- Update page (pages_update) ------------------------------
            'upd.title': 'Firmware update',
            'upd.current_version': 'Current firmware version:',
            'upd.select_file_html': 'Please select a <b>.bin</b> firmware file:',
            'upd.flash': 'Flash',
            'upd.system_actions': 'System Actions',
            'upd.restart_esp': 'Restart ESP',
            'upd.wifi_reset': 'WiFi Reset',
            'upd.factory_reset': 'Factory Reset',
            'upd.storage_info': 'Storage Information',
            'upd.spiffs_total': 'SPIFFS Total:',
            'upd.spiffs_used': 'SPIFFS Used:',
            'upd.spiffs_free': 'SPIFFS Free:',
            'upd.nvs_total': 'NVS Total:',
            'upd.nvs_used': 'NVS Used:',
            'upd.nvs_free': 'NVS Free:',
            'upd.alert_select_bin': 'Please select a .bin file',
            'upd.uploading': 'Uploading...',
            'upd.upload_progress': 'Upload: {p}%',
            'upd.update_success': 'Update successful! ESP is restarting...',
            'upd.error_prefix': 'Error: ',
            'upd.confirm_restart': 'The ESP will be restarted.',
            'upd.restarting': 'Restarting',
            'upd.confirm_wifi_reset': 'WiFi settings will be deleted. Continue?',
            'upd.wifi_reset_done': 'WiFi reset done. ESP is restarting.',
            'upd.confirm_factory_reset': 'ALL user data will be deleted!\nPerform factory reset?',
            'upd.factory_reset_done': 'Factory reset done. ESP is restarting.',
            'upd.bytes': ' Bytes',
            'upd.pct_of_100': '% of 100%',
            'upd.pct_used': '% used',
            'upd.pct_free': '% free',
            'upd.entries': ' entries',

            // ---- Data export page (pages_dataexport) ---------------------
            'exp.title': 'Data export',
            'exp.no_storage_html': '<strong>No storage available</strong> — SD card not mounted or not formatted as FAT32.',
            'exp.battery_export': 'Battery data export',
            'exp.intro': 'Select what to export. Module data and Cells data can run together in one export, inside the From/To window (From empty = start now, To empty = until stopped).',
            'exp.module_data': 'Module data',
            'exp.cells_data': 'Cells data',
            'exp.from_optional': 'From (optional — start)',
            'exp.to_optional': 'To (optional — end)',
            'exp.modules_label': 'Modules (comma-separated, empty = all)',
            'exp.modules_placeholder': 'e.g. 1,2,3',
            'exp.records_per_file': 'Records per file (100–100000)',
            'exp.bat_warning': 'Cells data must be enabled on the Cells page',
            'exp.export_btn': 'Export',
            'exp.history_btn': 'History',
            'exp.export_running': 'Export running:',
            'exp.options_locked': '— options are locked until it is stopped.',
            'exp.query_interval_default': 'Note: Module data / Cells data exports use the query interval configured in Battery data / Cells pages.',
            'exp.bms_export': 'BMS data export',
            'exp.history_intro': "History writes the battery's datalist response verbatim, for one module. It starts immediately and has no time range.",
            'exp.module_number': 'Module number',
            'exp.export_timeout': 'Max export duration (s) (60–3600)',
            'exp.progress_title': 'Export Progress',
            'exp.status_label': 'Status:',
            'exp.exporting_label': 'Exporting:',
            'exp.files_created': 'Files created:',
            'exp.records_current': 'Records in current file:',
            'exp.current_file': 'Current file:',
            'exp.error_label': 'Error:',
            'exp.stop_hint': 'Stop the export with the button of the running export kind.',
            'exp.info_title': 'Info',
            'exp.info_p1': 'Module data and Cells data exports write one CSV file per module and rotate to the next file when the configured records per file is reached. Running both together keeps the module data and the cells data files of a module side by side. Rows are recorded live and only within the configured From/To time window: an empty "From" starts the export now, an empty "To" runs until stopped manually. There is no backfill.',
            'exp.info_p3': 'History exports write the BMS "datalist" command response verbatim, without added headers.',
            'exp.info_p4': 'Only one export can run at a time.',
            'exp.stop_export': 'Stop export',
            'exp.query_interval_note': 'Note: rows are written live, one per module and query interval (currently: Module data={pwr}s, Cells data={bat}s).',
            'exp.alert_select_kinds': 'Select Module data, Cells data or both before exporting.',
            'exp.alert_module_required': 'Module number required',
            'exp.alert_export_failed': 'Export failed: ',
            'exp.alert_stop_failed': 'Stop failed: ',
            'exp.see_device_log': 'see device log',
            'exp.alert_completed': 'Export completed ({kinds}, {n} file(s) created)',
            'exp.alert_stopped': 'Export stopped',
            'exp.alert_timeout': 'Export timed out after {n}s',
            'exp.state_scheduled': 'scheduled (waiting for start time{when})',

            // ---- Console page (pages_console) ----------------------------
            'con.title': 'Battery Console',
            'con.command': 'command:',
            'con.send': 'send',
            'con.prompt': 'pylon>',

            // ---- Frame dump page (pages_framedump) -----------------------
            'fd.title': 'Frame Dump (Live)',
            'fd.start': 'Start',
            'fd.stop': 'Stop',
            'fd.clear': 'Clear',
            'fd.timestamp': '[{t}]',

            // ---- Battery stack/module status (server tokens, display only) --
            'status.Charging': 'Charging',
            'status.Discharging': 'Discharging',
            'status.Charge': 'Charge',
            'status.Dischg': 'Dischg',
            'status.Discharge': 'Discharge',
            'status.Idle': 'Idle',
            'status.Balance': 'Balance',
            'status.Balancing': 'Balancing',
            'status.Disconnected': 'Disconnected',
            'status.Alarm!': 'Alarm!',
            'status.Normal': 'Normal',
            'status.OK': 'OK',
            'status.Warning': 'Warning',
            'status.Error': 'Error',
            'status.Warning in modules': 'Warning in modules',
            'status.Error in modules': 'Error in modules',

            // ---- Health events (server tokens, display only) -------------
            'event.threshold_error': 'Error: threshold reached',
            'event.threshold_warning': 'Warning: threshold reached'
        },

        es: {
            // ---- SPA shell -----------------------------------------------
            'layout.title': 'Panel Pylontech',
            'layout.h1': 'Monitor Pylontech',
            'layout.toggle_menu': 'Alternar menú',
            'layout.auth_required': 'Se requiere autenticación.',
            'layout.error_loading': 'Error al cargar: ',
            'layout.mqtt_disabled': 'Deshabilitado',
            'layout.mqtt_active': 'Activo',
            'layout.mqtt_disconnected': 'Desconectado',

            // ---- Sidebar navigation --------------------------------------
            'nav.section_info': 'Información',
            'nav.section_mqtt': 'MQTT',
            'nav.section_config': 'Configuración',
            'nav.section_service': 'Servicio',
            'nav.dashboard': 'Panel',
            'nav.modules': 'Módulos',
            'nav.health': 'Salud',
            'nav.battery_data': 'Datos de batería',
            'nav.cells': 'Celdas',
            'nav.statistics': 'Estadísticas',
            'nav.settings': 'Ajustes',
            'nav.screen': 'Pantalla',
            'nav.log': 'Registro',
            'nav.data_export': 'Exportar datos',
            'nav.sd_card': 'Tarjeta SD',
            'nav.file_manager': 'Gestor de archivos',
            'nav.console': 'Consola',
            'nav.frame_dump': 'Volcado de tramas',
            'nav.update': 'Actualizar',

            // ---- Settings page: generic ----------------------------------
            'conn.settings': 'Ajustes',
            'conn.save': 'Guardar',
            'conn.password': 'Contraseña:',
            'conn.server': 'Servidor:',
            'conn.server_heading': 'Servidor',

            // ---- Settings page: WiFi -------------------------------------
            'conn.wifi': 'WiFi',
            'conn.wifi_loading': 'Cargando estado de WiFi…',
            'conn.wifi_disable': 'Desactivar WiFi',
            'conn.wifi_enable': 'Activar WiFi',
            'conn.wifi_boot_hint': 'Pulse el botón Boot del dispositivo para volver a activar el WiFi',
            'conn.wifi_scan': 'Escanear',
            'conn.wifi_manual': 'Modo manual',
            'conn.wifi_ssid': 'SSID:',
            'conn.connect': 'Conectar',
            'conn.connected': 'Conectado',
            'conn.enter_password': 'Introduzca la contraseña',
            'conn.wifi_disabling': 'El WiFi se está desactivando — esta página dejará de ser accesible.',
            'conn.wifi_enabled': 'WiFi activado',
            'conn.wifi_enable_failed': 'No se pudo activar el WiFi',

            // ---- Settings page: MQTT -------------------------------------
            'conn.mqtt': 'MQTT',
            'conn.mqtt_enabled': 'Activado:',
            'conn.port': 'Puerto:',
            'conn.user': 'Usuario:',
            'conn.mqtt_topic_prefix': 'Prefijo de tema:',

            // ---- Settings page: NTP / Time -------------------------------
            'conn.ntp_time': 'NTP / Hora',
            'conn.use_gateway_ntp': 'Usar NTP de la puerta de enlace',
            'conn.manual_ntp': 'Servidor NTP manual',
            'conn.manual_time': 'Hora manual',
            'conn.ntp_server': 'Servidor NTP:',
            'conn.date': 'Fecha:',
            'conn.time': 'Hora:',
            'conn.timezone': 'Zona horaria',
            'conn.region': 'Región:',
            'conn.city': 'Ciudad:',
            'conn.manual_time_no_ntp': 'Hora manual (sin NTP)',

            // ---- Settings page: IP configuration -------------------------
            'conn.ip_config': 'Configuración IP',
            'conn.dhcp': 'DHCP',
            'conn.ip_address': 'Dirección IP:',
            'conn.subnet_mask': 'Máscara de subred:',
            'conn.gateway': 'Puerta de enlace:',
            'conn.dns': 'DNS:',

            // ---- Settings page: Automatic restart ------------------------
            'conn.automatic_restart': 'Reinicio automático',
            'conn.automatic_reboot': 'Reinicio automático',
            'conn.reboot_time': 'Hora de reinicio:',
            'conn.reboot_every': 'Reiniciar cada:',
            'conn.days': 'día(s)',
            'conn.restart_hint': 'Reinicia a la hora configurada cada N días. Requiere una hora del sistema válida (NTP o manual). Predeterminado: diario a las 03:00.',

            // ---- Settings page: Config password --------------------------
            'conn.config_password': 'Contraseña de configuración',
            'conn.password_hint': 'Máximo 20 caracteres alfanuméricos. Déjelo en blanco para mantener la contraseña actual. Predeterminado: 12345.',

            // ---- Settings page: Language ---------------------------------
            'conn.language': 'Idioma',
            'conn.language_saved': 'Idioma guardado',

            // ---- Settings page: server response mapping ------------------
            'conn.msg_connecting': 'Conectando…',
            'conn.msg_mqtt_saved': 'MQTT guardado',
            'conn.msg_time_saved': 'Hora guardada',
            'conn.msg_network_saved': 'Red guardada',
            'conn.msg_password_saved': 'Contraseña guardada',
            'conn.msg_password_unchanged': 'Contraseña sin cambios',
            'conn.msg_restart_saved': 'Reinicio automático guardado',
            'conn.msg_missing_body': 'Falta el cuerpo de la solicitud',
            'conn.msg_invalid_json': 'JSON no válido',
            'conn.msg_ssid_missing': 'Falta el SSID',
            'conn.msg_max_20': 'Máximo 20 caracteres',
            'conn.msg_alnum': 'Solo caracteres alfanuméricos (A-Z a-z 0-9)',
            'conn.msg_time_format': 'La hora debe tener el formato HH:MM (00-23 / 00-59)',
            'conn.msg_days_range': 'Los días deben estar entre 1 y 365',
            'conn.msg_unsupported_language': 'Idioma no admitido',

            // ---- Settings page: local validation -------------------------
            'conn.err_password_charset': 'Máximo 20 caracteres, solo letras y números',
            'conn.err_time_format': 'La hora debe tener el formato HH:MM',
            'conn.err_days_range': 'Los días deben estar entre 1 y 365',

            // ---- Dashboard page (pages_dashboard) ------------------------
            'dash.title': 'Panel',
            'dash.battery': 'Batería',
            'dash.battery_overview': 'Resumen de la batería',
            'dash.health': 'Salud',
            'dash.modules': 'Módulos',
            'dash.wifi': 'WiFi',
            'dash.mqtt': 'MQTT',
            'dash.cpu_memory': 'CPU y memoria',
            'dash.system': 'Sistema',
            'dash.status': 'Estado:',
            'dash.soc': 'SOC:',
            'dash.voltage': 'Voltaje:',
            'dash.current': 'Corriente:',
            'dash.power': 'Potencia:',
            'dash.temperature': 'Temperatura:',
            'dash.cell_min': 'Celda mín.:',
            'dash.cell_max': 'Celda máx.:',
            'dash.max_delta': 'Delta máx.:',
            'dash.avg_soh': 'SOH promedio:',
            'dash.events_history': 'Eventos en el historial:',
            'dash.detected_modules': 'Módulos detectados:',
            'dash.last_update': 'Última actualización:',
            'dash.mode': 'Modo:',
            'dash.ssid': 'SSID:',
            'dash.ip': 'IP:',
            'dash.rssi': 'RSSI:',
            'dash.quality': 'Calidad:',
            'dash.server': 'Servidor:',
            'dash.last_contact': 'Último contacto:',
            'dash.core0': 'Núcleo 0:',
            'dash.core1': 'Núcleo 1:',
            'dash.heap': 'Heap:',
            'dash.psram': 'PSRAM:',
            'dash.sd_free': 'SD libre:',
            'dash.time': 'Hora:',
            'dash.uptime': 'Tiempo activo:',
            'dash.firmware': 'Firmware:',
            'dash.qual_high': 'Alta',
            'dash.qual_medium': 'Media',
            'dash.qual_low': 'Baja',
            'dash.qual_very_low': 'Muy baja',
            'dash.no_card': 'Sin tarjeta',
            'dash.free': 'libres',

            // ---- Modules page (pages_modules) ----------------------------
            'mod.stack_title': 'Pila de módulos',
            'mod.modules': 'Módulos',
            'mod.module': 'Módulo',
            'mod.status': 'Estado',
            'mod.soc': 'SOC',
            'mod.voltage': 'Voltaje',
            'mod.current': 'Corriente',
            'mod.temperature': 'Temperatura',
            'mod.cell_delta': 'Delta de celdas',
            'mod.statistics': 'Estadísticas',
            'mod.soh': 'SOH',
            'mod.cycles': 'Ciclos',
            'mod.total_discharged': 'Total descargado',
            'mod.ov': 'OV',
            'mod.hv': 'HV',
            'mod.lv': 'LV',
            'mod.uv': 'UV',
            'mod.bms_warnings': 'Advertencias del BMS',
            'mod.bms_alarms': 'Alarmas del BMS',
            'mod.cell_voltages': 'Voltajes de celdas',
            'mod.cell_socs': 'SOC de celdas',
            'mod.legend_stats': 'SOH: estado de salud &middot; OV: recuento de sobretensión &middot; HV: recuento de tensión alta &middot; LV: recuento de tensión baja &middot; UV: recuento de subtensión',
            'mod.legend_voltage': '<span style="color:#ff0000; font-weight:bold;">Rojo</span> = celda más alta del módulo &middot; <span style="color:#0000ff; font-weight:bold;">Azul</span> = celda más baja del módulo',
            'mod.stat_disabled': 'Muestreador STAT deshabilitado. Pulse Actualizar para obtener valores actualizados.',
            'mod.refresh': 'Actualizar',
            'mod.cells_interval_hint': 'Los valores se actualizarán según el valor del intervalo de consulta en la página Celdas',
            'mod.hdr_batt_soc': 'SOC de la batería:',
            'mod.hdr_status': 'Estado:',
            'mod.hdr_voltage': 'Voltaje:',
            'mod.hdr_current': 'Corriente:',
            'mod.hdr_modules': 'Módulos:',
            'mod.hdr_update': 'Actualización:',
            'mod.err_bat_load': 'Error al cargar BAT: ',
            'mod.err_cell_read': 'Error al leer celdas: ',
            'mod.err_cells_read': 'Error al leer celdas: ',
            'mod.err_statistics_read': 'Error al leer estadísticas: ',
            'mod.err_modules_read': 'Error al leer módulos: ',
            'mod.err_stat_load': 'Error al cargar STAT: ',

            // ---- Health page (pages_health) ------------------------------
            'health.title': 'Estado de salud',
            'health.modules': 'Módulos',
            'health.history': 'Historial',
            'health.module': 'Módulo',
            'health.status': 'Estado',
            'health.temp': 'Temp.',
            'health.cell_min': 'Celda mín.',
            'health.cell_max': 'Celda máx.',
            'health.delta': 'Delta',
            'health.cell_min_today': 'Celda mín. de hoy',
            'health.cell_max_today': 'Celda máx. de hoy',
            'health.delta_max_today': 'Delta máx. de hoy',
            'health.date': 'Fecha',
            'health.event': 'Evento',
            'health.reset': 'Restablecer',
            'health.thresholds_title': 'Ajustes de umbrales de delta',
            'health.warning_threshold': 'Umbral de advertencia (V):',
            'health.error_threshold': 'Umbral de error (V):',
            'health.save': 'Guardar',
            'health.prefix': 'Salud: ',
            'health.err_reset_auth': 'Se requiere autenticación. No se aplicó el restablecimiento.',
            'health.err_save_auth': 'Se requiere autenticación. No se guardó.',

            // ---- Screen page (pages_screen) ------------------------------
            'screen.title': 'Pantalla',
            'screen.display': 'Pantalla',
            'screen.enabled': 'Pantalla activada:',
            'screen.touch': 'Táctil activo:',
            'screen.dark': 'Modo oscuro:',
            'screen.brightness': 'Brillo:',
            'screen.refresh': 'Actualización mín. (s):',
            'screen.refresh_hint': 'segundos; 0 = actualizar en cada actualización. Controla el parpadeo de la pantalla',
            'screen.wifi_warn': 'El WiFi no está conectado y el dispositivo no está en modo AP — no se puede apagar la pantalla.',
            'screen.save': 'Guardar',
            'screen.power_control': 'Control de energía',
            'screen.schedule': 'Programación:',
            'screen.screen_off': 'Apagar pantalla:',
            'screen.screen_on': 'Encender pantalla:',
            'screen.time_warn': 'La programación está activa pero la hora del sistema no es válida (NTP no sincronizado o sin hora manual) — el control horario no se activará.',
            'screen.auto_off': 'Apagado automático:',
            'screen.auto_off_hint': 'La pantalla se apaga automáticamente tras el tiempo configurado sin ningún toque. Un toque la vuelve a encender, mostrando la vista que tenía al apagarse.',
            'screen.msg_saved': 'Configuración de pantalla guardada',
            'screen.msg_missing_body': 'Falta el cuerpo de la solicitud',
            'screen.msg_invalid_json': 'JSON no válido',
            'screen.msg_wifi_unavailable': 'No se puede apagar la pantalla mientras el WiFi no esté disponible',

            // ---- Log page (pages_log) ------------------------------------
            'log.title': 'Registro',
            'log.loading': 'cargando...',
            'log.reload': 'Recargar',
            'log.level_filter': 'Filtro de nivel de registro',
            'log.info': 'Info',
            'log.warn': 'Advertencia',
            'log.error': 'Error',
            'log.debug': 'Depuración',
            'log.save': 'Guardar',
            'log.err_loading': 'Error al cargar el registro',
            'log.msg_saved': 'Nivel de registro actualizado',
            'log.msg_missing_body': 'Falta el cuerpo de la solicitud',
            'log.msg_invalid_json': 'JSON no válido',

            // ---- Battery data page (pages_basevalue) ---------------------
            'bv.title': 'Datos de batería',
            'bv.mqtt': 'MQTT:',
            'bv.temp_settings': 'Ajustes de temperatura',
            'bv.fahrenheit': 'Enviar temperatura en Fahrenheit',
            'bv.mqtt_subtopics': 'MQTT Subtopics',
            'bv.topic_pwr': 'Topic PWR',
            'bv.topic_stack': 'Topic Stack',
            'bv.query_interval': 'Intervalo de consulta',
            'bv.pwr_seconds': 'PWR (segundos)',
            'bv.pwr_parser_fields': 'Campos del muestreador PWR',
            'bv.field_name': 'Nombre del campo',
            'bv.display_name': 'Nombre mostrado',
            'bv.raw_data': 'Datos sin procesar',
            'bv.factor': 'Factor',
            'bv.unit': 'Unidad',
            'bv.mqtt_col': 'MQTT',
            'bv.save': 'Guardar',
            'bv.msg_saved': 'Configuración de PWR guardada',
            'bv.msg_missing_body': 'Falta el cuerpo de la solicitud',
            'bv.msg_invalid_json': 'JSON no válido',

            // ---- Cell data page (pages_celldata) -------------------------
            'cell.title': 'Datos de celdas',
            'cell.mqtt': 'MQTT:',
            'cell.parser': 'Muestreador de celdas (BAT)',
            'cell.enable': 'Activar',
            'cell.mqtt_subtopics': 'MQTT Subtopics',
            'cell.topic_bat': 'Topic BAT',
            'cell.cell_prefix': 'Prefijo de celda',
            'cell.query_interval': 'Intervalo de consulta',
            'cell.bat_seconds': 'BAT (segundos)',
            'cell.cell_values': 'Valores de celdas',
            'cell.field_name': 'Nombre del campo',
            'cell.display_name': 'Nombre mostrado',
            'cell.raw_data': 'Datos sin procesar',
            'cell.factor': 'Factor',
            'cell.unit': 'Unidad',
            'cell.mqtt_col': 'MQTT',
            'cell.save': 'Guardar',
            'cell.interval_hint_one': 'Recomendado: al menos {sec} s (1 módulo x 5 s).',
            'cell.interval_hint_many': 'Recomendado: al menos {sec} s ({n} módulos x 5 s).',
            'cell.msg_saved': 'Configuración de BAT guardada',
            'cell.msg_saved_warning': 'Configuración de BAT guardada. Advertencia: con {n} módulo(s) el intervalo de BAT debería ser de al menos {sec} s (5 s por módulo); un intervalo más corto puede fragmentar el heap durante una exportación a SD.',
            'cell.msg_missing_body': 'Falta el cuerpo de la solicitud',
            'cell.msg_invalid_json': 'JSON no válido',
            'cell.msg_export_running': 'No se puede desactivar el muestreador BAT mientras hay una exportación BAT en curso',
            'cell.msg_interval_min': 'El intervalo de consulta BAT debe ser de al menos 10 segundos',

            // ---- Statistics page (pages_statistic) -----------------------
            'stat.title': 'Estadísticas de la batería',
            'stat.mqtt': 'MQTT:',
            'stat.parser': 'Muestreador de estadísticas (STAT)',
            'stat.mqtt_subtopic': 'MQTT Subtopic',
            'stat.topic_stat': 'Topic STAT',
            'stat.query_interval': 'Intervalo de consulta',
            'stat.stat_seconds': 'STAT (segundos)',
            'stat.save': 'Guardar',
            'stat.field_name': 'Nombre del campo',
            'stat.display_name': 'Nombre mostrado',
            'stat.raw_data': 'Datos sin procesar',
            'stat.factor': 'Factor',
            'stat.unit': 'Unidad',
            'stat.mqtt_col': 'MQTT',
            'stat.msg_saved': 'Configuración de STAT guardada',
            'stat.msg_missing_body': 'Falta el cuerpo de la solicitud',
            'stat.msg_stale': 'Página de estadísticas obsoleta: recargue la interfaz (Ctrl+F5)',
            'stat.msg_invalid_json': 'JSON no válido: ',

            // ---- SD card page (pages_sdcard) -----------------------------
            'sd.title': 'Tarjeta SD',
            'sd.upload': 'Subir',
            'sd.refresh': 'Actualizar',
            'sd.no_card': 'No hay tarjeta SD insertada o no está montada.',
            'sd.col_name': 'Nombre',
            'sd.col_size': 'Tamaño',
            'sd.col_action': 'Acción',
            'sd.download': 'Descargar',
            'sd.delete': 'Eliminar',
            'sd.bytes': ' bytes',
            'sd.progress_file': 'Archivo {i}/{n}: {p}%',
            'sd.done': 'Completado',
            'sd.confirm_delete': '¿Eliminar {path}?',
            'sd.err_invalid_response': 'respuesta no válida',
            'sd.err_load_files': 'Error al cargar archivos: ',
            'sd.err_load_status': 'Error al cargar el estado de la tarjeta SD: ',
            'sd.err_network': 'error de red',
            'sd.err_upload_failed': 'Error al subir: ',
            'sd.err_delete_failed': 'Error al eliminar: ',
            'sd.delete_all': 'Eliminar todo',
            'sd.confirm_delete_all': '¿Eliminar permanentemente TODOS los archivos en {path}?\n\nEsto no se puede deshacer. Las subcarpetas se conservan.',
            'sd.err_delete_all_failed': 'Error al eliminar todo: ',
            'sd.err_delete_all_some': 'Eliminar todo: no se pudieron eliminar {n} archivo(s)',

            // ---- File manager page (pages_filemanager) -------------------
            'fm.title': 'Gestor de archivos ESP32',
            'fm.upload': 'Subir',
            'fm.col_name': 'Nombre',
            'fm.col_size': 'Tamaño',
            'fm.col_action': 'Acción',
            'fm.download': 'Descargar',
            'fm.delete': 'Eliminar',
            'fm.bytes': ' bytes',
            'fm.progress_file': 'Archivo {i}/{n}: {p}%',
            'fm.done': 'Completado',
            'fm.delete_all': 'Eliminar todo',
            'fm.confirm_delete_all': '¿Eliminar permanentemente TODOS los archivos en {path}?\n\nEsto no se puede deshacer. En la raíz de SPIFFS elimina la interfaz web hasta que se vuelva a subir la carpeta data/. Las subcarpetas se conservan.',
            'fm.err_delete_all_failed': 'Error al eliminar todo: ',
            'fm.err_delete_all_some': 'Eliminar todo: no se pudieron eliminar {n} archivo(s)',

            // ---- Update page (pages_update) ------------------------------
            'upd.title': 'Actualización de firmware',
            'upd.current_version': 'Versión de firmware actual:',
            'upd.select_file_html': 'Seleccione un archivo de firmware <b>.bin</b>:',
            'upd.flash': 'Flashear',
            'upd.system_actions': 'Acciones del sistema',
            'upd.restart_esp': 'Reiniciar ESP',
            'upd.wifi_reset': 'Restablecer WiFi',
            'upd.factory_reset': 'Restablecer de fábrica',
            'upd.storage_info': 'Información de almacenamiento',
            'upd.spiffs_total': 'SPIFFS total:',
            'upd.spiffs_used': 'SPIFFS usado:',
            'upd.spiffs_free': 'SPIFFS libre:',
            'upd.nvs_total': 'NVS total:',
            'upd.nvs_used': 'NVS usado:',
            'upd.nvs_free': 'NVS libre:',
            'upd.alert_select_bin': 'Seleccione un archivo .bin',
            'upd.uploading': 'Subiendo...',
            'upd.upload_progress': 'Subida: {p}%',
            'upd.update_success': '¡Actualización correcta! El ESP se está reiniciando...',
            'upd.error_prefix': 'Error: ',
            'upd.confirm_restart': 'El ESP se reiniciará.',
            'upd.restarting': 'Reiniciando',
            'upd.confirm_wifi_reset': 'Se eliminarán los ajustes de WiFi. ¿Continuar?',
            'upd.wifi_reset_done': 'Restablecimiento de WiFi completado. El ESP se está reiniciando.',
            'upd.confirm_factory_reset': '¡Se eliminarán TODOS los datos del usuario!\n¿Realizar el restablecimiento de fábrica?',
            'upd.factory_reset_done': 'Restablecimiento de fábrica completado. El ESP se está reiniciando.',
            'upd.bytes': ' Bytes',
            'upd.pct_of_100': '% de 100%',
            'upd.pct_used': '% usado',
            'upd.pct_free': '% libre',
            'upd.entries': ' entradas',

            // ---- Data export page (pages_dataexport) ---------------------
            'exp.title': 'Exportar datos',
            'exp.no_storage_html': '<strong>No hay almacenamiento disponible</strong> — tarjeta SD no montada o no formateada como FAT32.',
            'exp.battery_export': 'Exportación de datos de batería',
            'exp.intro': 'Seleccione qué exportar. Los datos de módulo y los datos de celdas pueden ejecutarse juntos en una exportación, dentro de la ventana Desde/Hasta (Desde vacío = iniciar ahora, Hasta vacío = hasta detenerse).',
            'exp.module_data': 'Datos de módulo',
            'exp.cells_data': 'Datos de celdas',
            'exp.from_optional': 'Desde (opcional — inicio)',
            'exp.to_optional': 'Hasta (opcional — fin)',
            'exp.modules_label': 'Módulos (separados por comas, vacío = todos)',
            'exp.modules_placeholder': 'p. ej. 1,2,3',
            'exp.records_per_file': 'Registros por archivo (100–100000)',
            'exp.bat_warning': 'Los datos de celdas deben estar habilitados en la página Celdas',
            'exp.export_btn': 'Exportar',
            'exp.history_btn': 'Historial',
            'exp.export_running': 'Exportación en curso:',
            'exp.options_locked': '— las opciones están bloqueadas hasta que se detenga.',
            'exp.query_interval_default': 'Nota: las exportaciones de datos de módulo / celdas usan el intervalo de consulta configurado en las páginas Datos de batería / Celdas.',
            'exp.bms_export': 'Exportación de datos BMS',
            'exp.history_intro': 'El historial escribe la respuesta de datalist de la batería tal cual, para un módulo. Se inicia de inmediato y no tiene rango de tiempo.',
            'exp.module_number': 'Número de módulo',
            'exp.export_timeout': 'Duración máxima de la exportación (s) (60–3600)',
            'exp.progress_title': 'Progreso de la exportación',
            'exp.status_label': 'Estado:',
            'exp.exporting_label': 'Exportando:',
            'exp.files_created': 'Archivos creados:',
            'exp.records_current': 'Registros en el archivo actual:',
            'exp.current_file': 'Archivo actual:',
            'exp.error_label': 'Error:',
            'exp.stop_hint': 'Detenga la exportación con el botón del tipo de exportación en curso.',
            'exp.info_title': 'Información',
            'exp.info_p1': 'Las exportaciones de datos de módulo y de celdas escriben un archivo CSV por módulo y rotan al siguiente archivo cuando se alcanzan los registros por archivo configurados. Ejecutar ambas juntas mantiene los archivos de datos de módulo y de celdas de un módulo en paralelo. Las filas se registran en vivo y solo dentro de la ventana de tiempo Desde/Hasta configurada: "Desde" vacío inicia la exportación ahora, "Hasta" vacío se ejecuta hasta hacer una parada manual. No hay relleno retroactivo.',
            'exp.info_p3': 'Las exportaciones del historial escriben la respuesta del comando "datalist" de la BMS tal cual, sin encabezados añadidos.',
            'exp.info_p4': 'Solo puede ejecutarse una exportación a la vez.',
            'exp.stop_export': 'Detener exportación',
            'exp.query_interval_note': 'Nota: las filas se escriben en vivo, una por módulo e intervalo de consulta (actualmente: Datos de módulo={pwr}s, Datos de celdas={bat}s).',
            'exp.alert_select_kinds': 'Seleccione Datos de módulo, Datos de celdas o ambos antes de exportar.',
            'exp.alert_module_required': 'Se requiere el número de módulo',
            'exp.alert_export_failed': 'Error de exportación: ',
            'exp.alert_stop_failed': 'Error al detener: ',
            'exp.see_device_log': 'ver el registro del dispositivo',
            'exp.alert_completed': 'Exportación completada ({kinds}, {n} archivo(s) creado(s))',
            'exp.alert_stopped': 'Exportación detenida',
            'exp.alert_timeout': 'La exportación agotó el tiempo tras {n}s',
            'exp.state_scheduled': 'programada (esperando la hora de inicio{when})',

            // ---- Console page (pages_console) ----------------------------
            'con.title': 'Consola de batería',
            'con.command': 'comando:',
            'con.send': 'enviar',
            'con.prompt': 'pylon>',

            // ---- Frame dump page (pages_framedump) -----------------------
            'fd.title': 'Volcado de tramas (en vivo)',
            'fd.start': 'Iniciar',
            'fd.stop': 'Detener',
            'fd.clear': 'Limpiar',
            'fd.timestamp': '[{t}]',

            // ---- Battery stack/module status (server tokens, display only) --
            'status.Charging': 'Cargando',
            'status.Discharging': 'Descargando',
            'status.Charge': 'Carga',
            'status.Dischg': 'Descarga',
            'status.Discharge': 'Descarga',
            'status.Idle': 'Reposo',
            'status.Balance': 'Equilibrando',
            'status.Balancing': 'Equilibrando',
            'status.Disconnected': 'Desconectado',
            'status.Alarm!': 'Alarma',
            'status.Normal': 'Normal',
            'status.OK': 'OK',
            'status.Warning': 'Aviso',
            'status.Error': 'Error',
            'status.Warning in modules': 'Aviso en módulos',
            'status.Error in modules': 'Error en módulos',

            // ---- Health events (server tokens, display only) -------------
            'event.threshold_error': 'Error: umbral superado',
            'event.threshold_warning': 'Aviso: umbral superado'
        }
    };

    function getCookie(name) {
        var match = document.cookie.match(new RegExp('(?:^|; )' + name + '=([^;]*)'));
        return match ? decodeURIComponent(match[1]) : null;
    }

    function setCookie(name, value) {
        document.cookie = name + '=' + encodeURIComponent(value) +
            '; path=/; max-age=31536000; SameSite=Lax';
    }

    function normalise(lang) {
        return lang === 'es' ? 'es' : 'en';
    }

    function resolveLang() {
        // The server injects an authoritative value; invalid placeholders (e.g.
        // "__LANG__" when /layout.html is opened directly) fall through to the
        // cookie.
        var injected = window.I18N_LANG;
        if (injected === 'en' || injected === 'es') return injected;
        return normalise(getCookie(COOKIE_NAME));
    }

    var I18N = {
        lang: resolveLang(),
        dict: dict
    };

    function t(key) {
        var table = I18N.dict[I18N.lang] || {};
        if (Object.prototype.hasOwnProperty.call(table, key)) {
            return table[key];
        }
        var en = I18N.dict.en || {};
        if (Object.prototype.hasOwnProperty.call(en, key)) {
            return en[key];
        }
        return key;
    }

    function applyI18n(root) {
        root = root || document;

        root.querySelectorAll('[data-i18n]').forEach(function (el) {
            el.textContent = t(el.getAttribute('data-i18n'));
        });

        root.querySelectorAll('[data-i18n-html]').forEach(function (el) {
            el.innerHTML = t(el.getAttribute('data-i18n-html'));
        });

        root.querySelectorAll('[data-i18n-ph]').forEach(function (el) {
            el.placeholder = t(el.getAttribute('data-i18n-ph'));
        });

        root.querySelectorAll('[data-i18n-title]').forEach(function (el) {
            el.title = t(el.getAttribute('data-i18n-title'));
        });

        root.querySelectorAll('[data-i18n-aria]').forEach(function (el) {
            el.setAttribute('aria-label', t(el.getAttribute('data-i18n-aria')));
        });
    }

    function setLang(lang) {
        I18N.lang = normalise(lang);
        setCookie(COOKIE_NAME, I18N.lang);
        document.documentElement.lang = I18N.lang;
        applyI18n(document);
    }

    // Translate a battery status token coming from the server (English
    // protocol wording) for display. Unknown tokens pass through unchanged,
    // so alarm-style free text (e.g. "Volt.St: OV") is preserved. A composite
    // status string (comma-joined, e.g. "Base.St: Dischg, Warning: threshold
    // reached") has each known token translated in place.
    function statusLabel(token) {
        if (token === null || token === undefined || token === '') return token;
        var table = I18N.dict[I18N.lang] || {};
        var s = String(token);

        // Exact match first (the common case: a single status token).
        if (Object.prototype.hasOwnProperty.call(table, 'status.' + s)) {
            return table['status.' + s];
        }

        // Composite status: translate known substrings, leave the rest (field
        // names, raw values) untouched. Longest tokens first so "Discharging"
        // is not partially rewritten by "Dischg".
        var out = s
            .split('Error: threshold reached').join(t('event.threshold_error'))
            .split('Warning: threshold reached').join(t('event.threshold_warning'))
            .split('Discharging').join(table['status.Discharging'])
            .split('Dischg').join(table['status.Dischg'])
            .split('Charging').join(table['status.Charging'])
            .split('Charge').join(table['status.Charge'])
            .split('Balance').join(table['status.Balance'])
            .split('Idle').join(table['status.Idle'])
            .split('Normal').join(table['status.Normal']);
        return out;
    }

    // Translate a health event text coming from the server. Threshold events
    // may appear alone or as part of a comma-joined status string, so replace
    // every occurrence; other text (e.g. "Volt.St: OV") passes through.
    function eventLabel(text) {
        if (text === null || text === undefined || text === '') return text;
        var out = String(text)
            .split('Error: threshold reached').join(t('event.threshold_error'))
            .split('Warning: threshold reached').join(t('event.threshold_warning'));
        return out;
    }

    window.I18N = I18N;
    window.t = t;
    window.applyI18n = applyI18n;
    window.setLang = setLang;
    window.statusLabel = statusLabel;
    window.eventLabel = eventLabel;

    // Reflect the resolved language on <html> immediately (the head script runs
    // before the body is parsed).
    document.documentElement.lang = I18N.lang;

    if (document.readyState === 'loading') {
        document.addEventListener('DOMContentLoaded', function () {
            applyI18n(document);
        });
    } else {
        applyI18n(document);
    }
})();
