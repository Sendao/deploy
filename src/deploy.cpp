#include "deploy.hh"

/*
 * Implement SensitiveBash(cmd) and make sure to grab the return value (probably echo $?)
 * Read bash script & run through sbash
 * Collect and compile errors
 * Continue if errors fixed
 *
 */


// Warning: this code may contain anti-patterns
// consume with salt

void pipeReadHandler( Pipe *p, char *buf );
void pipeIdleHandler( Pipe *p );
void pipeCmdHandler( Pipe *p );

scriptfile *running_script=NULL;
SMap *scanned_vars=NULL;

bool pipeTest( Pipe *p )
{
	if( running_script && !running_script->current ) {
		return true;//anyway:)return false;
	}
	return false;
}

Pipe *pipeConnection( void )
{
	Pipe *p = new Pipe;

	p->log("deploy_latest.log");
	p->readCB = pipeReadHandler;
	p->idleCB = pipeIdleHandler;
	p->cmdlineCB = pipeCmdHandler;

	p->breakTest = pipeTest;

	p->write_to_stdout = false;
	p->read_from_stdin = false;

	return p;
}

char *strPrint( const char *pattern )
{
	stringbuf *sp = new stringbuf;
	const char *p, *stringvar;
	char *strname, *tmpbuf;

	for( p = pattern; *p; p++ ) {
		if( *p == '\\' ) {
			p++;
			switch( *p ) {
			case 'n':
				sp->append("\n");
				break;
			case 'r':
				sp->append("\r");
				break;
			case 't':
				sp->append("\t");
				break;
			default:
				sp->append(*p);
				break;
			}
			continue;
		}
		if( *p == '$' ) {
			p++;
			if( *p != '{' ) {
				sp->append('$');
				sp->append(*p);
				continue;
			}
			stringvar = p+1;
			while( *p && *p != '}' ) p++;
			if( !*p ) {
				fprintf(stderr, "Invalid syntax in line '%s'.\n", pattern);
				abort();
			}
			strname = (char*)malloc( p-stringvar );
			strncpy(strname, stringvar, p-stringvar);
			strname[ p-stringvar ] = '\0';

			tmpbuf = (char*)scanned_vars->Get(strname);
			if( tmpbuf ) {
				//printf("Add var '%s' of '%s'\n", tmpbuf, strname);
				sp->append( tmpbuf );
			} else {
				printf("Var not found '%s'\n", strname);
			}
			free(strname);
			continue;
		}
		sp->append(*p);
	}
	strname = strdup(sp->p);
	delete sp;
	//printf("Translate '%s': '%s'\n", pattern, strname);

	return strname;
}

