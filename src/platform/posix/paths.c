/* Arcan-fe (OS/device platform), scriptable front-end engine
 *
 * Arcan-fe is the legal property of its developers, please refer
 * to the platform/LICENSE file distributed with this source distribution
 * for licensing terms.
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <libgen.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <ctype.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <arcan_math.h>
#include <arcan_general.h>

bool arcan_isdir(const char* fn)
{
	struct stat buf;
	bool rv = false;

	if (fn == NULL)
			return false;

	if (stat(fn, &buf) == 0)
		rv = S_ISDIR(buf.st_mode);

	return rv;
}

bool arcan_isfile(const char* fn)
{
	struct stat buf;
	bool rv = false;

	if (fn == NULL)
		return false;

	if (stat(fn, &buf) == 0)
		rv = S_ISREG(buf.st_mode) || S_ISFIFO(buf.st_mode);

	return rv;
}

static char* envvs[] = {
	"ARCAN_APPLPATH",
	"ARCAN_RESOURCEPATH",
	"ARCAN_APPLTEMPPATH",
	"ARCAN_STATEPATH", /* will be ignored */
	"ARCAN_APPLBASEPATH",
	"ARCAN_APPLSTOREPATH",
	"ARCAN_STATEBASEPATH",
	"ARCAN_FONTPATH",
	"ARCAN_BINPATH",
	"ARCAN_LIBPATH",
	"ARCAN_LOGPATH"
};

static char* alloc_cat(char* a, char* b)
{
	size_t a_sz = strlen(a);
	size_t b_sz = strlen(b);
	char* newstr = malloc(a_sz + b_sz + 1);
	newstr[a_sz + b_sz] = '\0';
	memcpy(newstr, a, a_sz);
	memcpy(newstr + a_sz, b, b_sz);
	return newstr;
}

static char* rep_str(char* instr)
{
	char* beg = strchr(instr, '[');
	if (!beg)
		return instr;

	char* end = strchr(beg+1, ']');

	for (size_t i = 0; i < sizeof(envvs)/sizeof(envvs[0]); i++){
		if (end)
			*end = '\0';

		if (strcmp(envvs[i], beg+1) != 0)
			continue;

		char* exp = arcan_expand_resource("", 1 << i);

		if (!exp)
			goto fail;

		if (!end){
			*beg = '\0';
			char* newstr = alloc_cat(instr, exp);
			free(instr);
			return newstr;
		}
		else{
			*beg = '\0';
			*end = '\0';
			char* newstr = alloc_cat(instr, exp);
			free(instr);
			char* resstr = alloc_cat(newstr, end+1);
			free(newstr);
			return rep_str(resstr);
		}
	}

fail:
	arcan_warning("expand failed, no match for supplied string (%s)\n", beg+1);
	return instr;
}

char** arcan_expand_namespaces(char** inargs)
{
	char** work = inargs;

	while(*work){
		*work = rep_str(*work);
		work++;
	}

	return inargs;
}

static char* binpath_unix()
{
	char* binpath = NULL;

	if (arcan_isfile( "./arcan_frameserver") )
		binpath = strdup("./arcan_frameserver" );
	else if (arcan_isfile( "/usr/local/bin/arcan_frameserver"))
		binpath = strdup("/usr/local/bin/arcan_frameserver");
	else if (arcan_isfile( "/usr/bin/arcan_frameserver" ))
		binpath = strdup("/usr/bin/arcan_frameserver");
	else ;

	return binpath;
}

static char* libpath_unix()
{
	char* libpath = NULL;

	if (arcan_isfile( getenv("ARCAN_HIJACK") ) )
		libpath = strdup( getenv("ARCAN_HIJACK") );
	else if (arcan_isfile( "./libarcan_hijack.so"))
		libpath = realpath( "./", NULL );
	else if (arcan_isfile( "/usr/local/lib/libarcan_hijack.so") )
		libpath = strdup( "/usr/local/lib/");
	else if (arcan_isfile( "/usr/lib/libarcan_hijack.so") )
		libpath = strdup( "/usr/lib/");

	return libpath;
}

static char* unix_find(const char* fname)
{
	char* res = NULL;
	char* pathtbl[] = {
		".",
		NULL, /* fill in with HOME */
		"/usr/local/share/arcan",
		"/usr/share/arcan",
		NULL /* NULL terminates */
	};

	if (getenv("HOME")){
		size_t len = strlen( getenv("HOME") ) + 9;
		pathtbl[1] = malloc(len);
		snprintf(pathtbl[1], len, "%s/.arcan", getenv("HOME") );
	}
	else
		pathtbl[1] = strdup("");

	size_t fn_l = strlen(fname);
	for (char** base = pathtbl; *base != NULL; base++){
		char buf[ fn_l + strlen(*base) + 2 ];
		snprintf(buf, sizeof(buf), "%s/%s", *base, fname);

		if (arcan_isdir(buf)){
			res = strdup(buf);
			break;
		}
	}

	free(pathtbl[1]);
	return res;
}

/*
 * This is set-up to mimic the behavior of previous arcan
 * version as much as possible. For other, more controlled settings,
 * this is a good function to replace.
 */
void arcan_set_namespace_defaults()
{
/*
 * use environment variables as hard overrides
 */
	for (int i = 0; i < sizeof( envvs ) / sizeof( envvs[0] ); i++){
		const char* tmp = getenv(envvs[i]);
		arcan_override_namespace(tmp, 1 << i);
	}

/*
 * legacy mapping from the < 0.5 days
 */
	arcan_softoverride_namespace(binpath_unix(), RESOURCE_SYS_BINS);
	arcan_softoverride_namespace(libpath_unix(), RESOURCE_SYS_LIBS);

	char* respath = unix_find("resources");
	char* tmp = NULL;

	if (!respath)
		respath = arcan_expand_resource("", RESOURCE_APPL_SHARED);

	if (respath){
		size_t len = strlen(respath);
		char debug_dir[ len + sizeof("/logs") ];
		char font_dir[ len + sizeof("/fonts") ];

		snprintf(debug_dir, sizeof(debug_dir), "%s/logs", respath);
		snprintf(font_dir, sizeof(font_dir), "%s/fonts", respath);

		arcan_softoverride_namespace(respath, RESOURCE_APPL_SHARED);
		arcan_softoverride_namespace(debug_dir, RESOURCE_SYS_DEBUG);
		arcan_softoverride_namespace(respath, RESOURCE_APPL_STATE);
		arcan_softoverride_namespace(font_dir, RESOURCE_SYS_FONT);
	}

	if (tmp)
		arcan_mem_free(tmp);

	char* scrpath = unix_find("appl");
	if (!scrpath)
		scrpath = unix_find("themes");

	if (scrpath){
		arcan_softoverride_namespace(scrpath, RESOURCE_SYS_APPLBASE);
		arcan_softoverride_namespace(scrpath, RESOURCE_SYS_APPLSTORE);
	}

	tmp = arcan_expand_resource("", RESOURCE_SYS_APPLSTATE);
	if (!tmp){
		arcan_mem_free(tmp);
		tmp = arcan_expand_resource("savestates", RESOURCE_APPL_SHARED);
		if (tmp)
			arcan_override_namespace(tmp, RESOURCE_SYS_APPLSTATE);
		arcan_mem_free(tmp);
	}
}