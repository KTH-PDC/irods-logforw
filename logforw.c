
/* File: LOGFORW.C. */

/* Simple program to watch log files and forward changes to syslog. */

/* System include files. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <regex.h>
#include <fnmatch.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <signal.h>
#include <libgen.h>
#include <pwd.h>
#include <syslog.h>

/* Status codes. */
#define FAILURE ((int) 1)
#define SUCCESS ((int) 0)

/* Boolean values. */
#define true ((int) 1)
#define false ((int) 0)

/* End of string character. */
#define EOS '\0'

/* New line character. */
#define NL '\n'

/* Sleep delay. */
#define SLEEP_DELAY 2

/* Regex routines error buffer length. */
#define ERRBUF_MAX 132

/* Printable representation of the new line character. */
#define NLPRINT '|'

/* Printable representation of any non-printable character. */
#define NONPRINT '.'

/* Limit on how much data can arrive during one scan. */
#define BUFLEN_MAX ((size_t) 2097152)

/* Amount of information to forward to the log. */
#define LINELENGTH_MAX ((int) 16384)

/* Line length to use to show the first few lines when too long. */
#define LONG_LINE ((int) 132)

/* Default pattern. */
#define DEFAULT_PATTERN "*"

/* Default log file name. */
#define LOG "/var/tmp/logforw/logforw.log"

/* Default facility name. */
#define DEFAULT_FACILITY "logforw"

/* Verbose messages. */
int verbose = false;

/* Daemonize. */
int background = true;

/* Main daemon loop sleep delay. */
int delayseconds = SLEEP_DELAY;

/* Log file name. */
char *logfilename = LOG;

/* Facility name. */
char *facility = DEFAULT_FACILITY;

/* Print error message and quit. */

void
error (char *msg)
{
	(void) fprintf (stderr, "%s\n", msg);
	exit (FAILURE);
}

/* Allocate memory. */

void *
allocate (size_t s)
{
	void *p;

	p = malloc (s);
	if (p == NULL)
	{
		error ("Cannot allocate");
	}
	return (p);
}

/* Macro to allocate memory. */

#define new(type) ((type *) allocate (sizeof (type)))

/* Compare strings. */

int
eqs (char *s1, char *s2)
{
	return (strcmp (s1, s2) == 0);
}

/* Match string to filename pattern. */

int
match (char *pattern, char *string)
{
	int status;

	if (pattern == NULL)
	{

		/* Return early with success. */
		return (true);
	}

	status = fnmatch (pattern, string, 0);
	if (status == 0)
	{
		return (true);
	}
	else if (status == FNM_NOMATCH)
	{
		return (false);
	}
	else
	{
		(void) fprintf (stderr, "Pattern is '%s'\n", pattern);
		(void) fprintf (stderr, "String is '%s'\n", string);
		(void) fprintf (stderr, "Status is '%d'\n", status);
		error ("Function fnmatch failed");
	}
}

/* Match string to regular expression. */

int
regmatch (char *pattern, char *string)
{
	int status;
	regex_t re;
	char errbuf[ERRBUF_MAX];

	if (pattern == NULL)
	{

		/* No pattern, return immediately. */
		return (true);
	}

	/* Compile regular expression. */
	status = regcomp (&re, pattern, REG_EXTENDED|REG_NOSUB);
	if (status != 0)
	{
		(void) fprintf (stderr, "Regular expression is '%s'\n", pattern);
		(void) fprintf (stderr, "String is '%s'\n", string);
		(void) regerror (status, &re, errbuf, (size_t) ERRBUF_MAX);
		(void) fprintf (stderr, "Regexp error %s\n", errbuf);
		perror ("Error context");
		error ("Error compiling regular expression");
	}

	/* Compare and free. */
	status = regexec (&re, string, (size_t) 0, NULL, 0);
	regfree (&re);

	/* Return accordingly. */
	if (status == 0)
	{
		return (true);
	}
	else if (status == REG_NOMATCH)
	{
		return (false);
	}
	else
	{
		(void) fprintf (stderr, "Matching with string %s\n", pattern);
		error ("Regular expression matching failure\n");
	}
}

/* Delay. */

void
delay (int seconds)
{
	unsigned int sleep_status;

	if (seconds == 0)
	{
		error ("Invalid argument to sleep");
	}
	sleep_status = sleep ((unsigned int) seconds);
	if (sleep_status != (unsigned int) 0)
	{
		error ("Error returned by sleep");
	}
}

/* Check if it is a directory. */

