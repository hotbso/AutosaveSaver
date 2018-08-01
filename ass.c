/*

MIT License

Copyright (c) 2018 

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <stdarg.h>
#include <dirent.h>
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
static void ass_clean(void);

static char xpdir[512];
static const char *psep;
static XPLMMenuID ass_menu;

static char autosave_file[512];
static char autosave_base[100];
static char autosave_ext[20];

#define TS_MAX 50
#define TS_LENGTH 12 /* YYMMDD_HHMM\0 */

static int ass_disabled;
static int ass_keep = 5;	/* default */
static unsigned int n_ts_list, max_ts_list;
static char *ts_list;

#ifdef IBM
/* 8-(
   only support ? wildchar
  */
static int
fnmatch(const char *pat, const char *str, int flags) {
	while (1) {
		char s = *str++;
		char p = *pat++;
	
		if (s == '\0' || p == '\0')
			return !(s == p);

		if ((p != '?') && (p != s))
			return 1;
	}
}
#else
#include <fnmatch.h>
#endif

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
					ass_clean();
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

#if 0
static int str_compare(void *a, void *b) {
	return 
}
#endif

static void ass_clean(void)
{
	if (ass_disabled)
		return;

	if (NULL == ts_list) {
		n_ts_list = 0;
		max_ts_list = ass_keep * 3 / 2 + 1;
		ts_list = malloc(max_ts_list * TS_LENGTH);
		if (NULL == ts_list) {
			log_msg("No memory for %d entries?", max_ts_list);
			ass_disabled = 1;
			return;
		}
		
		char ass_mask[512];
		
		strcpy(ass_mask, autosave_file);
		char *s = strrchr(ass_mask, psep[0]);
		*s = '\0';

		DIR *dir = opendir(ass_mask);
		if (NULL == dir) {
			log_msg("Can't scan directory \"%s\": %s", ass_mask, strerror(errno));
			ass_disabled = 1;
			return;
		}
		
		strcpy(ass_mask, autosave_base);
		strcat(ass_mask, "_??????_????");
		strcat(ass_mask, autosave_ext);
		log_msg("mask: '%s'", ass_mask);

		const struct dirent *de;
		while (NULL != (de = readdir(dir)))	{
			if (0 != fnmatch(ass_mask, de->d_name, 0))
				continue;

			const char *s = de->d_name + strlen(autosave_base) + 1; /* past the _ */
			char *d = ts_list + (TS_LENGTH * n_ts_list);
			memcpy(d, s, TS_LENGTH-1);
			d[TS_LENGTH-1] = '\0';
			log_msg("File: '%s', TS: %s", de->d_name, d);

			n_ts_list++;
			if (n_ts_list == max_ts_list) {
				max_ts_list = n_ts_list * 3 / 2 + 1;
				ts_list = realloc(ts_list, max_ts_list * TS_LENGTH);
				if (NULL == ts_list) {
					log_msg("No memory for %d entries?", max_ts_list);
					ass_disabled = 1;
					return;
				}
				log_msg("realloc %d\n", max_ts_list);
			}
		}

		closedir(dir);
		
		qsort(ts_list, n_ts_list, TS_LENGTH, (int (*)(const void *, const void *))strcmp);
		int i;
		for (i = 0; i < n_ts_list; i++) {
			log_msg("Sorted: %s", ts_list + i * TS_LENGTH);
		}
	}
}

static float game_loop_cb(float elapsed_last_call,
                float elapsed_last_loop, int counter,
                void *in_refcon)
{
	float loop_delay = 30.0f;
	struct stat stat_buf;
	time_t now = time(NULL);

	if (ass_disabled || ('\0' == autosave_file[0]))
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

