#include "deploy.hh"
#include <unistd.h>
//#include <stropts.h>

Pipe::Pipe(void)
{
	fdm = fds = -1;
	childproc = -1;
	logfn = NULL;
	logbuf = NULL;
	readbuffer = new stringbuf(128);
	read_from_stdin = write_to_stdout = true;
	support_password_block = false;
	detect_new_username = detect_new_cwd = false;
	prompt_reset = false;
	readCB = NULL;
	idleCB = NULL;
	cmdlineCB = NULL;
	readBreakTest = NULL;
	breakTest = NULL;
	username = NULL;
	cmdprompt = NULL;
	cwd = NULL;
	data = NULL;
	matched_cmdline = false;
}

void Pipe::log(const char *logfilename)
{
	logfn = strdup( logfilename );
}

void Pipe::logtobuf( char **lbuf )
{
	logbuf = lbuf;
	if( logbuf )
		*logbuf = NULL;
}

bool pipe_test_newline( Pipe *p, char *buf )
{
	return ( strstr(buf,"\n") != NULL );
}

bool Pipe::open(void)
{
	char **args;

	args = (char**) calloc( sizeof(char*), 2 );
	args[0] = strdup("bash");
	args[1] = NULL;

	run( args );
	free( args[0] );
	free( args );

	return true;
}

void Pipe::exec(char **lbuf, const char *cmd)
{
	char *args[256];
	int i;
	const char *p;
	char buf[1025], *pbuf;
	
	logbuf = lbuf;
	*lbuf = NULL;
	i=0;
	for( p = cmd, pbuf=buf; *p; p++ ) {
		if( *p == ' ' ) {
			if( buf != pbuf ) {
				*pbuf = '\0';
				args[i++] = strdup( buf );
				pbuf = buf;
				continue;
			}
		} else {
			*pbuf++ = *p;
		}
	}
	if( buf != pbuf ) {
		*pbuf = '\0';
		args[i++] = strdup( buf );
	}
	args[i] = NULL;
	
	run( args );

	while( i > 0 ) {
		--i;
		free(args[i]);
	}
}


void Pipe::reopen(void)
{
	// close then reopen the pipe.
	if( fdm != -1 ) {
		write("exit\n");
		close();
	}

	char **args;

	args = (char**) calloc( sizeof(char*), 2 );
	args[0] = strdup("bash");
	args[1] = NULL;

	run( args );
	free( args[0] );
	free( args );
}