int
directory (char *path)
{
	int status;
	struct stat s;

	/* Get status information. */
	status = stat (path, &s);
	if (status != 0)
	{
		(void) fprintf (stderr, "Function stat returned error for %s\n", path);
		error ("Cannot stat");
	}

	/* Return accordingly. */
	return (S_ISDIR (s.st_mode));
}

/* Check if it is a real name. */

int
realname (char *path)
{
	char *start;
	char *end;
	char *p;
	char ch;

	/* Scan path in reverse. Stop at slash or at start. */
	start = path;
	end = path + strlen (path);
	p = end;
	ch = *p;
	while (p != start)
	{
		if (ch == '/')
		{
			p++;
			break;
		}
		p--;
		ch = *p;
	}

	/* Return whether the name was not dot or dot-dot. */
	return (! ((strcmp (p, ".") == 0) || (strcmp (p, "..") == 0)));
}

/* File catalog is an array of file descriptors. */

/* File descriptor. */
typedef struct
{

	/* Sequence number. */
	int sn;

	/* File name. */
	char *name;

	/* File descriptor. */
	int fd;

	/* Status buffers, last and current. */
	struct stat *laststatbuf;
	struct stat *statbuf;

	/* Modified times, last and current. */
	time_t lastmodified;
	time_t modified;

	/* End offset positions of the file, last and current. */
	off_t lastendpos;
	off_t endpos;
} file_t;

/* Maximum number of entries in file catalog. */
#define FILES_MAX 8192

/* Number of files in the catalog. */
int nfiles = 0;

/* File catalog. */
file_t *files[FILES_MAX];

/* Print configuration. */

void
print_configuration (void)
{
	if (verbose)
	{
		printf ("Verbose on\n");
	}
	if (! background)
	{
		printf ("Debug on\n");
	}
	printf ("Delay in main daemon loop %d\n", delayseconds);
	printf ("Data limit on data during a scan is %lu\n",
		(unsigned long) BUFLEN_MAX);
	printf ("Maximum amount to forward to the log is %d\n", LINELENGTH_MAX);
	printf ("Line length to show sample buffer is %d\n", LONG_LINE);
	printf ("File table size is %d\n", FILES_MAX);
	printf ("Facility is %s\n", facility);
	printf ("Log file name is %s\n", logfilename);
}

/* Initialize file catalog. */

void
init_file (void)
{
	int i;
	file_t *f;

	if (verbose)
	{
		printf ("Initializing file table with %d entries\n", FILES_MAX);
	}
	for (i=0; i<FILES_MAX; i++)
	{
		files[i] = NULL;
	}
}

/* Print status block. */

void
print_statbuf (struct stat *st)
{
	printf ("%40s: %ld\n", "Device id", (long) st->st_dev);
	printf ("%40s: %ld\n", "Inode number", (long) st->st_ino);
	printf ("%40s: %lo\n", "Access mode", (unsigned long) st->st_mode);
	printf ("%40s: %ld\n", "Number of links", (long) st->st_nlink);
	printf ("%40s: %ld\n", "Uid", (long) st->st_uid);
	printf ("%40s: %ld\n", "Gid", (long) st->st_gid);
	printf ("%40s: %ld\n", "Special device id", (long) st->st_rdev);
	printf ("%40s: %lld\n", "Size", (long long) st->st_size);
	printf ("%40s: %lld\n", "Block size", (long long) st->st_blksize);
	printf ("%40s: %lld\n", "Blocks allocated", (long long) st->st_blocks);
	printf ("%40s: %s", "Status change", ctime (&st->st_ctime));
	printf ("%40s: %s", "Access", ctime (&st->st_atime));
	printf ("%40s: %s", "Modification", ctime (&st->st_mtime));
}

/* Print file. */

void
print_file (file_t *f)
{
	printf ("%40s: %d\n", "Sequence number", f->sn);
	printf ("%40s: %s\n", "File name", f->name);
	printf ("%40s: %d\n", "File descriptor number", f->fd);
	print_statbuf (f->laststatbuf);
	print_statbuf (f->statbuf);
	printf ("%40s: %s", "Last modification time", ctime (&f->lastmodified));
	printf ("%40s: %s", "Current modification time", ctime (&f->modified));
	printf ("%40s: %lld\n", "Last end position", (long long) f->lastendpos);
	printf ("%40s: %lld\n", "Current end position", (long long) f->endpos);
}

/* Print file catalog. */

void
print_catalog (void)
{
	int i;

	for (i=0; i<FILES_MAX; i++)
	{
		if (files[i] != NULL)
		{
			if (files[i]->sn != -1)
			{
				printf ("\n");
				print_file (files[i]);
			}
		}
	}
}

