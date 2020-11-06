#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <stdio.h>
#include <bio.h>
#include <String.h>

char *querystr;
char *stubhost   = "error.host\t1";
char *gopherhost = "localhost";
char *spacetab   = "    ";
char *srvroot    = "./";
char *defprog    = "\n\
	info you seem to be lost...\n\
	dir 'back home' /\n\
";

enum
{
	OP_COMMENT,
	OP_EXEC,
};

enum
{
		/* Unofficial */
	GOPHER_INFO			= 'i',
	GOPHER_DOC			= 'd',
	GOPHER_HTML			= 'h',
	GOPHER_AUDIO		= 's',
		/* RFC1436 */
	GOPHER_FILE			= '0',
	GOPHER_DIR,
	GOPHER_PHONEBOOK,
	GOPHER_ERR,
	GOPHER_MAC_FILE,
	GOPHER_DOS_FILE,
	GOPHER_UNIX_FILE,
	GOPHER_SEARCH,
	GOPHER_TELNET,
	GOPHER_BIN,
	GOPHER_REDUNDANT	= '+',
	GOPHER_TN3270		= 'T',
	GOPHER_GIF			= 'g',
	GOPHER_IMAGE		= 'I',
};

void
usage(void)
{
	fprint(2, "usage: [-r root] [-d defprog] [-h hostaddr] %s", argv0);
	exits("usage");
}

int
opforcmd(char* cmd)
{
	struct
	{
		char *k;
		int op;
	} ops[] = {
		{"--",		OP_COMMENT},
		{"exec",	OP_EXEC},

		{"info",	GOPHER_INFO},
		{"doc",		GOPHER_DOC},
		{"html",	GOPHER_HTML},

		{"file",		GOPHER_FILE},
		{"dir",			GOPHER_DIR},
		{"phonebook",	GOPHER_PHONEBOOK},
		{"error",		GOPHER_ERR},
			/* {MAC,UNIX,DOS}-FILE */
		{"search",		GOPHER_SEARCH},
		{"telnet",		GOPHER_TELNET},
		{"bin",			GOPHER_BIN},
			/* REDUNDANT, TN3270 */
		{"gif",			GOPHER_GIF},
		{"img",			GOPHER_IMAGE},

		{nil, 0},
	};

	for(int c = 0; ops[c].k != nil; c++)
		if(strcmp(ops[c].k, cmd) == 0)
			return ops[c].op;

	if(strlen(cmd) == 1)
		return *cmd;

	return -1;
}

char*
readall(char *path)
{
	char *buf;
	int fd, bsize = 4096;

	if((fd = open(path, OREAD)) < 0)
		sysfatal("open: %r");

	buf = malloc(bsize);
	if(buf == nil)
		sysfatal("malloc: %r");

	for(int c = 0; read(fd, buf+(c*bsize), bsize) != 0; c++)
	{
		buf = realloc(buf, ((c+2)*bsize));
		if(buf == nil)
			sysfatal("realloc: %r");
	}

	close(fd);
	return buf;
}

/*
 * Funny bug, sometimes strings allocated on multiple invocations
 * of the same function ends up with leftovers from previous invocations,
 * probably getting assigned the same memory location as before (which
 * wasn't cleaned up, so it's full of garbage), and the string "picks it up".
 * s_terminate doesn't seem to do it's job, somehow, or I'm doing something
 * wrong.
 * FIXME investigate this
 */
void
s_cleanup(String *str)
{
	for(char *c = str->base; c < str->end; c++)
		*c = 0;
}

/*
 * Like %s, but replaces tabs with spacetab
 */
#pragma varargck type "G" char*
int
gopherfmt(Fmt *fmt)
{
	String *scratch;
	char *str;
	int ret = 0;

	scratch = s_new();
	str = va_arg(fmt->args, char*);
	for(; ret >= 0 && *str != '\0'; str++)
		switch(*str)
		{
		case '\t':
			ret = fmtprint(fmt, "%s", spacetab);
			break;
		default:
			fmtprint(fmt, "%c", *str);
		}

	s_free(scratch);
	return ret;
}

/*
 * Like %G, but interpolates variables
 */
#pragma varargck type "V" char*
int
varfmt(Fmt *fmt)
{
	String *out, *scratch;
	char *str, *buf;
	int ret;

	scratch = s_new();
	out = s_new();
	str = va_arg(fmt->args, char*);
	for(; *str != '\0'; str++)
		switch(*str)
		{
		case '$':
			s_restart(scratch);

			str++;
			while(isalnum(*str))
				s_putc(scratch, *str++);
			str--;
			s_terminate(scratch);

			buf = getenv(s_to_c(scratch));
			if(buf == nil)
				fprint(2, "getenv: %r\n");
			else
			{
				s_append(out, buf);
				free(buf);
			}

			break;
		case '\\':
			/* FIXME cannot literally print \$ with this */
			if(*(str+1) == '$')
				str++;
		default:
			s_putc(out, *str);
		}

	s_terminate(out);
	ret = fmtprint(fmt, "%G", s_to_c(out));

	s_free(scratch);
	s_free(out);

	return ret;
}

#pragma varargck argpos info 1
int
info(char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	fmt = smprint("i%V\t\t%s\r\n", fmt, stubhost);
	n = vfprint(1, fmt, args);
	free(fmt);
	va_end(args);

	return n;
}

#pragma varargck argpos error 1
int
error(char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	fmt = smprint("3%V\t\t%s\r\n", fmt, stubhost);
	n = vfprint(1, fmt, args);
	free(fmt);
	va_end(args);

	return n;
}

