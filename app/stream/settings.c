#include "settings.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "ini.h"

#include "stream/platform.h"

#include "util/ini_ext.h"
#include "util/nullable.h"
#include "util/path.h"
#include "util/logging.h"
#include "util/i18n.h"

static void settings_initialize(const char *confdir, PCONFIGURATION config);

static bool settings_read(const char *filename, PCONFIGURATION config);

static void settings_write(const char *filename, PCONFIGURATION config);

static int find_ch_idx_by_config(int config);

static int find_ch_idx_by_value(const char *value);

static const char *serialize_audio_config(int config);

static int parse_audio_config(const char *value);

static int settings_parse(PCONFIGURATION config, const char *section, const char *name, const char *value);

static void set_string(char **field, const char *value);

static void set_int(int *field, const char *value);

struct audio_config {
    int configuration;
    const char *value;
};


PCONFIGURATION settings_load() {
    PCONFIGURATION config = malloc(sizeof(CONFIGURATION));
    char *confdir = path_pref(), *conffile = path_join(confdir, CONF_NAME_MOONLIGHT);
    settings_initialize(confdir, config);
    if (!settings_read(conffile, config)) {
        applog_w("Settings", "Failed to read settings %s", conffile);
    }
    free(conffile);
    free(confdir);
    return config;
}

void settings_save(PCONFIGURATION config) {
    char *confdir = path_pref(), *conffile = path_join(confdir, CONF_NAME_MOONLIGHT);
    settings_write(conffile, config);
    free(conffile);
    free(confdir);
}

void settings_free(PCONFIGURATION config) {
    free_nullable(config->decoder);
    free_nullable(config->audio_backend);
    free_nullable(config->audio_device);
    free_nullable(config->language);
    free(config);
}

void settings_initialize(const char *confdir, PCONFIGURATION config) {
    memset(config, 0, sizeof(CONFIGURATION));
    LiInitializeStreamConfiguration(&config->stream);

    config->stream.width = 1280;
    config->stream.height = 720;
    config->stream.fps = 60;
    config->stream.bitrate = settings_optimal_bitrate(1280, 720, 60);
    config->stream.packetSize = 1392;
    config->stream.streamingRemotely = STREAM_CFG_AUTO;
    config->stream.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    config->stream.supportsHevc = true;

    config->debug_level = 0;
    set_string(&config->language, "auto");
    set_string(&config->audio_backend, "auto");
    set_string(&config->decoder, "auto");
    config->audio_device = NULL;
    config->sops = true;
    config->localaudio = false;
    config->fullscreen = true;
    if (!config->fullscreen) {
        config->window_state.w = 1280;
        config->window_state.h = 720;
    }
    // TODO make this automatic
    config->unsupported = true;
    config->quitappafter = false;
    config->viewonly = false;
    config->rotate = 0;
    config->absmouse = true;
    config->virtual_mouse = false;
    config->stop_on_stall = false;
    path_join_to(config->key_dir, sizeof(config->key_dir), confdir, "key");
}

int settings_optimal_bitrate(int w, int h, int fps) {
    if (fps <= 0) {
        fps = 60;
    }
    int kbps = w * h / 150;
    switch (RES_MERGE(w, h)) {
        case RES_720P:
            kbps = 5000;
            break;
        case RES_1080P:
            kbps = 10000;
            break;
        case RES_1440P:
            kbps = 20000;
            break;
        case RES_4K:
            kbps = 25000;
            break;
    }
    int suggested_max = decoder_info.suggestedBitrate;
    if (!suggested_max) {
        suggested_max = decoder_info.maxFramerate;
    }
    int calculated = kbps * fps / 30;
    if (!suggested_max) {
        return calculated;
    }
    return calculated < suggested_max ? calculated : suggested_max;
}

bool settings_read(const char *filename, PCONFIGURATION config) {
    return ini_parse(filename, (ini_handler) settings_parse, config) == 0;
}

void settings_write(const char *filename, PCONFIGURATION config) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) return;
    ini_write_string(fp, "language", config->language);
    ini_write_bool(fp, "fullscreen", config->fullscreen);
    ini_write_int(fp, "debug_level", config->debug_level);

    ini_write_section(fp, "streaming");
    ini_write_int(fp, "width", config->stream.width);
    ini_write_int(fp, "height", config->stream.height);
    ini_write_int(fp, "net_fps", config->stream.fps);
    ini_write_int(fp, "bitrate", config->stream.bitrate);
    ini_write_int(fp, "packetsize", config->stream.packetSize);
    ini_write_int(fp, "rotate", config->rotate);
    ini_write_int(fp, "stop_on_stall", config->stop_on_stall);

    ini_write_section(fp, "host");
    ini_write_bool(fp, "sops", config->sops);
    ini_write_bool(fp, "localaudio", config->localaudio);
    ini_write_bool(fp, "quitappafter", config->quitappafter);
    ini_write_bool(fp, "viewonly", config->viewonly);

    ini_write_section(fp, "input");
    ini_write_bool(fp, "absmouse", config->absmouse);
    ini_write_bool(fp, "virtual_mouse", config->virtual_mouse);
    ini_write_bool(fp, "swap_abxy", config->swap_abxy);

    ini_write_section(fp, "video");
    ini_write_string(fp, "decoder", config->decoder);
    ini_write_bool(fp, "hdr", config->stream.enableHdr);
    ini_write_bool(fp, "hevc", config->stream.supportsHevc);

    ini_write_section(fp, "audio");
    ini_write_string(fp, "backend", config->audio_backend);
    if (config->audio_device && config->audio_device[0]) {
        ini_write_string(fp, "device", config->audio_device);
    }
    ini_write_string(fp, "surround", serialize_audio_config(config->stream.audioConfiguration));

    if (!config->fullscreen) {
        ini_write_section(fp, "window");
        ini_write_int(fp, "x", config->window_state.x);
        ini_write_int(fp, "y", config->window_state.y);
        ini_write_int(fp, "width", config->window_state.w);
        ini_write_int(fp, "height", config->window_state.h);
    }

    fclose(fp);
}