/* Get file from the catalog. */

file_t *
get_file (char *name)
{
	file_t *f;
	int i;

	f = NULL;
	for (i=0; i<FILES_MAX; i++)
	{
		f = files[i];
		if (f != NULL)
		{
			if (f->sn != -1)
			{
				if (strcmp (f->name, name) == 0)
				{
					break;
				}
			}
		}
	}
	return (f);
}

/* Put file into the catalog. */

void
put_file (char *name)
{
	file_t *f;
	int found;
	int i;
	int fd;
	int status;
	off_t filepos;

	/* Check if we got the file already. */
	f = get_file (name);

	/* If found there is not much to do, otherwise add it. */
	if (f == NULL)
	{

		/* Print message. */
		if (verbose)
		{
			printf ("Registering %s\n", name);
		}

		/* Find first empty slot. */
		found = false;
		for (i=0; i<FILES_MAX; i++)
		{
			f = files[i];
			if (f == NULL)
			{

				/* Unallocated fresh. */
				files[i] = new (file_t);
				f = files[i];
				found = true;
				break;
			}
			if (f->sn == -1)
			{

				/* Used before but free. */
				found = true;
				break;
			}
		}
		if (found == false)
		{

			/* No empty slots. */
			error ("File catalog is full");
		}

		/* Fill new slot. */
		f->sn = i;
		f->name = strdup (name);

		/* Open file. */
		fd = open (name, O_RDONLY);
		if (fd == -1)
		{
			fprintf (stderr, "Error opening %s\n", name);
			perror ("Error context");
			error ("Cannot open file");
		}
		f->fd = fd;

		/* Create  and fill last and current status blocks. */
		f->laststatbuf = new (struct stat);
		(void) memset (f->laststatbuf, 0, sizeof (struct stat));
		f->statbuf = new (struct stat);
		(void) memset (f->statbuf, 0, sizeof (struct stat));
		status = fstat (fd, f->statbuf);
		if (status == -1)
		{
			fprintf (stderr, "Error calling stat for %s\n", name);
			perror ("Error context");
			error ("Cannot stat file");
		}
		(void) memcpy (f->laststatbuf, f->statbuf, sizeof (struct stat));

		/* Get last and current modification dates. */
		f->lastmodified = f->statbuf->st_mtime;
		f->modified = f->lastmodified;

		/* Get last and current end of file offset. */
		filepos = lseek (fd, (off_t) 0, SEEK_END);
		if (filepos == (off_t) -1)
		{
			fprintf (stderr, "Error seeking %s\n", f->name);
			perror ("Error context");
			error ("Cannot seek");
		}
		f->lastendpos = filepos;
		f->endpos = f->lastendpos;

		/* Close file. */
		status = close (fd);
		if (status == -1)
		{
			fprintf (stderr, "Error closing %s\n", f->name);
			perror ("Error context");
			error ("Cannot close");
		}
		f->fd = -1;
	}
	nfiles++;
}

/* Remove file entry from the catalog. */

void
remove_entry (int n)
{
	file_t *f;

	/* File is by the number. */
	f = files[n];

	if (f != NULL)
	{

		/* Mark it as removed. */
		f->sn = -1;

		/* Free allocated objects. */
		if (f->name != NULL)
		{
			free (f->name);
			f->name = NULL;
		}
		if (f->fd != -1)
		{
			(void) close (f->fd);
			f->fd = -1;
		}
		if (f->statbuf != NULL)
		{
			free (f->statbuf);
			f->statbuf = NULL;
		}
		if (f->laststatbuf != NULL)
		{
			free (f->laststatbuf);
			f->laststatbuf = NULL;
		}

		/* Initialize. */
		f->lastmodified = (time_t) 0;
		f->modified = (time_t) 0;
		f->lastendpos = (off_t) 0;
		f->endpos = (off_t) 0;
	}
	nfiles--;
}

/* Check if file changed. */

