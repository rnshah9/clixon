/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2016 Olof Hagsand and Benny Holmgren
  Copyright (C) 2017-2019 Olof Hagsand
  Copyright (C) 2020-2021 Olof Hagsand and Rubicon Communications, LLC(Netgate)

  This file is part of CLIXON.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  Alternatively, the contents of this file may be used under the terms of
  the GNU General Public License Version 3 or later (the "GPL"),
  in which case the provisions of the GPL are applicable instead
  of those above. If you wish to allow use of your version of this file only
  under the terms of the GPL, and not to allow others to
  use your version of this file under the terms of Apache License version 2, 
  indicate your decision by deleting the provisions above and replace them with
  the  notice and other provisions required by the GPL. If you do not delete
  the provisions above, a recipient may use your version of this file under
  the terms of any one of the Apache License version 2 or the GPL.

  ***** END LICENSE BLOCK *****

 *
 * Regular logging and debugging. Syslog using levels.
 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clixon_err.h"
#include "clixon_log.h"

/* The global debug level. 0 means no debug 
 * @note There are pros and cons in having the debug state as a global variable. The 
 * alternative to bind it to the clicon handle (h) was considered but it limits its
 * usefulness, since not all functions have h
 */
static int _clixon_debug = 0;

/* Bitmask whether to log to syslog or stderr: CLICON_LOG_STDERR | CLICON_LOG_SYSLOG */
static int _logflags = 0x0;

/* Set to open file to write debug messages directly to file */
static FILE *_logfile = NULL;

/*! Initialize system logger.
 *
 * Make syslog(3) calls with specified ident and gates calls of level upto specified level (upto).
 * May also print to stderr, if err is set.
 * Applies to clicon_err() and clicon_debug too
 *
 * @param[in]  ident   prefix that appears on syslog (eg 'cli')
 * @param[in]  upto    log priority, eg LOG_DEBUG,LOG_INFO,...,LOG_EMERG (see syslog(3)).
 * @param[in]  flags   bitmask: if CLICON_LOG_STDERR, then print logs to stderr
 *                              if CLICON_LOG_SYSLOG, then print logs to syslog
 *				You can do a combination of both
 * @code
 *  clicon_log_init(__PROGRAM__, LOG_INFO, CLICON_LOG_STDERR); 
 * @endcode
 */
int
clicon_log_init(char         *ident, 
		int           upto, 
		int           flags)
{
    _logflags = flags;
    if (flags & CLICON_LOG_SYSLOG){
	if (setlogmask(LOG_UPTO(upto)) < 0)
	    /* Cant syslog here */
	    fprintf(stderr, "%s: setlogmask: %s\n", __FUNCTION__, strerror(errno)); 
	openlog(ident, LOG_PID, LOG_USER); /* LOG_PUSER is achieved by direct stderr logs in clicon_log */
    }
    return 0;
}

int
clicon_log_exit(void)
{
    if (_logfile)
	fclose(_logfile);
    closelog(); /* optional */
    return 0;
}

/*! Utility function to set log destination/flag using command-line option
 * @param[in]  c  Log option,one of s,f,e,o
 * @retval    -1  No match
 * @retval     0  One of CLICON_LOG_SYSLOG|STDERR|STDOUT|FILE
 */
int
clicon_log_opt(char c)
{
    int logdst = -1;

    switch (c){
    case 's':
	logdst = CLICON_LOG_SYSLOG;
	break;
    case 'e':
	logdst = CLICON_LOG_STDERR;
	break;
    case 'o':
	logdst = CLICON_LOG_STDOUT;
	break;
    case 'f':
	logdst = CLICON_LOG_FILE;
	break;
    default:
	break;
    } 
    return logdst;
}

/*! If log flags include CLICON_LOG_FILE, set the file 
 * @param[in]   filename   File to log to
 * @retval      0          OK
 * @retval     -1          Error
 * @see clicon_debug_init where a strean
 */
