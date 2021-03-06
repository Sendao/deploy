#include "deploy.hh"


scriptsegment *new_segment(scriptfile *sf)
{
	scriptsegment *seg = (scriptsegment*)malloc(sizeof(scriptsegment));
	seg->lines = new tlist;
	seg->comment = NULL;
	seg->current = NULL;
	seg->chained = sf->chained;
	seg->repeatable = seg->repeat_segment = false;
	seg->repeat_segment = sf->segment_repeats > 0;
	seg->repeatable = sf->line_repeats > 0;
	seg->segment_repeats = sf->segment_repeats;
	seg->line_repeats = sf->line_repeats;
	return seg;
}
scriptline *new_scriptline(scriptfile *sf, scriptsegment *seg, const char *txt)
{
	scriptline *sl = (scriptline*)malloc(sizeof(scriptline));
	sl->cmd = str_dup(txt);
	sl->runstate = RUNSTATE_NONE;
	sl->line_repeats = seg->line_repeats;
	sl->result = NULL;
	sl->scans = new tlist;
	sl->responses = new tlist;
	return sl;
}

scriptsegment *current_scriptsegment( scriptfile *sf )
{
	if( !sf->current ) return NULL;

	scriptsegment *seg = (scriptsegment*)sf->current->data;
	while( !seg->current || !seg->lines->nodes ) {
		seg->current = seg->lines->nodes;
		if( !seg->current ) {
			// next_segment for empty segments!!! <<-->>
	//		if( seg->comment )
//				printf("\n##%s\n", seg->comment);

			sf->current = sf->current->next;
			seg = (scriptsegment*)sf->current->data;
		}
	}
	return seg;
}

scriptline *current_scriptline( scriptfile *sf )
{
	scriptsegment *seg = (scriptsegment*)sf->current->data;
	return (scriptline*)seg->current->data;
}

scriptline *next_scriptline( scriptfile *sf )
{
	scriptsegment *seg = current_scriptsegment(sf);
	scriptline *line;
	tnode *n;
	bool next_segment=true, found_error;

	if( !seg ) {
		sf->runstate = RUNSTATE_ERRORED;
		return NULL;
	}

	line = (scriptline*)seg->current->data;

	if( line->runstate != RUNSTATE_COMPLETE ) {

		if( line->line_repeats > 1 ) {
			line->line_repeats--;
			line->runstate = RUNSTATE_NONE;
			printf("Repeat line\n");
			sf->runstate = RUNSTATE_NONE;
			return line;
		} else if( seg->chained ) {
			printf("Chained stop: segment failed\n");

			// next segment or try this one again
			if( seg->segment_repeats > 1 ) {
				seg->segment_repeats--;
				printf("Repeat segment\n");
				forTLIST( line, n, seg->lines, scriptline* ) {
					line->runstate = RUNSTATE_NONE;
					if( line->result ) strexpand(&line->result, "\n");
					line->line_repeats = seg->line_repeats;
				}
				next_segment = false;
			}
			if( next_segment ) {
				printf("Next segment\n");
				sf->current = sf->current->next;
			}
			if( sf->current && sf->current->data ) {
				seg = (scriptsegment*)sf->current->data;
				seg->current = seg->lines->nodes;
				sf->runstate = RUNSTATE_NONE;
				return (scriptline*)seg->current->data;
			} else {
				// end of script
				return NULL;
			}
		} else {
			printf("Failed line, moving on\n");
			sf->runstate = RUNSTATE_NONE;
		}

	}

	if( seg->current->next ) { // next line
		seg->current = seg->current->next;
		sf->runstate = RUNSTATE_NONE;
		return (scriptline*)seg->current->data;
	}

	next_segment = true;

	if( seg->repeat_segment && seg->segment_repeats > 0 ) {
		tnode *n;
		found_error=false;

		// any errors in current segment?
		forTLIST( line, n, seg->lines, scriptline* ) {
			if( line->runstate == RUNSTATE_ERRORED ) {
				found_error=true;
				break;
			}
		}
		if( found_error ) {
			printf("Segment did not complete successfully.\n");

			// ok, we repeat the segment
			seg->segment_repeats--;

			if( seg->segment_repeats >= 0 ) {
				printf("Repeating segment.\n");

				seg->current = seg->lines->nodes;

				forTLIST( line, n, seg->lines, scriptline* ) {
					line->runstate = RUNSTATE_NONE;
					if( line->result ) strexpand(&line->result, "\n");
					line->line_repeats = seg->line_repeats;
				}

				sf->runstate = RUNSTATE_NONE;
				next_segment = false;
			}
		}
	}

	if( next_segment ) {
		sf->current = sf->current->next;
		if( !sf->current )
			return NULL;
		seg = (scriptsegment*)sf->current->data;
		if( !seg->current )
			seg->current = seg->lines->nodes;
	}

	line = (scriptline*)seg->current->data;
	return line;
}