int
check_file (file_t *f)
{
	int fd;
	off_t filepos;
	int status;

	/* Open file. */
	fd = open (f->name, O_RDONLY);
	if (fd == -1)
	{
		if (fd = ENOENT)
		{

			/* The file was removed in the meantime and the entry should
			   be removed as well. */
			if (verbose)
			{
				printf ("Deregister %s\n", f->name);
			}
			remove_entry (f->sn);

			/* Quit here prematurely, there is nothing more to do. */
			return (false);
		}

		/* Just some other error. */
		fprintf (stderr, "Error opening %s\n", f->name);
		perror ("Error context");
		error ("Cannot open file");
	}
	f->fd = fd;

	/* Save old status block and get a new one. */
	(void) memcpy (f->laststatbuf, f->statbuf, sizeof (struct stat));
	status = fstat (fd, f->statbuf);
	if (status == -1)
	{
		fprintf (stderr, "Error calling stat for %s\n", f->name);
		perror ("Error context");
		error ("Cannot stat file");
	}

	/* Save old modified time and get current. */
	f->lastmodified = f->modified;
	f->modified = f->statbuf->st_mtime;

	/* Get last and current end of file offset. */
	filepos = lseek (fd, (off_t) 0, SEEK_END);
	if (filepos == (off_t) -1)
	{
		error ("Cannot seek");
	}
	f->lastendpos = f->endpos;
	f->endpos = filepos;

	/* Close file. */
	status = close (fd);
	if (status == -1)
	{
		error ("Cannot close");
	}
	f->fd = -1;

	/* Report if changed. */
	return ((f->lastmodified != f->modified) || (f->lastendpos != f->endpos));
}

/* Remove newline and non-printable characters from the buffer. */

void
printable (long nbytes, char *buffer)
{
	char *p;
	long c;

	p = buffer;
	c = 0;
	while (c < nbytes)
	{
		if (*p == NL)
		{
			*p = NLPRINT;
		}
		if (! isprint (*p))
		{
			*p = NONPRINT;
		}
		p++;
		c++;
	}
}

/* Forward buffer content to syslog. */

void
forward (char *name, long nbytes, char *buffer)
{
	int i;
	char line[LINELENGTH_MAX+1];
	char *from;
	char *to;
	unsigned long count;
	char prefixname[PATH_MAX+1];
	char *syslogprefix;
	char syslogline[LINELENGTH_MAX+2+1];
	unsigned long sysloglinelength;

	/* Check. */
	if (nbytes > LINELENGTH_MAX)
	{

		/* Line would be too long. */
		fprintf (stderr, "File changed is %s\n", name);
		fprintf (stderr, "Number of bytes to forward is %lu\n", nbytes);
		fprintf (stderr, "First %d characters are shown\n", LONG_LINE);
		fprintf (stderr, "Line would be too long\n");

		/* Tried to show the beginning of the long line but not possible. */
		if (LONG_LINE > LINELENGTH_MAX)
		{
			error ("Sample of long line to show is too long - confused");
		}

		/* Copy the beginning of the long line to show. */
		(void) memcpy (line, buffer, (size_t) LONG_LINE);

		/* Remove non-printable characters since these cases tend to
		   be binary files included by mistake. */
		printable (LONG_LINE, line);
		line[LONG_LINE] = EOS;

		/* Show the line and quit. */
		printf ("Buffer starts like '%s'\n", line);
		return;
	}

	/* Looks fine, copy and forward line by line. */
	from = buffer;
	to = line;
	*to = EOS;
	count = 0;
	for (i=0; i<nbytes; i++)
	{

		/* Paranoid check. */
		if (count > (unsigned long) LINELENGTH_MAX)
		{
			fprintf (stderr, "Line length is %lu\n", count);
			error ("Line too long");
		}

		/* Line terminated, forward. */
		if (*from == NL)
		{

			/* Put end of string at the end. */
			*to = EOS;

			/* Forward the line. Prefix with file name. */
			prefixname[PATH_MAX] = EOS;
			(void) strncpy (prefixname, name, PATH_MAX);
			syslogprefix = basename (prefixname);
			sysloglinelength = strlen (syslogprefix) + strlen (line) + 2;
			if (sysloglinelength > LINELENGTH_MAX)
			{
				fprintf (stderr, "Syslog line length is %lu\n", count);
				error ("Line way too long");
			}
			syslogline[0] = EOS;
			(void) strcat (syslogline, syslogprefix);
			(void) strcat (syslogline, ": ");
			(void) strcat (syslogline, line);
			if (verbose)
			{
				printf ("%s\n", syslogline);
			}
			syslog (LOG_INFO, "%s", syslogline);

			/* Back to beginning of line. */
			to = line;
			from++;
		}
		else
		{

			/* Just copy. */
			*to = *from;
			to++;
			from++;
		}
		count++;
	}
}

/* Print file additions. */

