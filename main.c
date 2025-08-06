// cc -O3 main.c -lwayland-client -lwayland-egl -lEGL -lGLESv2 -o main
// Requires: wayland, wayland-protocols, mesa (or nvidia-egl + egl-wayland)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-shell-protocol.c"

#include <wayland-egl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#define CHECK(x,msg) do{ if(!(x)){fprintf(stderr,"%s\n",msg); exit(1);} }while(0)
#define EGLCHK(x)    CHECK((x),"EGL error")

/* globals --------------------------------------------------------------- */
static struct wl_display     *dpy;
static struct wl_compositor  *comp;
static struct xdg_wm_base    *wm;
static int width = 640, height = 480, configured = 0;

/* helpers --------------------------------------------------------------- */
static void naptick(void){ struct timespec t={0,16*1000000}; nanosleep(&t,0); }

static void paint(void){
    //glViewport(0,0,width,height);
    glClearColor(0.2f,0.5f,0.7f,1.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

/* listeners ------------------------------------------------------------- */
static void ping_cb(void*d, struct xdg_wm_base*w,uint32_t s){ xdg_wm_base_pong(w,s); }
static const struct xdg_wm_base_listener wm_lis={ .ping = ping_cb };

static void reg_cb(void*d,struct wl_registry*r,uint32_t id,const char*ifc,uint32_t ver){
    if(!strcmp(ifc,"wl_compositor"))
        comp = wl_registry_bind(r,id,&wl_compositor_interface,4);
    else if(!strcmp(ifc,"xdg_wm_base")){
        wm = wl_registry_bind(r,id,&xdg_wm_base_interface,1);
        xdg_wm_base_add_listener(wm,&wm_lis,NULL);
    }
}
static const struct wl_registry_listener reg_lis={ .global = reg_cb };

static void surf_cfg(void*d,struct xdg_surface*s,uint32_t serial){
    xdg_surface_ack_configure(s,serial);
    configured = 1;
}
static const struct xdg_surface_listener surf_lis={ .configure = surf_cfg };

static void top_cfg(void*d,struct xdg_toplevel*t,int32_t w,int32_t h,struct wl_array*st){
    if(w>0) width=w;
    if(h>0) height=h;
}
static const struct xdg_toplevel_listener top_lis={ .configure = top_cfg };

/* main ------------------------------------------------------------------ */
int main(void)
{
    /* Wayland connect + early EGL init (surfaceless) -------------------- */
    dpy = wl_display_connect(NULL);                 CHECK(dpy,"wl_display_connect");
    EGLDisplay edpy = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR,
                                            (void*)dpy, NULL);
    EGLCHK(edpy != EGL_NO_DISPLAY);
    EGLCHK(eglInitialize(edpy,NULL,NULL));          /* driver loads now    */

    /* discover globals while driver warms ------------------------------ */
    struct wl_registry *reg = wl_display_get_registry(dpy);
    wl_registry_add_listener(reg,&reg_lis,NULL);
    wl_display_roundtrip(dpy);                      CHECK(comp&&wm,"globals");

    /* create surfaceless context (warms shaders) ----------------------- */
    const EGLint ctx_attr[]={ EGL_CONTEXT_CLIENT_VERSION,2, EGL_NONE };
    EGLContext ctx = eglCreateContext(edpy,NULL,EGL_NO_CONTEXT,ctx_attr);
    EGLCHK(ctx!=EGL_NO_CONTEXT);

    /* Wayland surface objects ------------------------------------------ */
    struct wl_surface *wsurf = wl_compositor_create_surface(comp);
    struct xdg_surface *xsurf = xdg_wm_base_get_xdg_surface(wm,wsurf);
    xdg_surface_add_listener(xsurf,&surf_lis,NULL);
    struct xdg_toplevel *top = xdg_surface_get_toplevel(xsurf);
    xdg_toplevel_add_listener(top,&top_lis,NULL);
    xdg_toplevel_set_title(top,"instant");

    wl_surface_commit(wsurf); wl_display_flush(dpy);

    while(!configured) wl_display_dispatch(dpy);

    /* EGL window + first frame ----------------------------------------- */
    EGLConfig cfg; EGLint n;
    const EGLint cfg_attr[]={ EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
                              EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
                              EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,
                              EGL_NONE };
    EGLCHK(eglChooseConfig(edpy,cfg_attr,&cfg,1,&n));
    struct wl_egl_window *wegl = wl_egl_window_create(wsurf,width,height);
    CHECK(wegl,"wl_egl_window_create");
    EGLSurface esurf=eglCreateWindowSurface(edpy,cfg,(void*)wegl,NULL);
    EGLCHK(eglMakeCurrent(edpy,esurf,esurf,ctx));

    paint(); eglSwapBuffers(edpy,esurf); wl_display_flush(dpy);   /* pixel ≤12 ms */

    /* 5-second demo loop ----------------------------------------------- */
    for(int i=0;i<300;i++){ paint(); eglSwapBuffers(edpy,esurf); naptick(); }

    return 0;
}