void pipeReadHandler( Pipe *p, char *buf )
{
	tlist *lines = split(buf, "\n");
	tnode *n, *n2;
	scriptsegment *seg;
	scriptline *line;
	scriptscan *scan, *readingto=NULL;
	scriptresponse *resp;
	char *line_input, *line_lower, *strptr, *strbuf;

	if( !running_script ) return;
	seg = current_scriptsegment(running_script);
	if( !seg ) return;
	line = current_scriptline(running_script);

	if( line && line->runstate == RUNSTATE_RUNNING ) {
//		printf("%s", buf);
		strexpand( &line->result, buf );
	}

	//printf("Lines count from '%s': %d\n", buf, lines->count);
	forTLIST( line_input, n, lines, char* ) {
		line_lower = strtolower(line_input);
		if( line->runstate == RUNSTATE_RUNNING ) {
			if( readingto ) {
				if( str_cn_cmp(readingto->endstr, "eof") ) {
					char *prev = (char*)scanned_vars->Get( readingto->saveas );
					char *tgt = (char*)malloc( strlen(prev) + strlen(line_input) + 4);
					strcpy(tgt, prev);
					free(prev);
					strcat(tgt, line_input);
					scanned_vars->Del( readingto->saveas );
					scanned_vars->Set( readingto->saveas, tgt);
					//printf("Scan '%s' onto '%s'\n", readingto->saveas, tgt);
				} //! else see if the endstr is in this line and stop readingto if found
			}
			forTLIST( scan, n2, line->scans, scriptscan* ) {
				strptr = strstr( line_input, scan->source );
				if( strptr ) {
					if( scan->endstr ) //! && not found in string
						readingto = scan;
					//printf("Found '%s'\n", scan->source);
					strptr += strlen(scan->source);
					//printf("Scan '%s' to '%s'\n", scan->saveas, strptr + scan->offset);
					scanned_vars->Del( scan->saveas );
					scanned_vars->Set( scan->saveas, strdup(strptr + scan->offset) );
				}
			}

			forTLIST( resp, n2, line->responses, scriptresponse* ) {
				strptr = strstr( line_input, resp->source );
				if( strptr ) {
					//printf("Found '%s'\n", resp->source);
					strbuf = strPrint( resp->response );
					//printf("Responding: '%s' from '%s'\n", strbuf, resp->response);
					p->write( strbuf );
					p->write( "\n" );
				}
			}
		}
		switch( running_script->runstate ) {
		case RUNSTATE_NONE: // initialization
			//fprintf(stdout, "init: '%s'\n", line_input);
			continue;
		case RUNSTATE_RUNNING: // command runtime
			//! detect warnings during runtime
			// if( line->runtype && list_regexp( line->runtype->warnings, line ) ) {
			if( seg ) {
				//line = (scriptline*)(seg->current ? seg->current->data : NULL);
				//fprintf(stdout, "cmd: %s: %s\n", line ? line->cmd : "[]", buf);
			}
			continue;
		case RUNSTATE_TESTING: // command test
			//printf("[testing] '%s'\n", line_input);
			if( seg ) {
				//line = (scriptline*)(seg->current ? seg->current->data : NULL);
				//fprintf(stdout, "test:(%s):'%s'\n", line ? line->cmd : "[]", buf);
			}
			continue;
		}
	}

	lines->Clear( free );
	delete lines;
}

void pipeIdleHandler( Pipe *p )
{
	//! detect idle time
	//! possibly error out, or just take note of the elapsed time
	// ... ;)
}