void
print_file_change (file_t *f)
{
	int fd;
	off_t offset;
	char *buffer;
	size_t buflen;
	ssize_t nbytes;
	int status;

	/* Locate to last position. */
	fd = open (f->name, O_RDONLY);
	if (fd == -1)
	{
		fprintf (stderr, "Error opening %s\n", f->name);
		perror ("Error context");
		error ("Cannot open file to print changes");
	}
	offset = lseek (fd, f->lastendpos, SEEK_SET);
	if (offset == (off_t) -1)
	{
		fprintf (stderr, "Error positioning %s\n", f->name);
		perror ("Error context");
		error ("Cannot seek file to print changes");
	}

	/* Paranoid check. */
	if (f->lastendpos > f->endpos)
	{
		fprintf (stderr, "Problem with %s\n", f->name);
		fprintf (stderr, "Last position was %lu\n",
			(unsigned long) f->lastendpos);
		fprintf (stderr, "Current position is %lu\n",
			(unsigned long) f->endpos);
		fprintf (stderr, "File had contracted\n");
		return;
	}

	/* Number of bytes added. */
	buflen = (size_t) (f->endpos - f->lastendpos);

	/* Nothing to print. */
	if (buflen == (size_t) 0)
	{
		return;
	}

	/* Too much change. */
	if (buflen >= BUFLEN_MAX)
	{
		fprintf (stderr, "Problem with %s\n", f->name);
		fprintf (stderr, "Last position was %lu\n",
			(unsigned long) f->lastendpos);
		fprintf (stderr, "Current position is %lu\n",
			(unsigned long) f->endpos);
		fprintf (stderr, "Large amounts of data added to the file\n");
		return;
	}

	/* Read into the buffer. */
	buffer = (char *) allocate (buflen);
	(void) memset (buffer, 0, buflen);
	nbytes = read (fd, buffer, buflen);
	if (nbytes == (ssize_t) -1)
	{
		fprintf (stderr, "Error reading %s\n", f->name);
		perror ("Error context");
		error ("Cannot read from file to print changes");
	}
	if (nbytes != (ssize_t) buflen)
	{
		fprintf (stderr, "Error reading %s - partial read\n", f->name);
		perror ("Error context");
		error ("Partial read from file to print changes");
	}

	/* Forward buffer content to syslog. */
	forward (f->name, (long) nbytes, buffer);

	/* Finish. */
	free (buffer);
	status = close (fd);
	if (status == -1)
	{
		fprintf (stderr, "Error closing %s\n", f->name);
		perror ("Error context");
		error ("Cannot close");
	}
	f->fd = -1;
}

/* Insert matching file into the global file table. */

void
insert_matching (char *pattern, char *exclude, char *filename)
{
	if ((pattern == NULL) || (exclude == NULL))
	{
		printf ("Excluded %s on null pattern\n", filename);
		return;
	}
	if (match (pattern, filename))
	{
		if (strcmp (exclude, "") != 0)
		{
			if (match (exclude, filename))
			{
				if (verbose)
				{
					printf ("Excluded %s with %s\n", filename, exclude);
				}
			}
			else
			{
				if (verbose)
				{
					printf ("Matched %s with %s\n", filename, pattern);
				}
				put_file (filename);
			}
		}
		else
		{
				if (verbose)
				{
					printf ("Matched %s with %s no exclude\n",
						filename, pattern);
				}
				put_file (filename);
		}
	}
}

/* Scan directory tree. */

void
scan (char *pattern, char *exclude, char *path)
{
	extern int errno;
	DIR *d;
	struct dirent *e;
	char *current;
	char filename[PATH_MAX+1];
	int dotty;
	int status;

	/* Error would be returned here. */
	errno = 0;

	/* Check. */
	if (! directory (path))
	{
		error ("Not a directory");
	}

	/* Open directory. */
	d = opendir (path);
	if (d == NULL)
	{
		(void) fprintf (stderr, "Error opening directory %s\n", path);
		error ("Cannot open directory");
	}

	/* Read directory entries. */
	e = readdir (d);
	if (e == NULL && errno != 0)
	{
		error ("Error scanning directory");
	}
	while (e != NULL)
	{

		/* Processing one entry. Create a copy of the name. */
		current = strdup (e->d_name);
		*filename = EOS;
		(void) strcat (filename, path);
		(void) strcat (filename, "/");
		(void) strcat (filename, current);

		/* Create file entry if it is a real file. */
		if (realname (filename))
		{
			if (! directory (filename))
			{
				insert_matching (pattern, exclude, filename);
			}
		}

		/* Descend if it is a real directory. */
		if (directory (filename))
		{
			dotty = (strcmp (current, ".") == 0) ||
				(strcmp (current, "..") == 0);
			if (! dotty)
			{
				scan (pattern, exclude, filename);
			}
		}

		free (current);

		/* Get next entry. */
		e = readdir (d);
		if (e == NULL && errno != 0)
		{
			error ("Error scanning directory");
		}
	}

	/* List exhausted, finish. */
	status = closedir (d);
	if (status != 0)
	{
		error ("Error closing directory");
	}
}

