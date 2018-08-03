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

#define VERSION "0.7a"

static float game_loop_cb(float elapsed_last_call,
                float elapsed_last_loop, int counter,
                void *in_refcon);

static void delete_tail(void);
static void init_ts_list(void);

static char xpdir[512];
static const char *psep;
static XPLMMenuID ass_menu;
static int ass_enable_item;
static XPWidgetID conf_widget;

static char autosave_file[512];
static char autosave_base[20];
static char autosave_ext[10];
static char cfg_path[512];


#define TS_MAX 50
#define TS_LENGTH 12 /* YYMMDD_HHMM\0 */

/* this is a nonessential plugin so if anything goes wrong
   we just disable and protect the sim */
static int ass_error_disabled;
static int ass_enabled = 1;
static int ass_keep = 5;	/* default */
static int n_ts_list, max_ts_list;
static char *ts_list;
static int ts_head;	/* newest TS */
static int ts_tail;	/* oldest TS */

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
	XPLMDebugString("ass: ");
	XPLMDebugString(line);
	va_end(ap);
}

static void
save_pref()
{
    FILE *f = fopen(cfg_path, "w");
    if (NULL == f)
        return;

    fprintf(f, "%d %d", ass_enabled, ass_keep);
    fclose(f);
}


static void
load_pref()
{
    FILE *f  = fopen(cfg_path, "r");
    if (NULL == f)
        return;

    fscanf(f, "%i %i", &ass_enabled, &ass_keep);
    fclose(f);
	log_msg("From pref: ass_enabled: %d, ass_keep: %d", ass_enabled, ass_keep);
}


static int
widget_cb(XPWidgetMessage msg, XPWidgetID widget_id, intptr_t param1, intptr_t param2)
{
	return 0;
}


static void
menu_cb(void *menu_ref, void *item_ref)
{
	if ((int *)item_ref == &ass_enabled) {
		ass_enabled = !ass_enabled;
		XPLMCheckMenuItem(ass_menu, ass_enable_item,
						  ass_enabled ? xplm_Menu_Checked : xplm_Menu_Unchecked);
	} else if ((int *)item_ref == &ass_keep) {
		if (NULL == conf_widget) {
			conf_widget = XPCreateWidget(200, 400, 400, 200,
				0, "Autosave Saver", 1, NULL, xpWidgetClass_MainWindow);
			/* XPSetWidgetProperty(conf_widget, xpProperty_MainWindowType, xpMainWindowStyle_Translucent); */
			XPSetWidgetProperty(conf_widget, xpProperty_MainWindowHasCloseBoxes, 1);
			XPAddWidgetCallback(conf_widget, widget_cb);	
		}
		
		XPShowWidget(conf_widget);
	}
}

PLUGIN_API int
XPluginStart(char *out_name, char *out_sig, char *out_desc)
{
    XPLMMenuID menu;
    int sub_menu;

 	/* Always use Unix-native paths on the Mac! */
	XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    psep = XPLMGetDirectorySeparator();
	XPLMGetSystemPath(xpdir);

    strcpy(out_name, "autosave saver (ass) " VERSION);
    strcpy(out_sig, "hotbso");
    strcpy(out_desc, "A plugin that save autosave state/situation files with a timestamp.");

    XPLMGetPrefsPath(cfg_path);
    XPLMExtractFileAndPath(cfg_path);
    strcat(cfg_path, psep);
    strcat(cfg_path, "ass.prf");
	load_pref();

    XPLMRegisterFlightLoopCallback(game_loop_cb, 30.0f, NULL);

    menu = XPLMFindPluginsMenu();
    sub_menu = XPLMAppendMenuItem(menu, "Autosave Save", NULL, 1);
    ass_menu = XPLMCreateMenu("Autosave Save", menu, sub_menu, menu_cb, NULL);
	ass_enable_item = XPLMAppendMenuItem(ass_menu, "Enabled", &ass_enabled, 0);
	XPLMCheckMenuItem(ass_menu, ass_enable_item,
					  ass_enabled ? xplm_Menu_Checked : xplm_Menu_Unchecked);
	XPLMAppendMenuItem(ass_menu, "Configure", &ass_keep, 0);
    return 1;
}



PLUGIN_API void
XPluginStop(void)
{
	save_pref();
}


PLUGIN_API void
XPluginDisable(void)
{
}


