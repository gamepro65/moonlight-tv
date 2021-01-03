#include "streaming_session.h"

#include "src/connection.h"
#include "src/config.h"
#include "src/platform.h"
#include "src/input/sdl.h"

#include <Limelight.h>

#include <SDL.h>

#include "libgamestream/client.h"
#include "libgamestream/errors.h"

#include "stream/audio/audio.h"
#include "stream/video/video.h"

#include "backend/computer_manager.h"

SDL_bool session_running = SDL_FALSE;
STREAMING_STATUS session_status = STREAMING_NONE;

SDL_Thread *streaming_thread;
SDL_mutex *lock;
SDL_cond *cond;

typedef struct
{
    SERVER_DATA *server;
    CONFIGURATION *config;
    int appId;
} STREAMING_REQUEST;

static int streaming_thread_action(void *data);

void streaming_init()
{
    streaming_thread = NULL;
    session_status = STREAMING_NONE;
    lock = SDL_CreateMutex();
    cond = SDL_CreateCond();
}

void streaming_destroy()
{
    streaming_interrupt();
    streaming_wait_for_stop();

    SDL_DestroyCond(cond);
    SDL_DestroyMutex(lock);
}

void streaming_begin(const char *addr, int app_id)
{
    CONFIGURATION *config = malloc(sizeof(CONFIGURATION));
    config_parse(0, NULL, config);

    PSERVER_DATA server = computer_manager_server_of(addr);

    STREAMING_REQUEST *req = malloc(sizeof(STREAMING_REQUEST));
    req->server = server;
    req->config = config;
    req->appId = app_id;
    streaming_thread = SDL_CreateThread(streaming_thread_action, "streaming", req);
}

void streaming_interrupt()
{
    SDL_LockMutex(lock);
    session_running = SDL_FALSE;
    SDL_CondSignal(cond);
    SDL_UnlockMutex(lock);
}

void streaming_wait_for_stop()
{
    fprintf(stderr, "streaming_wait_for_stop");
    if (streaming_thread == NULL)
    {
        return;
    }
    SDL_WaitThread(streaming_thread, NULL);
    streaming_thread = NULL;
}

bool streaming_running()
{
    return session_running == SDL_TRUE;
}

int streaming_thread_action(void *data)
{
    session_status = STREAMING_CONNECTING;
    STREAMING_REQUEST *req = data;
    PSERVER_DATA server = req->server;
    PCONFIGURATION config = req->config;
    int appId = req->appId;

    int gamepads = 0;
    // gamepads += sdl_gamepads;
    int gamepad_mask = 0;
    for (int i = 0; i < gamepads && i < 4; i++)
        gamepad_mask = (gamepad_mask << 1) + 1;

    int ret = gs_start_app(server, &config->stream, appId, config->sops, config->localaudio, gamepad_mask);
    if (ret < 0)
    {
        if (ret == GS_NOT_SUPPORTED_4K)
            fprintf(stderr, "Server doesn't support 4K\n");
        else if (ret == GS_NOT_SUPPORTED_MODE)
            fprintf(stderr, "Server doesn't support %dx%d (%d fps) or try --unsupported option\n", config->stream.width, config->stream.height, config->stream.fps);
        else if (ret == GS_NOT_SUPPORTED_SOPS_RESOLUTION)
            fprintf(stderr, "SOPS isn't supported for the resolution %dx%d, use supported resolution or add --nosops option\n", config->stream.width, config->stream.height);
        else if (ret == GS_ERROR)
            fprintf(stderr, "Gamestream error: %s\n", gs_error);
        else
            fprintf(stderr, "Errorcode starting app: %d\n", ret);
        return -1;
    }

    int drFlags = 0;

    if (config->debug_level > 0)
    {
        printf("Stream %d x %d, %d fps, %d kbps\n", config->stream.width, config->stream.height, config->stream.fps, config->stream.bitrate);
    }

    LiStartConnection(&server->serverInfo, &config->stream, &connection_callbacks, platform_get_video(NONE), platform_get_audio(NONE, config->audio_device), NULL, drFlags, config->audio_device, 0);
    session_running = true;
    session_status = STREAMING_STREAMING;

    SDL_LockMutex(lock);
    while (session_running)
    {
        // Wait until interrupted
        SDL_CondWait(cond, lock);
    }
    SDL_UnlockMutex(lock);

    session_status = STREAMING_DISCONNECTING;
    LiStopConnection();

    gs_quit_app(server);

    // if (config->quitappafter)
    // {
    //     if (config->debug_level > 0)
    //         printf("Sending app quit request ...\n");
    //     gs_quit_app(server);
    // }
    streaming_thread = NULL;

    session_status = STREAMING_NONE;
    return 0;
}

STREAMING_STATUS streaming_status()
{
    return session_status;
}

PAUDIO_RENDERER_CALLBACKS platform_get_audio(enum platform system, char *audio_device)
{
#ifdef OS_WEBOS
    return &audio_callbacks_ndl;
#else
#error "No supported callbacks for this platform"
#endif
}

PDECODER_RENDERER_CALLBACKS platform_get_video(enum platform system)
{
#ifdef OS_WEBOS
    return &decoder_callbacks_ndl;
#else
#error "No supported callbacks for this platform"
#endif
}