void Pipe::run(char* const args[])
{
//	int i;

	close();
	fdm = ::open("/dev/ptmx", O_RDWR);
	if( fdm < 0 ) {
		perror("open");
		abort();
	}
	
	grantpt( fdm );
	unlockpt( fdm );
	char *slavename = ptsname(fdm);

	// Fork into a shell
	childproc = fork();
	if( childproc == 0 ) {
		//printf("fork: child\n");
		if( fdm >= 0 )
			::close(fdm);
		
		setsid();
		
		fds = ::open(slavename, O_RDWR);
		if( fds < 0 ) {
			perror("open");
			abort();
		}
		
		//ioctl( fds, I_PUSH, "ptem" );
		//ioctl( fds, I_PUSH, "ldterm" );
		//ioctl( fds, I_PUSH, "ptem" );
		//ioctl( fds, I_PUSH, "ldterm" );

		// Redirect into 'fds'
		if( dup2( fds, STDIN_FILENO ) != STDIN_FILENO ) {
			fprintf(stdout, "dup2 stdin failed\n");
			abort();
		}
		if( dup2( fds, STDOUT_FILENO ) != STDOUT_FILENO ) {
			fprintf(stdout, "dup2 stdout failed\n");
			abort();
		}
		if( dup2( fds, STDERR_FILENO ) != STDERR_FILENO ) {
			fprintf(stdout, "dup2 stderr failed\n");
			abort();
		}
		// Grab the client shell
		
		
		struct termios tio;
		if (tcgetattr(0, &tio) < 0) {
			perror( "tcgetattr" );
			abort();
		}
		tio.c_oflag |= OCRNL;
		tio.c_oflag &= ~OPOST;
		tio.c_lflag |= ICANON | ISIG;
		tio.c_lflag &= ~ECHO;
		//tio.c_cflag &= ~CMSPAR;

		/*for( i = 0; i < NCCS; i++ ) {
			tio.c_cc[i] = 0;
		}
		*/
		if (tcsetattr(0, TCSANOW, &tio) < 0) {
			perror( "tcsetattr" );
			abort();
		}
		
		/*
		fprintf( stdout, "pipe " );
		for( i = 0; args[i]; i++ ) {
			fprintf( stdout, "%s ", args[i]);
		}
		fprintf( stdout, "\n" );
		*/
		
		//printf("child:run\n");
		//execlp( "echo", "hello", "world");
		execvp( args[0], args );
		//execlp("/usr/local/bin/bash", "bash", "-l", "-i", NULL);
		
		perror("execvp");
		abort();
	}
	if( fds >= 0 )
		::close(fds);
		
	struct termios tio;
	if (tcgetattr(fdm, &tio) < 0) {
		perror( "tcgetattr" );
		abort();
	}
	tio.c_oflag &= ~OPOST;
//	tio.c_oflag |= ONLCR;
	tio.c_iflag |= IGNCR; 
	tio.c_lflag &= ~ECHO;
	if (tcsetattr(fdm, TCSANOW, &tio) < 0) {
		perror( "tcsetattr" );
		abort();
	}
}

void Pipe::cleanstring( char *buf )
{
	char *pBuf = buf, *pTgt = buf;

	while( *pBuf ) {
		while( *pBuf && ( *pBuf == '\r' || ( !isspace(*pBuf) && !isprint(*pBuf) ) ) ) {
			pBuf++;
		}
		if( pTgt != pBuf )
			*pTgt = *pBuf;
		pTgt++;
		pBuf++;
	}
	*pTgt = '\0';
}

int Pipe::read(char *buf, int maxlen, bool nonblocking)
{
	int n, maxfd=fdm+1;
	fd_set ins;
	char *tptr;
	char *bufprompt;
	size_t size;

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	FD_ZERO( &ins );
	FD_SET( fdm, &ins );
	select( maxfd, &ins, NULL, NULL, &timeout );
	
	if( FD_ISSET( fdm, &ins ) ) {
		n = ::read( fdm, buf, maxlen );
		if( n <= 0 ) {
			buf[0] = '\0';
			return n;
		}
		
		buf[n] = '\0';
		//printf("Read: '%s'\n", buf);
		cleanstring(buf);
		//printf("read[%d]:%s\n", n, buf);
		if( support_password_block && ( (bufprompt=strstr(buf, "Password:")) != NULL ) ) {
			readbuffer->append(buf);
			if( readCB )
				readCB(this, buf );

			fprintf(stdout, "Enter password: ");
			size = 128;
			tptr = (char*)malloc(size);
			while( getline(&tptr, &size, stdin) == -1 ) {
				printf("Try again.\nEnter password: ");
			}
			write(tptr);
			free(tptr);
			support_password_block = false;
			prompt_reset = true;
		} else {
			if( cmdprompt && (bufprompt=match_commandline(buf))!=NULL ) { // trim output after a commandline appears
				*bufprompt = '\0';
				matched_cmdline=true;
			}
			readbuffer->append(buf);
			if( readCB )
				readCB(this, buf );
		}

		if( logfn ) {
			FILE *fp;
			fp = fopen( logfn, "a" );
			fputs( buf, fp );
			fclose( fp );
		}
		if( logbuf ) {
			if( *logbuf ) {
				tptr = (char*)realloc( *logbuf, strlen( *logbuf ) + n + 5 );
				strcat(tptr, buf);
			} else {
				tptr = (char*)malloc( n + 5 );
				strcpy(tptr, buf);
			}
			*logbuf = tptr;
		}
		return n;
	}
	
	return 0;
}