void pipeCmdHandler( Pipe *p )
{
	scriptsegment *seg = NULL;
	scriptline *line = NULL;
	char buf[512];
	char *rebuf;
	char exitCodeBuf[64];
	int iExit;
	const char *endofline, *ecode;

	if( !running_script ) {
		printf("no_run_cmd\n");
		return;
	}

	seg = current_scriptsegment(running_script);
	line = seg ? current_scriptline(running_script) : NULL;

	switch( running_script->runstate ) {
		case RUNSTATE_ERRORED:
			printf("Script had an error.\n");
			running_script->current = NULL;
			return;
		case RUNSTATE_COMPLETE:
			printf("Script completed.\n");
			running_script->current = NULL;
			return;
		case RUNSTATE_TESTING:
			// add results to segment  log
			printf("Command '%s':\n%s\n", line->cmd, line->result ? line->result : "none");
			line->runstate = RUNSTATE_TESTING;
			p->write("echo exit:$?\n");
			running_script->runstate = RUNSTATE_RESULT;
			//! check result patterns for regexp errors if we have any here, eg
			// if( list_regexp( line->result_buffer, line->runtype->errorlist ) ) { running_script->runstate = RUNSTATE_ERRORED; }
			return;
		case RUNSTATE_RESULT:
			ecode = str_r_str( p->readbuffer->p, "exit:" );
			if( ecode ) {
				ecode = ecode + 5;
				endofline = strstr( ecode, "\n" );
			} else endofline=NULL;

			if( !endofline ) {
				printf("\n\nCouldn't find eCode? :: '%s'\n", p->readbuffer->p);
				line->runstate = RUNSTATE_ERRORED;
			} else {
				strncpy( exitCodeBuf, ecode, endofline-ecode );
				exitCodeBuf[ endofline-ecode ] = 0;
				//printf("\nGot exit code: '%s'\n", exitCodeBuf);
				iExit = atoi(exitCodeBuf);

				if( iExit == 0 ) {
					line->runstate = RUNSTATE_COMPLETE;
				} else {
					// not so successful.. repeat loop if configured
					line->runstate = RUNSTATE_ERRORED;
					printf("Command failed! Exit code: %d\n", iExit);
					// log command output to failure buffer
					if( str_cn_cmp(line->cmd, "cd") == 0 ) {
						if( strstr(line->result, "No such file or directory") ) {
							free(line->result); line->result = NULL;
							sprintf(buf, "mkdir -p %s\n", line->cmd+3);
							p->write(buf);
							printf("Building directory %s\n", line->cmd+3);
							if( line->line_repeats == 0 ) {
								line->line_repeats = 2;
							}
						}
					}
				}
			}

			/*
			if( line->runstate == RUNSTATE_COMPLETE ) {
				refresh_prompt=false;
				/ * no need
				if( str_cn_cmp(line->cmd, "su exit") == 0 ) {
					refresh_prompt=true;
				} else if( str_cn_cmp(line->cmd, "su -") == 0 ) {
					refresh_prompt=true;
				} else if( str_cn_cmp(line->cmd, "su") == 0 ) {
					//! check for su <username>
					refresh_prompt=true;
				}
				* /
				if( refresh_prompt ) {
					p->getcommandprompt();
				}
			}
			*/

			line = next_scriptline(running_script);
			break;
	}

	if( line ) {
		bool inline_command=false;
		char replbuf[1024];

		*replbuf='\0';

		//printf("\n\n%s: %s[%d]\n", seg->comment ? seg->comment : "runtime", line->cmd, line->line_repeats );
		if( str_cn_cmp(line->cmd, "cd") == 0 ) {
			p->detect_new_cwd = true;
		} else if( str_cn_cmp(line->cmd, "su exit") == 0 || str_cn_cmp(line->cmd, "exit") == 0 ) {
			printf("got su exit\n");
			p->write( "exit\n" );
			inline_command=true;
			p->prompt_reset = true;
		} else if( str_cn_cmp(line->cmd, "input") == 0 ) {
			tlist *opts = split( line->cmd+6, "=" );
			char buf[255];
			printf( "%s", (char*)opts->FindData(0) );
			fgets(buf, 255, stdin);
			strip_newline(buf);
			scanned_vars->Del( (char*)opts->FindData(1) );
			scanned_vars->Set( (char*)opts->FindData(1), strdup(buf) );
			printf( "\n%s set to '%s'.\n", (char*)opts->FindData(1), buf );
			inline_command=true;
			p->write( "echo\n" );
		} else if( str_cn_cmp(line->cmd, "su") == 0 ) {
			p->support_password_block = true;
		} else if( str_cn_cmp(line->cmd, "source ~/.bashrc") == 0 ) {
			p->reopen();
			inline_command = true;
			p->getcommandprompt();
			p->write("echo\n");
		}
		if( !inline_command ) {
			if( *replbuf ) {
				p->write( replbuf );
			} else {
				rebuf = strPrint(line->cmd);
				//printf("Run command '%s':\n", rebuf);
				p->write( rebuf );
				free(rebuf);
				p->write(" 2>&1\n");
			}
		}
		running_script->runstate = RUNSTATE_TESTING;
		line->runstate = RUNSTATE_RUNNING;
	} else {
		printf("\nempty_cmd\n");
		running_script->current = NULL;
		return;
	}
}



int main(int ac, char *av[])
{
	int c, c0=0;
	char buf[32];
	scriptfile *scr;

	scanned_vars = new SMap(32);

	c = 1;
	if( scriptext ) {
		scr = loadScript(scriptext);
		c0 = 0;
	} else if( ac > 1 ) {
		scr = loadScriptFile(av[1]);
		c = 2;
		c0 = 1;
	} else {
		return 2;
	}

	for( ; c<ac; ++c ) {
		sprintf(buf, "arg%d", c-c0);
		scanned_vars->Set( buf, av[c] );
//		printf("%s: %s\n", buf, av[c]);
	}

	Pipe *target = pipeConnection();

	if( !target || !scr ) {
		return 2;
	}

	target->open();


	target->getcommandprompt();
	running_script = scr;

	printf("Running script.\n");
	target->write("echo\n");

	target->inputloop();

//! clean up and store logfiles


	printf("exit()\n");
//! unnecessary GC:
	delete target;

	return 0;
}