int
clicon_log_file(char *filename)
{
    if (_logfile)
	fclose(_logfile);
    if ((_logfile = fopen(filename, "a")) == NULL){
	fprintf(stderr, "fopen: %s\n", strerror(errno)); /* dont use clicon_err here due to recursion */
	return -1;
    }
    return 0;
}

int
clicon_get_logflags(void)
{
    return _logflags;
}

/*! Mimic syslog and print a time on file f
 */
static int
flogtime(FILE *f)
{
    struct timeval tv;
    struct tm tm;

    gettimeofday(&tv, NULL);
    localtime_r((time_t*)&tv.tv_sec, &tm);
    fprintf(f, "%s %2d %02d:%02d:%02d: ", 
	    mon2name(tm.tm_mon), tm.tm_mday,
	    tm.tm_hour, tm.tm_min, tm.tm_sec);
    return 0;
}

#ifdef NOTUSED
/*
 * Mimic syslog and print a time on string s
 * String returned needs to be freed.
 */
static char *
slogtime(void)
{
    struct timeval tv;
    struct tm     *tm;
    char           *str;

    /* Example: "Apr 14 11:30:52: " len=17+1 */
    if ((str = malloc(18)) == NULL){
	fprintf(stderr, "%s: malloc: %s\n", __FUNCTION__, strerror(errno));
	return NULL;
    }
    gettimeofday(&tv, NULL);
    tm = localtime((time_t*)&tv.tv_sec);
    snprintf(str, 18, "%s %2d %02d:%02d:%02d: ", 
	     mon2name(tm->tm_mon), tm->tm_mday,
	     tm->tm_hour, tm->tm_min, tm->tm_sec);
    return str;
}
#endif

/*! Make a logging call to syslog (or stderr).
 *
 * @param[in]   level log level, eg LOG_DEBUG,LOG_INFO,...,LOG_EMERG. Thisis OR:d with facility == LOG_USER
 * @param[in]   msg   Message to print as argv.
 * This is the _only_ place the actual syslog (or stderr) logging is made in clicon,..
 * @note syslog makes its own filtering, but if log to stderr we do it here
 * @see  clicon_debug
 */
static int
clicon_log_str(int           level, 
	       char         *msg)
{
    if (_logflags & CLICON_LOG_SYSLOG)
	syslog(LOG_MAKEPRI(LOG_USER, level), "%s", msg);
   /* syslog makes own filtering, we do it here:
    * if normal (not debug) then filter loglevels >= debug
    */
    if (_clixon_debug == 0 && level >= LOG_DEBUG)
	goto done;
    if (_logflags & CLICON_LOG_STDERR){
	flogtime(stderr);
	fprintf(stderr, "%s\n", msg);
    }
    if (_logflags & CLICON_LOG_STDOUT){
	flogtime(stdout);
	fprintf(stdout, "%s\n", msg);
    }
    if ((_logflags & CLICON_LOG_FILE) && _logfile){
	flogtime(_logfile);
	fprintf(_logfile, "%s\n", msg);
	fflush(_logfile);
    }
    /* Enable this if you want syslog in a stream. But there are problems with 
     * recursion
     */
 done:
    return 0;
}

/*! Make a logging call to syslog using variable arg syntax.
 *
 * @param[in]   level    log level, eg LOG_DEBUG,LOG_INFO,...,LOG_EMERG. This 
 *                       is OR:d with facility == LOG_USER
 * @param[in]   format   Message to print as argv.
 * @code
	clicon_log(LOG_NOTICE, "%s: dump to dtd not supported", __PROGRAM__);
 * @endcode
 * @see clicon_log_init and clicon_log_str
 */