bool audio_config_valid(int config) {
    return find_ch_idx_by_config(config) >= 0;
}

const char *serialize_audio_config(int config) {
    return audio_configs[find_ch_idx_by_config(config)].value;
}

int parse_audio_config(const char *value) {
    int index = value ? find_ch_idx_by_value(value) : -1;
    if (index < 0)
        index = 0;
    return audio_configs[index].configuration;
}

int find_ch_idx_by_config(int config) {
    for (int i = 0; i < audio_config_len; i++) {
        if (audio_configs[i].configuration == config)
            return i;
    }
    return -1;
}

int find_ch_idx_by_value(const char *value) {
    for (int i = 0; i < audio_config_len; i++) {
        if (strcmp(audio_configs[i].value, value) == 0)
            return i;
    }
    return -1;
}

static int settings_parse(PCONFIGURATION config, const char *section, const char *name, const char *value) {
    if (INI_FULL_MATCH("streaming", "width")) {
        set_int(&config->stream.width, value);
    } else if (INI_FULL_MATCH("streaming", "height")) {
        set_int(&config->stream.height, value);
    } else if (INI_FULL_MATCH("streaming", "net_fps")) {
        set_int(&config->stream.fps, value);
    } else if (INI_FULL_MATCH("streaming", "bitrate")) {
        set_int(&config->stream.bitrate, value);
    } else if (INI_FULL_MATCH("streaming", "packetsize")) {
        set_int(&config->stream.packetSize, value);
    } else if (INI_FULL_MATCH("streaming", "rotate")) {
        set_int(&config->rotate, value);
    } else if (INI_FULL_MATCH("streaming", "stop_on_stall")) {
        config->stop_on_stall = INI_IS_TRUE(value);
    } else if (INI_NAME_MATCH("hevc")) {
        config->stream.supportsHevc = INI_IS_TRUE(value);
    } else if (INI_NAME_MATCH("hdr")) {
        config->stream.enableHdr = INI_IS_TRUE(value);
    } else if (INI_NAME_MATCH("surround")) {
        config->stream.audioConfiguration = parse_audio_config(value);
    } else if (INI_NAME_MATCH("sops")) {
        config->sops = INI_IS_TRUE(value);
    } else if (INI_NAME_MATCH("localaudio")) {
        config->localaudio = INI_IS_TRUE(value);
    } else if (INI_NAME_MATCH("quitappafter")) {
        config->quitappafter = INI_IS_TRUE(value);
    } else if (INI_NAME_MATCH("viewonly")) {
        config->viewonly = INI_IS_TRUE(value);
    } else if (INI_NAME_MATCH("absmouse")) {
        config->absmouse = INI_IS_TRUE(value);
    } else if (INI_NAME_MATCH("virtual_mouse")) {
        config->virtual_mouse = INI_IS_TRUE(value);
    } else if (INI_NAME_MATCH("swap_abxy")) {
        config->swap_abxy = INI_IS_TRUE(value);
    } else if (INI_FULL_MATCH("video", "decoder")) {
        set_string(&config->decoder, value);
    } else if (INI_FULL_MATCH("audio", "backend")) {
        set_string(&config->audio_backend, value);
    } else if (INI_FULL_MATCH("audio", "device")) {
        set_string(&config->audio_device, value);
    } else if (INI_NAME_MATCH("language")) {
        set_string(&config->language, value);
    } else if (INI_NAME_MATCH("fullscreen")) {
#if TARGET_WEBOS
        config->fullscreen = true;
#else
        config->fullscreen = INI_IS_TRUE(value);
#endif
    } else if (INI_NAME_MATCH("debug_level")) {
        set_int(&config->debug_level, value);
    } else if (INI_FULL_MATCH("window", "x")) {
        set_int(&config->window_state.x, value);
    } else if (INI_FULL_MATCH("window", "y")) {
        set_int(&config->window_state.y, value);
    } else if (INI_FULL_MATCH("window", "width")) {
        set_int(&config->window_state.w, value);
    } else if (INI_FULL_MATCH("window", "height")) {
        set_int(&config->window_state.h, value);
    }
    return 1;
}

static void set_string(char **field, const char *value) {
    free_nullable(*field);
    *field = value != NULL ? strdup(value) : NULL;
}

static void set_int(int *field, const char *value) {
    if (value == NULL) {
        *field = 0;
        return;
    }
    errno = 0;
    int val = (int) strtol(value, NULL, 10);
    if (errno != 0) {
        *field = 0;
        return;
    }
    *field = val;
}