PLUGIN_API int
XPluginEnable(void)
{
	return 1;
}


PLUGIN_API void
XPluginReceiveMessage(XPLMPluginID in_from, long in_msg, void *in_param)
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
					init_ts_list();
				} else {
					autosave_file[0] = '\0';
				}
			}
		break;
	}
}


static void
delete_tail(void) {
	char fname[512];
	strcpy(fname, autosave_file);
	char *s = strrchr(fname, psep[0]);

	while (n_ts_list > ass_keep) {
		sprintf(s+1, "%s_%s%s", autosave_base, ts_list + ts_tail * TS_LENGTH, autosave_ext);
		if (unlink(fname) < 0) {
			log_msg("Can't unlink '%s': %s", fname, strerror(errno));
			ass_error_disabled = 1;
			return;
		}

		log_msg("Deleted: '%s'", fname);
		n_ts_list--;
		ts_tail = (ts_tail + 1) % max_ts_list;
	}
}

static int
alloc_ts_list(void) {
	if (n_ts_list < max_ts_list - 1)
		return 1;

	max_ts_list = (ass_keep > max_ts_list ? ass_keep : max_ts_list) + 15;
	ts_list = realloc(ts_list, max_ts_list * TS_LENGTH);

	if (NULL == ts_list) {
		log_msg("No memory for %d entries?", max_ts_list);
		ass_error_disabled = 1;
		return 0;
	}

	log_msg("realloc to %d", max_ts_list);
	return 1;
}

static void
init_ts_list(void)
{
	if (ass_error_disabled)
		return;

	n_ts_list = 0;
	if (! alloc_ts_list())
		return;

	char ass_mask[512];

	strcpy(ass_mask, autosave_file);
	char *s = strrchr(ass_mask, psep[0]);
	*s = '\0';

	DIR *dir = opendir(ass_mask);
	if (NULL == dir) {
		log_msg("Can't scan directory \"%s\": %s", ass_mask, strerror(errno));
		ass_error_disabled = 1;
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

		if (!alloc_ts_list())
			return;

		const char *s = de->d_name + strlen(autosave_base) + 1; /* past the _ */
		char *d = ts_list + (TS_LENGTH * n_ts_list);
		memcpy(d, s, TS_LENGTH-1);
		d[TS_LENGTH-1] = '\0';
		log_msg("File: '%s', TS: %s", de->d_name, d);
		n_ts_list++;
	}

	closedir(dir);

	qsort(ts_list, n_ts_list, TS_LENGTH, (int (*)(const void *, const void *))strcmp);

	ts_head = n_ts_list - 1;
	ts_tail = 0;

	delete_tail();
}

static float
game_loop_cb(float elapsed_last_call,
             float elapsed_last_loop, int counter, void *in_refcon)
{
	float loop_delay = 30.0f;
	struct stat stat_buf;
	time_t now = time(NULL);

	if (ass_error_disabled || ('\0' == autosave_file[0]))
		goto done;

	if (0 != stat(autosave_file, &stat_buf))
		goto done;

	/* no idea whether we run in the sam thread as the writer, so
	   hopefully it is finished after 30 s */

	if (now - stat_buf.st_mtime > 30) {
		char new_name[1024];
		char ts[TS_LENGTH];

		struct tm *tm = localtime(&now);

		strcpy(new_name, autosave_file);
		char *s = strrchr(new_name, psep[0]);
		snprintf(ts, TS_LENGTH, "%02d%02d%02d_%02d%02d", tm->tm_year-100, tm->tm_mon+1,
				 tm->tm_mday, tm->tm_hour, tm->tm_min);
		sprintf(s+1, "%s_%s%s", autosave_base, ts, autosave_ext);
		if (rename(autosave_file, new_name) < 0) {
			log_msg("Cannot rename autosave file to '%s': %s", new_name, strerror(errno));
			ass_error_disabled = 1;
			goto done;
		}

		log_msg("Renamed autosave file to '%s'", new_name);

		if (!alloc_ts_list())
			goto done;

		char *d = ts_list + (TS_LENGTH * ts_head);
		memcpy(d, ts, TS_LENGTH);
		n_ts_list++;
		ts_head = (ts_head + 1) % max_ts_list;

		delete_tail();
	}

   done:
    return loop_delay;
}
