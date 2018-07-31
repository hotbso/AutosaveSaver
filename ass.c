#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <sys/stat.h>

#include "XPLMPlugin.h"
#include "XPLMPlanes.h"
#include "XPLMUtilities.h"
#include "XPLMProcessing.h"
#include "XPLMMenus.h"
#include "XPWidgets.h"
#include "XPStandardWidgets.h"

#if 0
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMDataAccess.h"
#include "XPLMNavigation.h"
#endif

#define UNUSED(x) (void)(x)

#define VERSION "0.1a"

static float game_loop_cb(float elapsed_last_call,
                float elapsed_last_loop, int counter,
                void *in_refcon);

static char xpdir[512];
static const char *psep;

static char autosave_file[512];
static char autosave_base[100];
static char autosave_ext[20];


static XPLMMenuID ass_menu;

static void
log_msg(const char *fmt, ...)
{
	char line[1024];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line) - 3, fmt, ap);
	strcat(line, "\n");
	XPLMDebugString("autosave_save: ");
	XPLMDebugString(line);
	va_end(ap);
}

static void menu_cb(void *menuRef, void *param)
{
}

PLUGIN_API int XPluginStart(char *out_name, char *out_sig, char *out_desc)
{
    XPLMMenuID menu;
    int sub_menu;

 	/* Always use Unix-native paths on the Mac! */
	XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    psep = XPLMGetDirectorySeparator();
	XPLMGetSystemPath(xpdir);

    strcpy(out_name, "autosave_save" VERSION);
    strcpy(out_sig, "hotbso");
    strcpy(out_desc, "A plugin that save autosave state/situation files.");


    XPLMRegisterFlightLoopCallback(game_loop_cb, 30.0f, NULL);

    menu = XPLMFindPluginsMenu();
    sub_menu = XPLMAppendMenuItem(menu, "Autosave Save", NULL, 1);
    ass_menu = XPLMCreateMenu("Autosave Save", menu, sub_menu, menu_cb, NULL);
    return 1;
}



PLUGIN_API void	XPluginStop(void)
{
}


PLUGIN_API void XPluginDisable(void)
{
}


PLUGIN_API int XPluginEnable(void)
{
	return 1;
}


PLUGIN_API void XPluginReceiveMessage(XPLMPluginID in_from,
                long in_msg, void *in_param)
{
	UNUSED(in_from);

	switch (in_msg) {
		case XPLM_MSG_PLANE_LOADED:
			if (in_param == 0) {
				char acf_path[512];
				char acf_file[256];

				XPLMGetNthAircraftModel(XPLM_USER_AIRCRAFT, acf_file, acf_path);
				log_msg(acf_file);
				log_msg(acf_path);

				if (0 == strcmp(acf_file, "A320.acf")) {
					strcpy(autosave_file, acf_path);
					char *s = strrchr(autosave_file, psep[0]);
					sprintf(s+1, "data%sstate%sautosave.asb", psep, psep);
					strcpy(autosave_base, "autosave");
					strcpy(autosave_ext, ".asb");
					log_msg(autosave_file);
				} else {
					autosave_file[0] = '\0';
				}
			}
		break;
	}
}



static int widget_cb(XPWidgetMessage msg, XPWidgetID widget_id, intptr_t param1, intptr_t param2)
{
	return 0;
}

static float game_loop_cb(float elapsed_last_call,
                float elapsed_last_loop, int counter,
                void *in_refcon)
{
	float loop_delay = 30.0f;
	struct stat stat_buf;
	time_t now = time(NULL);

	if ('\0' == autosave_file[0])
		goto done;

	if (0 != stat(autosave_file, &stat_buf))
		goto done;

	/* no idea whether we run in the sam thread as the writer, so
	   hopefully it is finished after 30 s */

	if (now - stat_buf.st_mtime > 30) {
		char new_name[1024];
		struct tm *tm = localtime(&now);

		strcpy(new_name, autosave_file);
		char *s = strrchr(new_name, psep[0]);
		sprintf(s+1, "%s_%02d%02d%02d_%02d%02d%s", autosave_base,
				tm->tm_year-100, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min,
				autosave_ext);
		log_msg(autosave_file);
		log_msg(new_name);
		rename(autosave_file, new_name);
	}

   done:
    return loop_delay;
}