int Pipe::write(const char *buf)
{
	char buf2[1025];
	int len;

	char *tmpx = cmdprompt;
	cmdprompt=NULL;
	len = read( buf2, 1024 ); // this->read
	if( len > 0 ) {
		printf("note: read on write: '%s'\n", buf2);
		if( write_to_stdout )
			::write( STDOUT_FILENO, buf, len ); // write to the real stdout
	}
	cmdprompt=tmpx;

	len = strlen(buf);

	if( logfn ) {
		FILE *fp;
		fp = fopen( logfn, "a" );
		fputs( buf, fp );
		fclose( fp );
	}
	
	readbuffer->clear();
	return ::write( fdm, buf, len );
}

int Pipe::waitchild(int flags)
{
	int n;
	
	if( childproc == -1 )
		return 1;
	
	n = waitpid( childproc, NULL, flags );
	if( n > 0 )
		childproc = -1;
	
	return n;
}
char *Pipe::match_commandline( char *inbuf )
{
	uint32_t len = strlen(inbuf), plen = strlen(cmdprompt), cwdlen = strlen(this->cwd), userlen = strlen(this->username);
	char *pInbuf, *pCmdline, *pCwd;

	pInbuf = inbuf + len;
	pCmdline = cmdprompt + plen-1;

	while( *pInbuf == '\0' || isspace(*pInbuf) ) pInbuf--;

	do {
		if( *pCmdline == ',' ) {
			if( detect_new_cwd ) {
				pCwd=pInbuf+1;
				pCmdline--;
				while( *pInbuf != *pCmdline ) {
					pInbuf--;
				}
				if( this->cwd ) free(this->cwd);
				this->cwd = strndup( pInbuf+1, (int)(pCwd-pInbuf)-1 );
				printf("Detected new cwd: '%s'\n", this->cwd);
				detect_new_cwd = false;
			} else {
				if( ( pInbuf+1-cwdlen < inbuf ) || ( str_cn_cmp( pInbuf+1-cwdlen, this->cwd ) != 0 ) ) {
					return NULL;
				}
				pInbuf -= cwdlen;
				pCmdline--;
			}
		} else if( *pCmdline == '$' && *pInbuf == '#' ) {
			pInbuf--;
			pCmdline--;
		} else if( *pCmdline == '`' ) {
			if( detect_new_username ) {
				pCwd=pInbuf+1;
				pCmdline--;
				while( pInbuf > inbuf && *pInbuf != *pCmdline ) {
					pInbuf--;
				}
				if( this->username ) free(this->username);
				this->username = strndup( pInbuf+1, (int)(pCwd-pInbuf)-1 );
				printf("Detected new username: '%s'\n", this->username);
				detect_new_username = false;
			} else {
				if( ( pInbuf+1-userlen < inbuf ) || ( str_cn_cmp( pInbuf+1-userlen, this->username ) != 0 ) ) {
					return NULL;
				}
				pInbuf -= userlen;
				pCmdline--;
			}
		} else if( *pCmdline != *pInbuf ) {
			return NULL;
		} else {
			pInbuf--;
			pCmdline--;
		}
	} while( pInbuf > inbuf && pCmdline > cmdprompt );

	return ( pCmdline <= cmdprompt ) ? pInbuf : NULL;
}
void print_string( const char *x )
{
	const char *p=x;
	printf("%s:\n", x);
	while( *p ) {
		printf("%u,", (unsigned)*p);
		p++;
	}
	printf("\n");
}
void Pipe::getcommandprompt( void )
{
	//printf("show prompt\n");

	// temporarily disable normal hooks
	pipe_read_cb *tempRead = readCB;
	pipe_cb *tempIdle = idleCB;

	pipe_readtest_cb *tempReadTest = readBreakTest;
	pipe_test_cb *tempTest = breakTest;
	readCB = NULL;
	idleCB = NULL;
	breakTest = NULL;
	readBreakTest = pipe_test_newline;
	char buf[512];
//	char *tmp_cmdline;

	stringbuf *sb;

	readbuffer->clear();

	// trigger a commandline prompt to be displayed
	//printf("\ngetcommandprompt()\n");
	if( this->cmdprompt ) {
		free(this->cmdprompt);
		this->cmdprompt = NULL;
	}
	write("echo && PS1=_\\\\u:\\\\w# && whoami && pwd\n");

	// run loops to fill the input buffer
	int i=0, x=0;
//	int attempts=0;
	int n=0;
	char *searchptr;

	while( n < 4 ) {
		inputloop(); // read lines

		//printf("Read buffer: '%s'\n", readbuffer->p+i);

		while( n < 4 ) {
			searchptr = strstr(readbuffer->p+i, "\n");
			if( n == 0 ) {
				// old commandline
				if( searchptr ) {
					n=1;
					x = (searchptr - readbuffer->p);
					//tmp_cmdline = strndup( readbuffer->p+i, x );
					//printf("Command source: '%s'\n", tmp_cmdline);
					//strip_newline(tmp_cmdline);
					i = x+1;
				} else break;
			} else if( n == 1 ) {

				if( searchptr ) {
					n=2;

				// find username
					x = searchptr - readbuffer->p;
					if( this->username ) free(this->username);
					this->username = strndup( readbuffer->p+i, x-i );
					printf("Working username: '%s'\n", this->username);
					i = x+1;
				} else break;


			} else if( n == 2 ) {

				if( searchptr ) {
					n=3;

				// find cwd
					x = searchptr - readbuffer->p;
					if( this->cwd ) free(this->cwd);
					this->cwd = strndup( readbuffer->p+i, x-i );

					sb = new stringbuf(this->cwd);
					if( str_c_cmp(this->username,"root") == 0 ) {
						sprintf(buf, "/%s", this->username);
					} else {
						sprintf(buf, "/home/%s", this->username);
					}
					sb->replace( buf, "~" );
					free( this->cwd );
					this->cwd = strdup(sb->p);
					delete sb;

					printf("Working directory: '%s'\n", this->cwd);
					i = x+1;
				} else break;

			} else if( n == 3 ) {
				/*

				x = (searchptr - readbuffer->p);
				tmp_cmdline = strndup( readbuffer->p+i, x );
				printf("Command source: '%s'\n", tmp_cmdline);
				strip_newline(tmp_cmdline);
				i = x+1;

				sb = new stringbuf(tmp_cmdline);
				sb->replace( this->cwd, "," );
				sb->replace( this->username, "`" );
				this->cmdprompt = strdup(sb->p);
				delete sb;
				printf("Command prompt: '%s'\n", this->cmdprompt);
				*/

				n = 4;
				break;
			}
		}
	}

	cmdprompt = strdup("_`:,$");
//	printf(" Prompt looks like: '%s'\n", cmdprompt?cmdprompt:"null");
	//print_string(cmdprompt);

	// re-enable hooks
	readCB = tempRead;
	idleCB = tempIdle;
	breakTest = tempTest;
	readBreakTest = tempReadTest;

	// normal processing resumes
}
void Pipe::inputloop()
{
	fd_set ins, outs, excepts;
	int maxfd;
	char buf[1025];
	int len;
	struct timeval timeout;
	
//	printf("parent:inputloop\n");
	maxfd = fdm+1;
		
	while( 1 ) {
		FD_ZERO( &ins );
		FD_ZERO( &outs );
		FD_ZERO( &excepts );
		
		if( read_from_stdin ) {
			FD_SET( STDIN_FILENO, &ins ); // messages from keyboard/user
			FD_SET( STDIN_FILENO, &excepts );
		}
		FD_SET( fdm, &ins ); // messages from pipe
		FD_SET( fdm, &excepts );
		
		timeout.tv_sec = 0;
		timeout.tv_usec = 100000;
		select( maxfd, &ins, NULL, &excepts, &timeout );

		/* Read from stdin and write to pipe */
		if( FD_ISSET( STDIN_FILENO, &ins ) ) {
			len = ::read( STDIN_FILENO, buf, 1024 );
			if( len == 0 ) {
				fprintf( stdout, "EOF read on stdin.\n");
				break;
			}
			
			buf[len] = '\0';
			::write( fdm, buf, len );
			// fsync( fdm );
		}
		
		/* Read from pipe, echo to stdout */
		if( FD_ISSET( fdm, &ins ) ) {
			len = read( buf, 1024 ); // this->read
			if( len > 0 ) {
				if( write_to_stdout )
					::write( STDOUT_FILENO, buf, len ); // write to the real stdout

				if( readBreakTest && readBreakTest(this, buf) ) {
					return;
				}
			} else {
				fprintf( stdout, "EOF on pipe.\n" );
				break;
			}
		} else {
			if( prompt_reset ) {
				prompt_reset=false;
				getcommandprompt();
				write("echo\n");
			} else if( matched_cmdline ) {
				matched_cmdline = false;
				if( cmdlineCB )
					cmdlineCB(this);
			} else if( idleCB ) {
				idleCB(this);
			}
		}
		
		if( FD_ISSET(fdm, &excepts) || FD_ISSET(0, &excepts) ) {
			fprintf( stdout, "Session ended: pipe crashed.\n" );
			break;
		}
		
		/*
		if( waitchild(WNOHANG) != 0 ) {
			fprintf( stdout, "Session ended peacefully.\n" );
			break;
		}
		*/

		if( breakTest && breakTest(this) ) {
			::write( fdm, "exit\n", 5 );
			break;
		}
	}
	
	maxfd++;
	fprintf( stdout, "Waiting for child to exit\n");
	waitchild( WNOHANG );
	fprintf( stdout, "Closing pipe\n" );
	// disconnect
	close();
}