/* Scan directory tree or insert single file. */

void
put_entry (char *pattern, char *exclude, char *name)
{
	if (directory (name))
	{
		if (verbose)
		{
			printf ("Scanning %s\n", name);
		}
		scan (pattern, exclude, name);
	}
	else
	{
		insert_matching (pattern, exclude, name);
	}
}

/* Check file catalog. */

void
check_catalog (void)
{
	int i;

	for (i=0; i<FILES_MAX; i++)
	{
		if (files[i] != NULL)
		{
			if (files[i]->sn != -1)
			{
				if (check_file (files[i]))
				{
					if (verbose)
					{
						printf ("Changed %s\n", files[i]->name);
					}
					print_file_change (files[i]);
				}
			}
		}
	}
}

/* Signal handler for HUP. */

void
handlehup (int sig)
{
	fprintf (stderr, "Signal SIGHUP received and ignored\n");
}

/* Signal handler for USR1. */

void
handleusr1 (int sig)
{

	/* Print configuration. */
	print_configuration ();
	print_catalog ();
	(void) fsync (fileno (stdout));
	(void) sleep ((unsigned int) 1);
}

/* Remove all entries. */

void
remove_all_entries ()
{
	int i;
	file_t *f;

	for (i=0; i<FILES_MAX; i++)
	{
		remove_entry (i);
	}
}

/* Signal handler for exit. */

void
handleexit (int sig)
{

	/* SIGHUP or SIGQUIT received. */
	fprintf (stderr, "Signal received shutting down\n");

	/* Remove all entries in the file table. */
	remove_all_entries ();

	/* Make syslog entry. */
	syslog (LOG_INFO, "%s", "Closing log and shutting down");

	/* Close syslog facility. */
	closelog ();

	/* Finish here. */
	exit (SUCCESS);
}

/* Daemonize. */

void
daemonize (void)
{
	int status;
	extern int errno;
	char *logcopy;
	char *logdir;
	struct stat statbuf;
	uid_t uid;
	char *username;
	struct passwd *p;
	pid_t pid;
	pid_t sid;
	mode_t mask;
	int fd;

	/* Check if we own the log file directory, for security reasons. */
	logcopy = strdup (logfilename);
	logdir = dirname (logcopy);
	errno = 0;
	status = mkdir (logdir, (mode_t) 0755);
	if (status == -1)
	{
		if (errno == EEXIST)
		{
			if (verbose)
			{
				printf ("%s is already there continuing\n", logdir);
			}
		}
		else
		{
			fprintf (stderr, "Error when trying to create %s\n", logdir);
			fprintf (stderr, "Value of errno is %d\n", errno);
			perror ("Error context");
			error ("Error creating log directory");
		}
	}
	errno = 0;
	status = stat (logdir, &statbuf);
	if (status == -1)
	{
		if (errno == ENOENT)
		{
			fprintf (stderr, "Log directory should be %s\n", logdir);
			fprintf (stderr, "Please create log directory\n");
			fprintf (stderr, "Make sure it is owned by yourself\n");
			error ("Cannot find log directory");
		}
		else
		{
			fprintf (stderr, "Log directory is %s\n", logdir);
			error ("Error trying to get stat information for log directory");
		}
	}
	uid = statbuf.st_uid;
	if (statbuf.st_uid != getuid())
	{

		/* Directory is not owned by us. */
		p = getpwuid (uid);
		if (p == NULL)
		{
			fprintf (stderr, "Error looking up username for %lu\n",
				(unsigned long) uid);
			fprintf (stderr, "Strange thing about log directory %s\n",  logdir);
			error ("Error calling getpwuid");
		}
		fprintf (stderr, "Log directory is %s\n", logdir);
		fprintf (stderr, "Owned by user %s uid %lu\n",
			p->pw_name, (unsigned long) uid);
		error ("Log directory not owned by process");
	}

	/* Fork, exit parent and get session ID. */
	pid = fork ();
	if (pid < 0)
	{
		error ("Cannot fork");
	}
	if (pid > 0)
	{
		exit (SUCCESS);
	}
	sid = setsid ();
	if (sid < 0)
	{
		error ("Cannot setsid");
	}

	/* Set umask. */
	mask = umask ((mode_t) 022);

	/* Change default directory as part of daemonizing process. */
	status = chdir (logdir);
	if (status == -1)
	{
		error ("Cannot chdir to log directory");
	}

	/* Clean up. */
	free (logcopy);
	logdir = NULL;

	/* Close files we know are open. */
	status = close (fileno (stdin));
	if (status == -1)
	{
		error ("Error closing stdin");
	}
	status = close (fileno (stdout));
	if (status == -1)
	{
		error ("Error closing stdout");
	}
	status = close (fileno (stderr));
	if (status == -1)
	{
		error ("Error closing stderr");
	}

	/* Open log file, duplicate std descriptors pointing to it. */
	fd = open (logfilename, O_RDWR|O_CREAT|O_APPEND, (mode_t) 0644);
	if (fd == -1)
	{
		error ("Error opening log file");
	}

	/* Duplicate log file descriptor and check the file ids, must be 0, 1, 2. */
	if (fd != 0)
	{
		error ("Wrong file descriptor number for stdin");
	}
	fd = dup (fd);
	if (fd != 1)
	{
		error ("Wrong file descriptor number for stdout");
	}
	fd = dup (fd);
	if (fd != 2)
	{
		error ("Wrong file descriptor number for stderr");
	}

	/* Ignore child and terminal signals. */
	(void) signal (SIGCHLD, SIG_IGN);
	(void) signal (SIGSTOP, SIG_IGN);
	(void) signal (SIGTTOU, SIG_IGN);
	(void) signal (SIGTTIN, SIG_IGN);

	/* Setup for hangup and term signals. */
	(void) signal (SIGHUP, handlehup);
	(void) signal (SIGTERM, handleexit);
	(void) signal (SIGQUIT, handleexit);
	(void) signal (SIGUSR1, handleusr1);
}

