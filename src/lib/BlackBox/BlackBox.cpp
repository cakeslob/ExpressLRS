

#define BLACKBOX_SWITCH_CHAN 15

#define BLACKBOX_FILE_CNT 5
#define BLACKBOX_FILE_NAME_LIMIT     (BLACKBOX_FILE_CNT * 3) //31

static bool attempt_failed = false;

#define BLACKBOX_CHAN_CNT 8
#define BLACKBOX_ESC_CNT 8

#define LPF_FRACTIONAL_BITS 8
#define LPF_SCALING_FACTOR (1 << LPF_FRACTIONAL_BITS)
static inline void low_pass_filter(int32_t x, int32_t* prev, const int32_t alpha) {
    int32_t one_minus_alpha = LPF_SCALING_FACTOR - alpha;
    int32_t temp = (int32_t)alpha * x + (int32_t)one_minus_alpha * (*prev);
    (*prev) = (int32_t)(temp >> LPF_FRACTIONAL_BITS);
}

typedef struct
{
    uint16_t rc[BLACKBOX_CHAN_CNT];
    int32_t vbat;
    esctelem_t* esc_telem[BLACKBOX_ESC_CNT];
}
blackbox_data_t;

static blackbox_data_t data;

void shrew_handle404(AsyncWebServerRequest *request)
{
    String path = request->url();

    // Check if the file exists in SPIFFS
    if (SPIFFS.exists(path)) {
        // Open the file for reading
        File file = SPIFFS.open(path, "r");

        // Serve the file as an octet stream
        AsyncWebServerResponse *response = request->beginResponse(file, path, "application/octet-stream");
        response->addHeader("Content-Disposition", "attachment; filename=\"" + path + "\"");
        request->send(response);

        file.close();
        return true;
    }
    else if (path == "logs.html" || path == "logs.htm")
    {
        String html = "<html><head><title>Log Files</title></head><body>\r\n";
        html += "<h1>Log Files</h1><ul>\r\n";
        bool has = false;

        // Iterate through SPIFFS and list files that start with "log_"
        File root = SPIFFS.open("/");
        File file = root.openNextFile();
        while (file) {
            String fileName = file.name();
            if (fileName.startsWith("/log_")) {
                html += "<li><a href=\"" + fileName + "\" target=\"_blank\">" + filename + "</a></li>\r\n";
                has = true;
            }
            file = root.openNextFile();
        }

        if (has == false) {
            html += "<li>no log files available</li>\r\n";
        }

        // End HTML response
        html += "</ul></body></html>";

        // Send the HTML response
        request->send(200, "text/html", html);
        return true;
    }
    else {
        return false;
    }
}

void blackbox_start()
{
    File root = SPIFFS.open("/");
    File file = root.openNextFile();

    uint32_t file_num_exists = 0;
    uint8_t cnt = 0;

    while (file) {
        String fileName = file.name();
        if (fileName.startsWith("/log_") && fileName.endsWith(".txt")) {
            String numStr = fileName.substring(5, fileName.length() - 4);
            char* endPtr;
            long number = strtol(numStr.c_str(), &endPtr, 10);
            if (*endPtr == '\0' && number > 0) {  // Valid number
                number--;
                if (number < (BLACKBOX_FILE_NAME_LIMIT + 1) && number >= 0) {
                    file_num_exists |= 1 << number;
                    cnt++;
                }
            }
        }
        file = root.openNextFile();
    }
    int next_num = 0;
    if (cnt > 0)
    {
        //int first_file_num = -1;
        int last_file_num = -1;
        if ((file_num_exists & (1 | (1 << BLACKBOX_FILE_NAME_LIMIT))) == (1 | (1 << BLACKBOX_FILE_NAME_LIMIT)))
        {
            for (int i = 1; i < BLACKBOX_FILE_NAME_LIMIT; i++) {
                if ((file_num_exists & (1 << i)) == 0) {
                    last_file_num = i - 1;
                    //for (int j = i; j < BLACKBOX_FILE_NAME_LIMIT; j++) {
                    //    if ((file_num_exists & (1 << j)) != 0) {
                    //        first_file_num = j;
                    //        break;
                    //    }
                    //}
                    break;
                }
            }
        }
        else if ((file_num_exists & (1 << BLACKBOX_FILE_NAME_LIMIT)) != 0) {
            //for (int i = 0; i < BLACKBOX_FILE_NAME_LIMIT + 1; i++) {
            //    if ((file_num_exists & (1 << i)) != 0) {
            //        first_file_num = i;
            //        break;
            //    }
            //}
            last_file_num = BLACKBOX_FILE_NAME_LIMIT;
        }
        else if ((file_num_exists & 1) != 0) {
            //first_file_num = 0;
            for (int i = 1; i < BLACKBOX_FILE_NAME_LIMIT; i++) {
                if ((file_num_exists & (1 << i)) == 0) {
                    last_file_num = i - 1;
                    break;
                }
            }
        }
        if (cnt > BLACKBOX_FILE_CNT) {
            int f = last_file_num;
            for (int i = 0; i < BLACKBOX_FILE_CNT; i++) {
                f -= 1;
                if (f < 0) {
                    f = BLACKBOX_FILE_NAME_LIMIT;
                }
            }
            for (; cnt > BLACKBOX_FILE_CNT; cnt--) {
                String fileNameToRemove = "/log_" + String(f) + ".txt";
                SPIFFS.remove(fileNameToRemove);
                f -= 1;
                if (f < 0) {
                    f = BLACKBOX_FILE_NAME_LIMIT;
                }
            }
        }
        next_num = last_file_num + 1;
        if (next_num > BLACKBOX_FILE_NAME_LIMIT) {
            next_num = 0;
        }
    }
    next_num += 1;
    String newLogFileName = "/log_" + String(next_num) + ".txt";
    static File newLogFile = SPIFFS.open(newLogFileName, FILE_WRITE);
    if (newLogFile) {
        blackbox_file = &newLogFile;
        uint8_t str[256];
        uint8_t si = 0;
        si += sprintf(&(str[si]), "time (ms),");
        for (int i = 0; i < BLACKBOX_CHAN_CNT; i++) {
            si += sprintf(&(str[si]), "CH%u,", (i + 1));
        }
        si += sprintf(&(str[si]), "RSSI,");
        if (GPIO_ANALOG_VBAT != UNDEF_PIN) {
            si += sprintf(&(str[si]), "vbat,");
        }
        si += sprintf(&(str[si]), "ESC CH,ESC V,ESC A,ESC ERPM,ESC TEMP (C),");
        si += sprintf(&(str[si]), "\r\n");
        newLogFile.write(str);
    }
    else {
        blackbox_file = nullptr;
        attempt_failed = true;
    }
}