void Pipe::FD_TSET(fd_set *ins, fd_set *excepts)
{
	if( fdm < 0 ) return; //!

	FD_SET( fdm, ins ); // messages from pipe
	FD_SET( fdm, excepts );
}
int Pipe::FD_TEST(fd_set *ins, fd_set *excepts)
{
	if( fdm < 0 ) return false; //!

	if( FD_ISSET( fdm, excepts ) ) {
		fprintf( stdout, "Session ended: pipe crashed.\n" );
		close();
		return -1;
	} else if( FD_ISSET( fdm, ins ) ) {
		return 1;
	}
	return 0;
}
int Pipe::FD_READ(char *buf, int bufsize)
{
	int len = read( buf, bufsize );
	if( len <= 0 ) {
		fprintf( stdout, "EOF on pipe.\n" );
		return len;
	}
	if( write_to_stdout )
		::write( STDOUT_FILENO, buf, len ); // write to the real stdout

	if( readBreakTest && readBreakTest(this, buf) ) {
		return 0;
	} else {
		if( prompt_reset ) {
			prompt_reset=false;
			getcommandprompt();
			write("echo\n");
		} else if( matched_cmdline ) {
			matched_cmdline = false;
			if( cmdlineCB )
				cmdlineCB(this);
		} else if( idleCB ) {
			idleCB(this);
		}
	}

	if( breakTest && breakTest(this) ) {
		::write( fdm, "exit\n", 5 );
		return 0;
	}

	return len;
}
void Pipe::close(void)
{
	if( fdm != -1 ) {
		::close( fdm );
		fdm = -1;
	}
	if( fds != -1 ) {
		::close( fds );
		fds = -1;
	}
	if( childproc > 0 ) {
		waitchild(WNOHANG);
		childproc = -1;
	}
}
