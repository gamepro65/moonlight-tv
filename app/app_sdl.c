#include <stdbool.h>
#include <SDL.h>

#ifdef USE_NDL
#include <NDL_directmedia.h>
#endif

#include "app.h"
#include "main.h"

#include "ui/config.h"

#define NK_IMPLEMENTATION
#include "nuklear/config.h"
#include "nuklear.h"
#include "nuklear/ext_widgets.h"
#include "nuklear/ext_functions.h"
#include "nuklear/ext_styling.h"

#if defined(NK_SDL_GLES2)
#define NK_SDL_GLES2_IMPLEMENTATION
#include "nuklear/platform_sdl_gles2.h"
#elif defined(NK_SDL_GL2)
#define NK_SDL_GL2_IMPLEMENTATION
#include <SDL_opengl.h>
#include "nuklear/platform_sdl_gl2.h"
#else
#error "No valid render backend specified"
#endif

#include "backend/backend_root.h"
#include "stream/input/absinput.h"
#include "stream/input/sdlinput.h"
#include "platform/sdl/events.h"
#include "ui/gui_root.h"
#include "ui/config.h"

#define MAX_VERTEX_MEMORY 512 * 1024
#define MAX_ELEMENT_MEMORY 128 * 1024

/* Platform */
SDL_Window *win;
SDL_GLContext glContext;
static char wintitle[32];


int app_init(int argc, char *argv[])
{
#ifdef USE_NDL
    if (NDL_DirectMediaInit(WEBOS_APPID, NULL))
    {
        SDL_Log("Unable to initialize NDL\n", NDL_DirectMediaGetError());
        return -1;
    }
#endif
    return 0;
}

APP_WINDOW_CONTEXT app_window_create()
{

    nk_platform_gl_setup();
    SDL_SetHint("SDL_WEBOS_ACCESS_POLICY_KEYS_BACK", "true");
    SDL_SetHint("SDL_WEBOS_CURSOR_SLEEP_TIME", "5000");
    win = SDL_CreateWindow("Moonlight", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    glContext = SDL_GL_CreateContext(win);

    /* OpenGL setup */
    glViewport(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    return win;
}

void app_destroy()
{
#ifdef OS_WEBOS
#ifdef USE_NDL
    NDL_DirectMediaQuit();
#endif
#endif
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

static void app_process_events(struct nk_context *ctx)
{
    /* Input */
    SDL_Event evt;
    nk_input_begin(ctx);
    while (SDL_PollEvent(&evt))
    {
        bool block_steam_inputevent = false;
        if (SDL_IS_INPUT_EVENT(evt))
        {
            // Those are input events
            gui_dispatch_inputevent(ctx, evt);
            block_steam_inputevent |= gui_should_block_input();
        }
        else if (evt.type == SDL_USEREVENT)
        {
            backend_dispatch_userevent(evt.user.code, evt.user.data1, evt.user.data2);
            gui_dispatch_userevent(evt.user.code);
        }
        else if (evt.type == SDL_QUIT)
        {
            request_exit();
        }
        if (!block_steam_inputevent)
        {
            absinput_dispatch_event(evt);
        }
        nk_sdl_handle_event(&evt);
    }
    nk_input_end(ctx);
}

void app_main_loop(void *data)
{
    static Uint32 last_ticks = 0, framecount = 0;
    struct nk_context *ctx = (struct nk_context *)data;

    app_process_events(ctx);

    bool cont = gui_root(ctx);

    /* Draw */
    {
        gui_background();
        /* 
         * IMPORTANT: `nk_sdl_render` modifies some global OpenGL state
         * with blending, scissor, face culling, depth test and viewport and
         * defaults everything back into a default state.
         * Make sure to either a.) save and restore or b.) reset your own state after
         * rendering the UI.
         */
#ifdef NK_SDL_GLES2_IMPLEMENTATION
        nk_sdl_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_MEMORY, MAX_ELEMENT_MEMORY);
#elif defined(NK_SDL_GL2_IMPLEMENTATION)
        nk_sdl_render(NK_ANTI_ALIASING_ON);
#endif
        SDL_GL_SwapWindow(win);
    }
    Uint32 ticks = SDL_GetTicks();
    if ((ticks - last_ticks) >= 1000)
    {
        sprintf(wintitle, "Moonlight | %d FPS", framecount);
        SDL_SetWindowTitle(win, wintitle);
        last_ticks = ticks;
        framecount = 0;
    }
    else
    {
        framecount++;
    }
    if (!cont)
    {
        request_exit();
    }
}