/* Build file table. */

void
build_table (char *cwd, int argc, char *argv[])
{
	char *pattern;
	char *exclude;
	time_t starttime;
	time_t endtime;
	time_t elapsed;
	int i;
	char *name;
	int found_entry;
	char absolute[PATH_MAX+1];

	/* The default pattern matches all. */
	pattern = DEFAULT_PATTERN;

	/* Not excluding by default. */
	exclude = "";

	/* Number of files inserted. */
	nfiles = 0;

	/* Mark start. */
	(void) time (&starttime);

	/* Process the args. */
	found_entry = false;
	for (i=1; i<argc; i++)
	{
		name = argv[i];

		/* Switch arguments are invalidated. */
		if (name != NULL)
		{

			/* Argv is valid. */
			if (eqs (name, "-p") || eqs (name, "--pattern"))
			{

				/* It is a pattern it will apply to all following names. */
				i++;
				pattern = argv[i];
			}
			else if (eqs (name, "-x") || eqs (name, "--exclude"))
			{

				/* It is an exlude  pattern and it will apply to all
				   following names. */
				i++;
				exclude = argv[i];
			}
			else
			{

				/* Name is interpreted as a file or directory name.
				   Use absolute path name if not already absolute. */
				*absolute = EOS;
				if (*name == '/')
				{

					/* Absolute already. */
					(void) strcat (absolute, name);
				}
				else
				{
					(void) strcat (absolute, cwd);
					(void) strcat (absolute, "/");
					(void) strcat (absolute, name);
				}

				/* Remember that we got at least one entry and add the entry. */
				found_entry = true;
				put_entry (pattern, exclude, absolute);
			}
		}
	}

	/* Check. */
	if (! found_entry)
	{
		error ("No files selected to watch, exiting");
	}

	/* Finish. */
	if (verbose)
	{
		printf ("Watching %d files\n", nfiles);
		(void) time (&endtime);
		elapsed = endtime - starttime;
		printf ("Scan took %d second(s)\n", (int) elapsed);
	}
}

/* Print help. */

void
print_help (void)
{
	printf ("\
This program runs in the background as a daemon and\n\
watches files. Forwards lines as they are appended to\n\
these files to the syslog facility.\n\
Usage:\n\
    logforw [-v][-d][-s delay][-l logfile] [-p pattern][-x pattern] name...\n\
        [[-p pattern][-x pattern] name...]\n\
where\n\
    -v          to print verbose messages\n\
    -d          debug mode, do not daemonize, run in the foreground\n\
    -s seconds  sleep delay in seconds\n\
    -f facility is the facility code to use with syslog\n\
    -l logfile  log file to use, the default is\n\
                /var/tmp/logforw/logforw.log.\n\
                The directory for the log files needs to be created\n\
                beforehand.\n\
                It should be owned by userid the daemon is running under.\n\
    -p pattern  is a pattern to match against the file names.\n\
    -x pattern  is a pattern to exclude files.\n\
    name        is the name of a file or a directory. If a directory is\n\
                specified it will be scanned for all files matching the\n\
                pattern.\n\
                You can specifiy the pattern repeatedly between the names,\n\
                always the last one will be effective.\n\
");
	exit (FAILURE);
}

