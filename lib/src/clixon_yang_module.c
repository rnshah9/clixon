/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2009-2018 Olof Hagsand and Benny Holmgren

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

 * Yang module and feature handling
 * @see https://tools.ietf.org/html/rfc7895
 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <regex.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <syslog.h>
#include <assert.h>
#include <sys/stat.h>
#include <netinet/in.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include "clixon_log.h"
#include "clixon_err.h"
#include "clixon_string.h"
#include "clixon_queue.h"
#include "clixon_hash.h"
#include "clixon_handle.h"
#include "clixon_file.h"
#include "clixon_yang.h"
#include "clixon_xml.h"
#include "clixon_options.h"
#include "clixon_netconf_lib.h"
#include "clixon_yang_module.h"

/*! Init the Yang module library
 *
 * Load RFC7895 yang spec, module-set-id, etc.
 * @param[in]     h       Clicon handle
 * @note CLIXON_DATADIR is hardcoded
 */
int
yang_modules_init(clicon_handle h)
{
    int retval = -1;
    yang_spec *yspec;

    yspec = clicon_dbspec_yang(h);	
    if (!clicon_option_bool(h, "CLICON_MODULE_LIBRARY_RFC7895"))
	goto ok;
    /* Ensure module-set-id is set */
    if (!clicon_option_exists(h, "CLICON_MODULE_SET_ID")){
	clicon_err(OE_CFG, ENOENT, "CLICON_MODULE_SET_ID must be defined when CLICON_MODULE_LIBRARY_RFC7895 is enabled");
	goto done;
    }
    /* Ensure revision exists is set */
    if (yang_spec_parse_module(h, "ietf-yang-library", CLIXON_DATADIR, NULL, yspec)< 0)
	goto done;
    /* Find revision */
    if (yang_modules_revision(h) == NULL){
	clicon_err(OE_CFG, ENOENT, "Yang client library yang spec has no revision");
	goto done;
    }
 ok:
     retval = 0;
 done:
     return retval;
}

/*! Return RFC7895 revision (if parsed)
 * @param[in]    h        Clicon handle
 * @retval       revision String (dont free)
 * @retval       NULL     Error: RFC7895 not loaded or revision not found
 */
char *
yang_modules_revision(clicon_handle h)
{
    yang_spec *yspec;
    yang_stmt *ymod;
    yang_stmt *yrev;
    char      *revision = NULL;

    yspec = clicon_dbspec_yang(h);
    if ((ymod = yang_find((yang_node*)yspec, Y_MODULE, "ietf-yang-library")) != NULL){
	if ((yrev = yang_find((yang_node*)ymod, Y_REVISION, NULL)) != NULL){
	    revision = yrev->ys_argument;
	}
    }
    return revision;
}

/*! Get modules state according to RFC 7895
 * @param[in]     h       Clicon handle
 * @param[in]     yspec   Yang spec
 * @param[in,out] xret    Existing XML tree, merge x into this
 * @retval       -1       Error (fatal)
 * @retval        0       OK
 * @retval        1       Statedata callback failed
 * @notes NYI: schema, deviation
x      +--ro modules-state
x         +--ro module-set-id    string
x         +--ro module* [name revision]
x            +--ro name                yang:yang-identifier
x            +--ro revision            union
            +--ro schema?             inet:uri
x            +--ro namespace           inet:uri
            +--ro feature*            yang:yang-identifier
            +--ro deviation* [name revision]
            |  +--ro name        yang:yang-identifier
            |  +--ro revision    union
            +--ro conformance-type    enumeration
            +--ro submodule* [name revision]
               +--ro name        yang:yang-identifier
               +--ro revision    union
               +--ro schema?     inet:uri
 */
int
yang_modules_state_get(clicon_handle    h,
		       yang_spec       *yspec,
		       cxobj          **xret)
{
    int         retval = -1;
    cxobj      *x = NULL;
    cbuf       *cb = NULL;
    yang_stmt  *ylib = NULL; /* ietf-yang-library */
    yang_stmt  *yns = NULL;  /* namespace */
    yang_stmt  *ymod;        /* generic module */
    yang_stmt  *ys;
    yang_stmt  *yc;
    char       *module_set_id;
    char       *module = "ietf-yang-library";

    module_set_id = clicon_option_str(h, "CLICON_MODULE_SET_ID");
    if ((ylib = yang_find((yang_node*)yspec, Y_MODULE, module)) == NULL){
	clicon_err(OE_YANG, 0, "%s not found", module);
	goto done;
    }
    if ((yns = yang_find((yang_node*)ylib, Y_NAMESPACE, NULL)) == NULL){
	clicon_err(OE_YANG, 0, "%s yang namespace not found", module);
	goto done;
    }
    if ((cb = cbuf_new()) == NULL){
	clicon_err(OE_UNIX, 0, "clicon buffer");
	goto done;
    }
    cprintf(cb,"<modules-state xmlns=\"%s\">", yns->ys_argument);
    cprintf(cb,"<module-set-id>%s</module-set-id>", module_set_id); 
    
    ymod = NULL;
    while ((ymod = yn_each((yang_node*)yspec, ymod)) != NULL) {
	if (ymod->ys_keyword != Y_MODULE)
	    continue;
	cprintf(cb,"<module>");
	cprintf(cb,"<name>%s</name>", ymod->ys_argument);
	if ((ys = yang_find((yang_node*)ymod, Y_REVISION, NULL)) != NULL)
	    cprintf(cb,"<revision>%s</revision>", ys->ys_argument);
	else
	    cprintf(cb,"<revision></revision>");
	if ((ys = yang_find((yang_node*)ymod, Y_NAMESPACE, NULL)) != NULL)
	    cprintf(cb,"<namespace>%s</namespace>", ys->ys_argument);
	else
	    cprintf(cb,"<namespace></namespace>");
	yc = NULL;
	while ((yc = yn_each((yang_node*)ymod, yc)) != NULL) {
	    switch(yc->ys_keyword){
	    case Y_FEATURE:
		if (yc->ys_cv && cv_bool_get(yc->ys_cv))
		    cprintf(cb,"<feature>%s</feature>", yc->ys_argument);
		break;
	    case Y_SUBMODULE:
		cprintf(cb,"<submodule>");
		cprintf(cb,"<name>%s</name>", yc->ys_argument);
		if ((ys = yang_find((yang_node*)yc, Y_REVISION, NULL)) != NULL)
		    cprintf(cb,"<revision>%s</revision>", ys->ys_argument);
		else
		    cprintf(cb,"<revision></revision>");
		cprintf(cb,"</submodule>");
		break;
	    default:
		break;
	    }
	}
	cprintf(cb,"</module>"); 
    }
    cprintf(cb,"</modules-state>");

    if (xml_parse_string(cbuf_get(cb), yspec, &x) < 0){
	if (netconf_operation_failed_xml(xret, "protocol", clicon_err_reason)< 0)
	    goto done;
	retval = 1;
	goto done;
    }
    retval = netconf_trymerge(x, yspec, xret);
 done:
    if (cb)
	cbuf_free(cb);
    if (x)
	xml_free(x);
    return retval;
}
