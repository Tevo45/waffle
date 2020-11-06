#include <u.h>
#include <libc.h>
#include <ctype.h>
#include <stdio.h>
#include <bio.h>
#include <String.h>

char *stubhost = "error.host\t1";
char *spacetab = "    ";
char *srvroot  = "./";
char *defprog  = "	\
	# This is a comment and shouldn't be considered\n\
	info system is $sysname on $cputype\n\
	info $objtype \\$notavar $nonexistant \\\\$yay\n\
	lsdir .\n		\
";

enum
{
	OP_COMMENT,
	OP_LSDIR,
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
	fprint(2, "usage: [-r root] [-d defprog] %s", argv0);
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
		{"#",		OP_COMMENT},
		{"lsdir",	OP_LSDIR},
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

/*
 * Like %s, but replaces tabs with spacetab and interpolates variables
 */
#pragma varargck type "G" char*
int
gopherfmt(Fmt *fmt)
{
	String *scratch;
	char *str, *buf;
	int ret = 0;

	scratch = s_new();
	str = va_arg(fmt->args, char*);
	for(; ret >= 0 && *str != '\0'; str++)
		switch(*str)
		{
		case '\t':
			ret = fmtprint(fmt, "%s", spacetab);
			break;
		case '$':
			s_restart(scratch);

			str++;
			while(!isspace(*str))
				s_putc(scratch, *str++);
			str--;
			s_terminate(scratch);

			buf = getenv(s_to_c(scratch));
			if(buf == nil)
				fprint(2, "getenv: %r\n");
			else
			{
				/* FIXME using %G here resolves variables recursively */
				fmtprint(fmt, "%G", buf);
				free(buf);
			}

			break;
		case '\\':
			/* FIXME cannot literally print \$ with this */
			if(*(str+1) == '$')
				str++;
		default:
			fmtprint(fmt, "%c", *str);
		}

	s_free(scratch);
	return ret;
}

#pragma varargck argpos info 1
int
info(char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	fmt = smprint("i%G\t\t%s\r\n", fmt, stubhost);
	n = vfprint(1, fmt, args);
	free(fmt);
	va_end(args);

	return n;
}

#pragma varargck argpos info 1
int
error(char *fmt, ...)
{
	int n;
	va_list args;

	va_start(args, fmt);
	fmt = smprint("3%G\t\t%s\r\n", fmt, stubhost);
	n = vfprint(1, fmt, args);
	free(fmt);
	va_end(args);

	return n;
}

int
entry(char type, char *name, char *path, char *host, int port)
{
	return print("%c%G\t%G\t%G\t%d\r\n", type, name, path, host, port);
}

/*
 * FIXME we should timeout at some point, so a user can't take us down
 * by opening connections but never sending a CRLF
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

void
servedir(char *path)
{
	Dir *dir;
	long n;
	int fd;

	dir = dirstat(path);
	if(dir == nil)
	{
	error:
		info("Error: %r");
		entry(GOPHER_DIR, "/", "", "neptune.shrine", 70);
		return;
	}
	if(dir->qid.type&QTDIR)
	{
		free(dir);
		info("%G", path);
		fd = open(path, OREAD);
		if(fd < 0)
			goto error;
		n = dirreadall(fd, &dir);
		if(n < 0)
			goto error;
		for(int c = 0; c < n; c++)
		{
			char *fpath = smprint("%s/%s", path, dir[c].name);
			char type = dir[c].mode&DMDIR ? GOPHER_DIR : GOPHER_FILE;
			entry(type, dir[c].name, fpath, "neptune.shrine", 70);
			free(fpath);
		}
	}
	else
		info("not supported...");
}

String*
getprog(char *path)
{
	/* FIXME actually read index.waffle */
	return s_copy(defprog);
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
interprog(String *prog)
{
	int op;
	String *line, *curtok;
	s_restart(prog);

	curtok = s_new();
	while((line = nextcomm(prog)) != nil)
	{
		s_parse(line, curtok);
		switch(op = opforcmd(s_to_c(curtok)))
		{
		case OP_COMMENT:
			break;
		case OP_LSDIR:
			servedir("/usr/tevo");
			break;
		case GOPHER_INFO:
			info("%G", line->ptr);
			break;
		case GOPHER_ERR:
			error("%G", line->ptr);
			break;
		case -1:
		default:
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
	char *path;

	ARGBEGIN {
	case 'r':
		srvroot = EARGF(usage());
		break;
	case 'd':
		fprint(2, "defprog: not implemented\n");
		exits("noimpl");
	default:
		usage();
	} ARGEND;

	fmtinstall('G', gopherfmt);

	req = readrequest();
	path = parsepath(s_to_c(req));
	if(path == nil)
	{
		error("cannot parse request: %r");
		goto end;
	}
	prog = getprog(path);
	if(prog == nil)
	{
		error("cannot find index for %s: %r", path);
		goto end;
	}
	interprog(prog);
	
//	servedir(path);
//	info("-----");
//	info("waffle on %s (plan9/%s)", getenv("sysname"), getenv("cputype"));
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
