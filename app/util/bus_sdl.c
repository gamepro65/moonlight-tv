#include "bus.h"

#include <SDL.h>

void bus_pushevent(int which, void *data1, void *data2)
{
    SDL_Event ev;
    ev.type = SDL_USEREVENT;
    ev.user.code = which;
    ev.user.data1 = data1;
    ev.user.data2 = data2;
    SDL_PushEvent(&ev);
}