int
clicon_log(int         level, 
	   const char *format, ...)
{
    va_list args;
    int     len;
    char   *msg    = NULL;
    int     retval = -1;

    /* first round: compute length of debug message */
    va_start(args, format);
    len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    /* allocate a message string exactly fitting the message length */
    if ((msg = malloc(len+1)) == NULL){
	fprintf(stderr, "malloc: %s\n", strerror(errno)); /* dont use clicon_err here due to recursion */
	goto done;
    }

    /* second round: compute write message from format and args */
    va_start(args, format);
    if (vsnprintf(msg, len+1, format, args) < 0){
	va_end(args);
	fprintf(stderr, "vsnprintf: %s\n", strerror(errno)); /* dont use clicon_err here due to recursion */
	goto done;
    }
    va_end(args);
    /* Actually log it */
    clicon_log_str(level, msg);

    retval = 0;
  done:
    if (msg)
	free(msg);
    return retval;
}

/*! Initialize debug messages. Set debug level.
 *
 * Initialize debug module. The level is used together with clicon_debug(dbglevel) calls as follows: 
 * print message if level >= dbglevel.
 * Example: clicon_debug_init(1) -> debug(1) is printed, but not debug(2).
 * Normally, debug messages are sent to clicon_log() which in turn can be sent to syslog and/or stderr.
 * But you can also override this with a specific debug file so that debug messages are written on the file
 * independently of log or errors. This is to ensure that a syslog of normal logs is unpolluted by extensive
 * debugging.
 *
 * @param[in] dbglevel  0 is show no debug messages, 1 is normal, 2.. is high debug. 
 *                      Note this is _not_ level from syslog(3)
 * @param[in] f         Debug-file. Open file where debug messages are directed. 
 *                      If not NULL, it overrides the clicon_log settings which is otherwise
 *			where debug messages are directed.
 * @see clicon_log_file where a filename can be given
 */
int
clicon_debug_init(int   dbglevel,
		  FILE *f)
{
    _clixon_debug = dbglevel; /* Global variable */
    
    if (f != NULL){
	if (_logfile)
	    fclose(_logfile);
	_logfile = f;
    }
    return 0;
}

int
clicon_debug_get(void)
{
    return _clixon_debug;
}

/*! Print a debug message with debug-level. Settings determine where msg appears.
 *
 * If the dbglevel passed in the function is equal to or lower than the one set by 
 * clicon_debug_init(level).  That is, only print debug messages <= than what you want:
 *      print message if level >= dbglevel.
 * The message is sent to clicon_log. EIther to syslog, stderr or both, depending on 
 * clicon_log_init() setting
 * 
 * @param[in] dbglevel   0 always called (dont do this: not really a dbg message)
 *                       1 default level if passed -D
 *                       2.. Higher debug levels
 * @param[in] format     Message to print as argv.
 */
int
clicon_debug(int         dbglevel, 
	     const char *format, ...)
{
    va_list args;
    int     len;
    char   *msg    = NULL;
    int     retval = -1;

    if (dbglevel > _clixon_debug) /* compare debug mask with global variable */
	return 0;
    /* first round: compute length of debug message */
    va_start(args, format);
    len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    /* allocate a message string exactly fitting the messgae length */
    if ((msg = malloc(len+1)) == NULL){
	clicon_err(OE_UNIX, errno, "malloc");
	goto done;
    }
    /* second round: compute write message from format and args */
    va_start(args, format);
    if (vsnprintf(msg, len+1, format, args) < 0){
	va_end(args);
	clicon_err(OE_UNIX, errno, "vsnprintf");
	goto done;
    }
    va_end(args);
    clicon_log_str(LOG_DEBUG, msg);
    retval = 0;
  done:
    if (msg)
	free(msg);
    return retval;
}

/*! Translate month number (0..11) to a three letter month name
 * @param[in] md  month number, where 0 is january
 */
char *
mon2name(int md)
{
    switch(md){
    case 0: return "Jan";
    case 1: return "Feb";
    case 2: return "Mar";
    case 3: return "Apr";
    case 4: return "May";
    case 5: return "Jun";
    case 6: return "Jul";
    case 7: return "Aug";
    case 8: return "Sep";
    case 9: return "Oct";
    case 10: return "Nov";
    case 11: return "Dec";
    default:
	break;
    }
    return NULL;
}

