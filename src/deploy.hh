#ifndef __DEPLOY_HH
#define __DEPLOY_HH

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "sendao.h"

typedef struct _scriptfile scriptfile;
typedef struct _scriptsegment scriptsegment;
typedef struct _scriptline scriptline;
typedef struct _scriptscan scriptscan;
typedef struct _scriptresponse scriptresponse;

#include "pipe.hh"
#include "rce.hh"

struct _scriptscan {
	char *source;
	int offset;
	char *saveas;
	char *endstr;
};

struct _scriptresponse {
	char *source;
	char *response;
};

struct _scriptfile {
	const char *source;
	tlist *segments;
	tnode *current;
	int runstate;
	int segment_repeats;
	int line_repeats;
	bool chained;
};

struct _scriptsegment {
	char *comment;
	tlist *lines;
	tnode *current;
	bool repeatable; // lines in segment will each repeat
	bool repeat_segment; // segment will repeat if any step fails
	bool chained; // segment will stop if any step fails
	int segment_repeats;
	int line_repeats;
};

struct _scriptline {
	char *cmd;
	char *result;
	tlist *responses;
	tlist *scans;
	int runstate; // 0=ready, 1=run, 2=done, 4=errors
	int line_repeats;
};

scriptsegment *new_segment(scriptfile *sf);
scriptline *new_scriptline(scriptfile *sf, scriptsegment *seg, const char *txt);
scriptfile *loadScriptFile(const char *fn);
scriptfile *loadScript(const char *buf);


scriptline * next_scriptline(scriptfile *sf );
scriptline *current_scriptline( scriptfile *sf );
scriptsegment * current_scriptsegment(scriptfile *sf);

extern const char *scriptext;

enum {
	RUNSTATE_NONE,
	RUNSTATE_RUNNING,
	RUNSTATE_ERRORED,
	RUNSTATE_COMPLETE,
	RUNSTATE_TESTING,
	RUNSTATE_RESULT
};

#endif