int
entry(char type, char *name, char *path, char *host, char *port)
{
	return print("%c%V\t%V\t%V\t%V\r\n", type, name, path, host, port);
}

/*
 * FIXME we should timeout at some point, so a user can't take us down
 * by opening connections but never sending a CRLF.
 * Maybe it's not our job.
 */
String*
readrequest(void)
{
	String *str = s_new();
	int c;
	while((c = getchar()) != EOF)
	{
		s_putc(str, c);
		if(str->ptr[-2] == '\r' && str->ptr[-1] == '\n')
			break;
	}
	str->ptr -= 2;
	s_terminate(str);
	return str;
}

/* TODO canonize path */
char*
parsepath(char* req)
{
	if(strlen(req) == 0)
		return strdup("/");
	return strdup(req);
}

/* FIXME disallow requests outside gopherroot */
String*
getprog(char *path)
{
	String *prog;
	char *apath, buf[4096];
	int fd, n;

	apath = smprint("%s/%s", srvroot, path);
	if(apath == nil)
		sysfatal("smprint: %r");

	if(chdir(apath) != 0)
	{
		free(apath);
		return s_copy(defprog);
	}
	free(apath);

	if((fd = open("index.waffle", OREAD)) < 0)
		return s_copy(defprog);

	prog = s_new();
	while((n = read(fd, buf, sizeof(buf))) != 0)
		s_memappend(prog, buf, n);

	close(fd);
	return prog;
}

String*
nextcomm(String *prog)
{
	String *comm;

	while(isspace(*prog->ptr))
		prog->ptr++;

	if(*prog->ptr == '\0')
		return nil;

	comm = s_new();
	s_cleanup(comm);

	for(; *prog->ptr != '\0'; prog->ptr++)
	{
		if(*prog->ptr == '\n')
			break;
		s_putc(comm, *prog->ptr);
	}

	s_terminate(comm);
	s_restart(comm);

	return comm;
}

void
parseentry(int op, String *line)
{
	char *defval[] = {
		nil, nil,
		[2] = gopherhost,
		[3] = "70",
	};
	String *p[] = {nil, nil, nil, nil};
	for(int c = 0; c < 4; c++)
	{
		p[c] = s_new();
		s_parse(line, p[c]);
		s_terminate(p[c]);
		if(strlen(s_to_c(p[c])) == 0)
			if(defval[c] != nil)
				s_append(p[c], defval[c]);
			else
			{
				fprint(2, "incomplete command '%c'\n", op);
				goto cleanup;
			}
	}
	entry((char)op, s_to_c(p[0]), s_to_c(p[1]), s_to_c(p[2]), s_to_c(p[3]));
cleanup:
	for(int c = 0; c < 4; c++)
		if(p[c] != nil)
			s_free(p[c]);
}

void
shellexec(char *cmd)
{
	Waitmsg *msg;
	char *argv[] = { "/bin/rc", "-c", cmd };
	int rcin[2];
	pipe(rcin);
	switch(rfork(RFFDG|RFPROC|RFMEM|RFNAMEG|RFNOTEG|RFREND))
	{
	case 0:
		close(0);
		dup(rcin[0], 0);
		close(rcin[0]);
		exec("/bin/rc", argv);
		sysfatal("exec: %r");
		break;
	case -1:
		sysfatal("rfork: %r");
		break;
	default:
		write(rcin[1], querystr, strlen(querystr));
		msg = wait();
		if(strlen(msg->msg) != 0)
			fprint(2, "rc: %s\n", msg->msg);
	}
}

void
interprog(String *prog)
{
	int op;
	String *line, *curtok;
	s_restart(prog);

	curtok = s_new();
	while((line = nextcomm(prog)) != nil)
	{
		s_parse(line, curtok);
		s_terminate(curtok);
		switch(op = opforcmd(s_to_c(curtok)))
		{
		case OP_COMMENT:
			break;
		case OP_EXEC:
			shellexec(line->ptr);
			break;
		case GOPHER_INFO:
			info("%V", line->ptr);
			break;
		case GOPHER_ERR:
			error("%V", line->ptr);
			break;
		default:
			parseentry(op, line);
			break;
		case -1:
			error("command not implemented: %G", s_to_c(curtok));
		}
		s_reset(curtok);
		s_free(line);
	}
	s_free(curtok);
}

void
main(int argc, char **argv)
{
	String *req, *prog;
	char *path, pwd[512];

	ARGBEGIN {
	case 'r':
		srvroot = EARGF(usage());
		break;
	case 'd':
		defprog = readall(EARGF(usage()));
		if(defprog == nil)
			sysfatal("readall: %r");
		break;
	case 'h':
		gopherhost = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND;

	fmtinstall('G', gopherfmt);
	fmtinstall('V', varfmt);

	if(chdir(srvroot) != 0)
		sysfatal("chdir: %r");

	if(getwd(pwd, sizeof(pwd)) == 0)
		sysfatal("getwd: %r");

	req = readrequest();
	querystr = s_to_c(req);

	path = parsepath(s_to_c(req));
	if(path == nil)
	{
		error("cannot parse request: %r");
		goto end;
	}

	putenv("gopherroot", pwd);
	putenv("gopherhost", gopherhost);
	putenv("querystr", s_to_c(req));
	putenv("pathstr", path);

	prog = getprog(path);
	if(prog == nil)
	{
		error("cannot find index for %G: %r", path);
		goto end;
	}
	interprog(prog);

end:
	print(".");
	/*
	 * we're quitting, don't think it would be bad to just leave
	 * those dangling for the os to cleanup
	 */
	s_free(req);
	free(path);
	exits(0);
}