scriptfile *loadScriptFile(const char *fn)
{
	return loadScript( readwholefile(fn) );
}
scriptfile *loadScript(const char *source)
{
	scriptfile *script = (scriptfile*)malloc(sizeof(scriptfile));
	scriptsegment *seg = NULL;
	scriptline *line = NULL;
	tlist *lines;
	tlist *config;
	tnode *n;
	scriptscan *scrscan;
	scriptresponse *scrres;
	int line_no=0;
	bool notComment=false;
	char *strline;

	script->source = source;
	if( !script->source ) {
		fprintf(stderr, "Problem reading script.");
		return NULL;
	}
	script->line_repeats = 3;
	script->segment_repeats = 3;
	script->chained = false;

	lines = split(script->source, "\n");

	script->segments = new tlist();

	seg = new_segment(script);

	forTLIST( strline, n, lines, char* ) {
		strip_newline(strline);
		line_no++;
		if( !*strline ) {
			if( script->segments->count == 0 ) {
				script->segments->PushBack(seg);
				//printf("Script '%s': [%d][%d]\n", seg->comment, script->segment_repeats, script->line_repeats);
				seg = new_segment(script);
			}
			continue;
		}
		if( *strline == '#' ) {
			if( *(strline+1) == '#' || *(strline+1) == '!' ) {
				// just comment lines
				continue;
			}
			strip_newline(strline+1);
			if( seg->lines->count > 0 ) {
				// next segment
				seg->current = seg->lines->nodes;
				script->segments->PushBack(seg);
				//printf("Segment '%s': [%d][%d]\n", seg->comment, seg->segment_repeats, seg->line_repeats);
				seg = new_segment(script);
			}

			if( line && str_cn_cmp(strline, "#response") == 0 ) {
				config = split(strline+10, "=");
				scrres = (scriptresponse*)malloc(sizeof(scriptresponse));
				scrres->source = strdup( (char*)config->FindData(0) );
				scrres->response = strdup( (char*)config->FindData(1) );
				line->responses->PushBack(scrres);
				//printf("Scanned #response linecode '%s': %s.\n", scrres->source, scrres->response);
			} else if( line && str_cn_cmp(strline, "#scan") == 0 ) {
				config = split(strline+5, "=");
				scrscan = (scriptscan*)malloc(sizeof(scriptscan));
				scrscan->source = strdup( (char*)config->FindData(0) );
				scrscan->saveas = strdup( (char*)config->FindData(1) );
				scrscan->endstr = NULL;
				scrscan->offset = 0;
				line->scans->PushBack(scrscan);
				//printf("Scanned #scan linecode '%s': %s.\n", scrscan->source, scrscan->saveas);
			} else if( line && str_cn_cmp(strline, "#bscan") == 0 ) {
				config = split(strline+6, "=");
				scrscan = (scriptscan*)malloc(sizeof(scriptscan));
				scrscan->source = strdup( (char*)config->FindData(0) );
				scrscan->saveas = strdup( (char*)config->FindData(1) );
				scrscan->endstr = strdup("eof");
				scrscan->offset = 0;
				line->scans->PushBack(scrscan);
				//printf("Scanned #bscan linecode '%s': %s.\n", scrscan->source, scrscan->saveas);
			} else if( str_cn_cmp(strline, "#segment-repeat") == 0 ) {
				strip_spaces(strline);
				config = split(strline, "=");
				if( config->count > 0 ) {
					if( script->segments->count > 0 ) { // per-segment
						seg->segment_repeats = atoi( (char*)config->FindData(1) );
					} else { // script default
						script->segment_repeats = atoi( (char*)config->FindData(1) );
					}
				} else {
					script->segment_repeats = 3;
				}
				config->Clear(free);
				delete config;
			} else if( str_cn_cmp(strline, "#line-repeat") == 0 ) {
				strip_spaces(strline);
				config = split(strline, "=");
				if( config->count > 0 ) {
					if( script->segments->count > 0 ) { // per-segment
						seg->line_repeats = atoi( (char*)config->FindData(1) );
					} else { // script default
						script->line_repeats = atoi( (char*)config->FindData(1) );
					}
				} else {
					seg->repeatable = true;
				}
				config->Clear(free);
				delete config;
			} else if( str_cn_cmp(strline, "#chain") == 0 ) {
				if( script->segments->count > 0 ) { // per-segment
					seg->chained = true;
				} else {
					script->chained = true;
				}
			} else {
				if( seg->comment )
					strexpand(&seg->comment, "\n"); //uu
				strexpand(&seg->comment, strline+1);
			}
//			printf("script seg: %s\n", strline);
		} else {
//			printf("script line: %s\n", strline);
			seg->lines->PushBack( line=new_scriptline(script, seg, strline) );
		}
	}
	lines->Clear(free);
	delete lines;

	if( seg ) {
		seg->current = seg->lines->nodes;
		//printf("Segment '%s': [%d][%d]\n", seg->comment, seg->segment_repeats, seg->line_repeats);
		script->segments->PushBack(seg);
	}

	script->current = script->segments->nodes;
	script->runstate = RUNSTATE_NONE;

	return script;
}