/* Main program. */

int
main (int argc, char *argv[])
{
	int i;
	char *arg;
	char *pattern;
	char *exclude;
	char cwdbuf[PATH_MAX+1];
	char *cwd;

	/* Check, we'll need enough arguments. */
	if (argc <= 1)
	{
		print_help ();
	}

	/* Print help. */
	if (eqs (argv[1], "-h") || eqs (argv[1], "-help") ||
		eqs (argv[1], "--help"))
	{
		print_help ();
	}

	/* Process switches. Cannot use getopt since that rearranges argv. */
	delayseconds = SLEEP_DELAY;
	pattern = DEFAULT_PATTERN;
	exclude = NULL;
	for (i=1; i<argc; i++)
	{
		arg = argv[i];
		if (eqs (arg, "-d") || eqs (arg, "--debug"))
		{

			/* Debug, do not daemonize. */
			background = false;

			/* Invalidate the argument which is a switch. */
			argv[i] = NULL;
		}
		else if (eqs (arg, "-s") || eqs (arg, "--sleep"))
		{

			/* Sleep delay time in seconds. */
			delayseconds = atoi (argv[i+1]);
			if (delayseconds == 0)
			{
				error ("Value error for atoi");
			}

			/* Invalidate the argument which is a switch. */
			argv[i] = NULL;

			/* Invalidate the argument to the switch. */
			argv[i+1] = NULL;

			/* Move to the next. */
			i++;
		}
		else if (eqs (arg, "-f") || eqs (arg, "--facility"))
		{

			/* Get log file name. */
			facility = argv[i+1];

			/* Invalidate the argument which is a switch. */
			argv[i] = NULL;

			/* Also the next one which is the facilty name. */
			argv[i+1] = NULL;

			/* Move to the next. */
			i++;
		}
		else if (eqs (arg, "-l") || eqs (arg, "--logfilename"))
		{

			/* Get log file name. */
			logfilename = argv[i+1];

			/* Invalidate the argument which is a switch. */
			argv[i] = NULL;

			/* Also the next one which is the log file name. */
			argv[i+1] = NULL;

			/* Move to the next. */
			i++;
		}
		else if (eqs (arg, "-v") || eqs (arg, "--verbose"))
		{

			/* Verbose messages. */
			verbose = true;

			/* Invalidate the argument which is a switch. */
			argv[i] = NULL;
		}
	}

	/* Print config and arguments if asked. */
	if (verbose)
	{
		print_configuration ();
		for (i=1; i<argc; i++)
		{
			arg = argv[i];
			if (arg != NULL)
			{
				if (eqs (arg, "-p") || eqs (arg, "--pattern"))
				{
					i++;
					pattern = argv[i];
					printf ("Pattern %s\n", pattern);
				}
				if (eqs (arg, "-x") || eqs (arg, "--exclude"))
				{
					i++;
					exclude = argv[i];
					printf ("Exclude %s\n", exclude);
				}
				if (! *arg == '-')
				{
					printf ("Argument %s with pattern %s", arg, pattern);
					if (exclude != NULL)
					{
						printf (" excluding %s", exclude);
					}
					printf ("\n");
				}
			}
		}
	}

	/* Filename arguments should be relative to this path.
	   We need this before daemonizing. */
	cwd = getcwd (cwdbuf, (size_t) PATH_MAX);
	if (cwd == NULL)
	{
		error ("Cannot obtain current working directory path");
	}

	/* Daemonize. */
	if (background)
	{
		if (verbose)
		{
			printf ("Going to background\n");
		}
		daemonize ();
	}

	/* Initialization. */
	if (verbose)
	{
		printf ("Starting up\n");
	}
	init_file ();

	/* Prepare for logging. */
	openlog (facility, (int) 0, LOG_LOCAL7);

	/* Make syslog entry about startup. */
	syslog (LOG_INFO, "%s", "Starting up");
	delay (delayseconds);

	/* Build table with files. */
	if (verbose)
	{
		printf ("Building file table\n");
	}
	build_table (cwd, argc, argv);

	/* Main cycle. */
	while (true)
	{

		/* Check catalog. */
		check_catalog ();

		/* Rescan files. */
		build_table (cwd, argc, argv);

		/* Wait. */
		delay (delayseconds);
	}

	/* Finish. Never called. */
	exit (SUCCESS);
}

/* End of file LOGFORW.C */