void blackbox_close()
{
    if (blackbox_file == NULL) {
        return;
    }
    blackbox_file->close();
}

void blackbox_flush()
{
    if (blackbox_file == NULL) {
        return;
    }
    blackbox_file->flush();
}

void blackbox_process()
{
    static uint32_t last_time = 0;
    static uint32_t last_flush_time = 0;

    // cannot save file, so don't bother
    if (attempt_failed) {
        return;
    }

    // a channel switch is assigned to activate or deactivate logging
    if (CRSF_to_US(ChannelData[BLACKBOX_SWITCH_CHAN]) < 1750) {
        if (last_time != 0 && blackbox_file != nullptr) {
            blackbox_file->flush(); // think of turning off this switch as "eject"
            last_time = 0;
        }
        return;
    }

    // make a new file if required
    if (blackbox_file == nullptr) {
        blackbox_start();
        if (attempt_failed) {
            return;
        }
    }

    uint32_t now = millis();

    // only log 5 times per second
    if ((now - last_time) < 200) {
        return;
    }

    blackbox_log(now);
    last_time = now;

    blackbox_resetForNext();

    if ((now - last_flush_time) >= 10000) {
        last_flush_time = now;
        blackbox_file->flush();
    }
}

void blackbox_resetForNext()
{
    for (int i = 0; i < BLACKBOX_ESC_CNT; i++)
    {
        esctelem_t* p = data.esc_telem[i];
        if (p == NULL) { // check existence
            continue;
        }
        // these items store the maximum value for each time interval
        p->current     = 0;
        p->erpm        = 0;
        p->temperature = 0;
    }
}

void blackbox_log_vbat(int32_t x)
{
    low_pass_filter(x, &(data.vbat), (const int32_t)(0.95 * LPF_SCALING_FACTOR));
}

void blackbox_log_ctrl()
{
    for (int i = 0; i < BLACKBOX_CHAN_CNT; i++) {
        data.rc[i] = CRSF_to_US(ChannelData[i]);
    }
}

void blackbox_log_esctelem(esctelem_t* x)
{
    if (x->ch >= BLACKBOX_ESC_CNT) {
        return;
    }
    if (data.esc_telem[x->ch] == NULL) {
        data.esc_telem[x->ch] = (esctelem_t*)malloc(sizeof(esctelem_t));
        if (data.esc_telem[x->ch] != NULL) {
            data.esc_telem[x->ch]->voltage = 0;
            data.esc_telem[x->ch]->current = 0;
            data.esc_telem[x->ch]->erpm = 0;
        }
    }
    esctelem_t* p = data.esc_telem[x->ch];
    if (p == NULL) {
        return;
    }
    p->current     = (x->current     > p->current    ) ? x->current     : p->current;     // take max
    p->erpm        = (x->erpm        > p->erpm       ) ? x->erpm        : p->erpm;        // take max
    p->temperature = (x->temperature > p->temperature) ? x->temperature : p->temperature; // take max
    p->voltage     = x->voltage; // AM32 already does a LPF on this data
}

void blackbox_log(uint32_t now)
{
    static uint8_t str[256];
    uint8_t i = 0;

    i += sprintf(&(str[i]), "%u,", now);

    for (int j = 0; j < BLACKBOX_CHAN_CNT; j++) {
        i += sprintf(&(str[i]), "%u,", data.rc[j]);
    }

    crsfLinkStatistics_t ls = *(crsfLinkStatistics_t *)((const void *)&CRSF::LinkStatistics);
    i += sprintf(&(str[i]), "%d,", ls.uplink_RSSI_1);

    if (GPIO_ANALOG_VBAT != UNDEF_PIN) {
        i += sprintf(&(str[i]), "%u,", data.vbat);
    }

    for (int j = 0; j < BLACKBOX_ESC_CNT; j++) {
        esctelem_t* p = data.esc_telem[j];
        if (p == NULL) {
            continue;
        }
        i += sprintf(&(str[i]), "%u,%u,%u,%u,%u,", j, p->voltage, p->current, p->erpm, p->temperature);
    }

    blackbox_file->write(str);
    if (blackbox_extraStr) {
        blackbox_file->write(blackbox_extraStr);
    }
    blackbox_file->write("\r\n");
}
