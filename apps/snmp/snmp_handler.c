/*
 *
  ***** BEGIN LICENSE BLOCK *****
 
  Copyright (C) 2022 Olof Hagsand and Kristofer Hallin

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
 */

#ifdef HAVE_CONFIG_H
#include "clixon_config.h" /* generated by config & autoconf */
#endif

#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <syslog.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <assert.h>

/* net-snmp */
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

/* cligen */
#include <cligen/cligen.h>

/* clicon */
#include <clixon/clixon.h>

#include "snmp_lib.h"
#include "snmp_handler.h"

/*! SNMP table operation handlre

 * Callorder: 161,160,.... 0, 1,2,3, 160,161,...
 * see https://net-snmp.sourceforge.io/dev/agent/data_set_8c-example.html#_a0
 */
int
snmp_table_handler(netsnmp_mib_handler          *handler,
		   netsnmp_handler_registration *nhreg,
		   netsnmp_agent_request_info   *reqinfo,
		   netsnmp_request_info         *requests)
{
    int                     retval = SNMP_ERR_GENERR;
    clixon_snmp_handle     *sh;
    netsnmp_table_data_set *table;
    yang_stmt *ys;
    clicon_handle       h;
    yang_stmt              *ylist;
    cvec                   *nsc = NULL;
    cxobj                  *xt = NULL;
    cbuf                   *cb = NULL;

    clicon_debug(1, "%s %s %s", __FUNCTION__,
                 handler->handler_name,
                 snmp_msg_int2str(reqinfo->mode));
    sh = (clixon_snmp_handle*)nhreg->my_reg_void;
    ys = sh->sh_ys;
    h = sh->sh_h;
    table = sh->sh_table;

    if ((ylist = yang_find(ys, Y_LIST, NULL)) == NULL)
        goto ok;

    if (clixon_table_create(table, ys, h) < 0)
        goto done;

    switch(reqinfo->mode){
    case MODE_GETNEXT: // 160
        break;
    case MODE_GET: // 160
    case MODE_SET_RESERVE1:
    case MODE_SET_RESERVE2:
    case MODE_SET_ACTION:
    case MODE_SET_COMMIT:
        break;

    }
ok:
    retval = SNMP_ERR_NOERROR;

done:
    if (xt)
        xml_free(xt);
    if (cb)
        cbuf_free(cb);
    if (nsc)
        xml_nsctx_free(nsc);
    return retval;
}

/*! Scalar handler, set a value to clixon 
 * get xpath: see yang2api_path_fmt / api_path2xpath 
 */
static int
snmp_scalar_get(clicon_handle               h,
		yang_stmt                  *ys,
		netsnmp_variable_list      *requestvb,
		char                       *defaultval,
		enum cv_type                cvtype,
		netsnmp_agent_request_info *reqinfo,
		netsnmp_request_info       *requests)
{
    int    retval = -1;
    cvec  *nsc = NULL;
    char  *xpath = NULL;
    cxobj *xt = NULL;
    cxobj *xerr;
    cxobj *x;
    char  *valstr = NULL;
    u_char *snmpval = NULL;
    size_t  snmplen;
    int    ret;

    if (xml_nsctx_yang(ys, &nsc) < 0)
	goto done;
    if (yang2xpath(ys, &xpath) < 0)
	goto done;
    if (clicon_rpc_get(h, xpath, nsc, CONTENT_ALL, -1, &xt) < 0)
	goto done;
    if ((xerr = xpath_first(xt, NULL, "/rpc-error")) != NULL){
	clixon_netconf_error(xerr, "clicon_rpc_get", NULL);
	goto done;
    }
    /* Get value, either from xml, or smiv2 default */
    if ((x = xpath_first(xt, nsc, "%s", xpath)) != NULL) {
	valstr = xml_body(x);
    }
    else if ((valstr = defaultval) != NULL)
	;
    else{
	netsnmp_set_request_error(reqinfo, requests, SNMP_NOSUCHINSTANCE);
	goto ok;
    }
    if ((ret = type_yang2snmp(valstr, cvtype, reqinfo, requests, &snmpval, &snmplen)) < 0)
	goto done;
    if (ret == 0)
	goto ok;

    /* 1. use cligen object and get rwa buf / size from that, OR
     *    + have parse function from YANG
     *    - does not have 
     * 2. use union netsnmp_vardata and pass that here?
     * 3. Make cv2asn1 conversion function <--
     */
	
    /* see snmplib/snmp_client.c */
    if (snmp_set_var_value(requestvb,
			   snmpval,
			   snmplen) != 0){
	clicon_err(OE_SNMP, 0, "snmp_set_var_value");
	goto done;
    }
 ok:
    retval = 0;
 done:
    if (snmpval)
	free(snmpval);
    if (xt)
	xml_free(xt);
    if (xpath)
	free(xpath);
    if (nsc)
	xml_nsctx_free(nsc);
    return retval;
}

/*! Scalar handler, get a value from clixon 
 */
static int
snmp_scalar_set(clicon_handle               h,
		yang_stmt                  *ys,
		netsnmp_variable_list      *requestvb,
		netsnmp_agent_request_info *reqinfo,
		netsnmp_request_info       *requests)
{
    int        retval = -1;
    char      *api_path = NULL;
    cxobj     *xtop = NULL;
    cxobj     *xbot = NULL;
    cxobj     *xb;
    yang_stmt *yspec;
    int        ret;
    char      *valstr = NULL;
    cbuf      *cb = NULL;
    
    if ((yspec = clicon_dbspec_yang(h)) == NULL){
	clicon_err(OE_FATAL, 0, "No DB_SPEC");
	goto done;
    }
    if ((xtop = xml_new(NETCONF_INPUT_CONFIG, NULL, CX_ELMNT)) == NULL)
	goto done;
    if (yang2api_path_fmt(ys, 0, &api_path) < 0)
	goto done;
    if ((ret = api_path2xml(api_path, yspec, xtop, YC_DATANODE, 1, &xbot, NULL, NULL)) < 0)
	goto done;
	    
    if (ret == 0){
	clicon_err(OE_XML, 0, "api_path2xml %s invalid", api_path);
	goto done;
    }
    if ((xb = xml_new("body", xbot, CX_BODY)) == NULL)
	goto done; 
    if ((ret = type_snmp2yang(requestvb, reqinfo, requests, &valstr)) < 0)
	goto done;
    if (ret == 0)
	goto ok;
    if (xml_value_set(xb, valstr) < 0)
	goto done;
    if ((cb = cbuf_new()) == NULL){
	clicon_err(OE_UNIX, errno, "cbuf_new");
	goto done;
    }
    if (clicon_xml2cbuf(cb, xtop, 0, 0, -1) < 0)
	goto done;
    if (clicon_rpc_edit_config(h, "candidate", OP_MERGE, cbuf_get(cb)) < 0)
	goto done;
 ok:
    retval = 0;
 done:
    if (cb)
	cbuf_free(cb);
    if (xtop)
	xml_free(xtop);
    if (valstr)
	free(valstr);
    return retval;
}

/*! SNMP Scalar operation handler
 * Calls order: READ:160, 
 *              WRITE: 0, 1, 2, 3, 
 * MODE_SET_RESERVE1, MODE_SET_RESERVE2, MODE_SET_ACTION, MODE_SET_COMMIT
 *
 */
int
snmp_scalar_handler(netsnmp_mib_handler          *handler,
		    netsnmp_handler_registration *nhreg,
		    netsnmp_agent_request_info   *reqinfo,
		    netsnmp_request_info         *requests)
{
    int                    retval = -1;
    clixon_snmp_handle    *sh;
    yang_stmt             *ys;
    int                    asn1_type;
    netsnmp_variable_list *requestvb; /* sub of requests */
    enum cv_type           cvtype;
    
    /*
     * can be used to pass information on a per-pdu basis from a
     * helper to the later handlers 
     netsnmp_agent_request_info   *reqinfo,
     netsnmp_data_list *agent_data;
     netsnmp_free_agent_data_set()
    */
    requestvb = requests->requestvb;
    if (0)
	fprintf(stderr, "%s %s %s\n", __FUNCTION__,
		handler->handler_name,
		snmp_msg_int2str(reqinfo->mode)
		);

    if (0)
	fprintf(stderr, "inclusive:%d\n", 
		requests->inclusive
		);
    clicon_debug(1, "%s %s %s %d", __FUNCTION__,
		 handler->handler_name,
		 snmp_msg_int2str(reqinfo->mode),
		 requests->inclusive);
    sh = (clixon_snmp_handle*)nhreg->my_reg_void;
    ys = sh->sh_ys;
    //    fprint_objid(stderr, nhreg->rootoid, nhreg->rootoid_len);
    assert(sh->sh_oidlen == requestvb->name_length);
    assert(requestvb->name_length == nhreg->rootoid_len);
    assert(snmp_oid_compare(sh->sh_oid, sh->sh_oidlen,
			    requestvb->name, requestvb->name_length) == 0);
    assert(snmp_oid_compare(requestvb->name, requestvb->name_length,
			    nhreg->rootoid, nhreg->rootoid_len) == 0);

#if 0 /* If oid match fails */
    netsnmp_set_request_error(reqinfo, requests,
			      SNMP_NOSUCHOBJECT);
    return SNMP_ERR_NOERROR;
#endif
    if (yang2snmp_types(ys, &asn1_type, &cvtype) < 0)
	goto done;

    /* see net-snmp/agent/snmp_agent.h / net-snmp/library/snmp.h */
    switch (reqinfo->mode) {
    case MODE_GET:          /* 160 */
	requestvb->type = asn1_type; // ASN_NULL on input
	if (snmp_scalar_get(sh->sh_h, ys, requestvb, sh->sh_default, cvtype, reqinfo, requests) < 0)
	    goto done;
        break;
    case MODE_GETNEXT:      /* 161 */
	assert(0); // Not seen?
	break;
    case MODE_SET_RESERVE1: /* 0 */
        if (requestvb->type != asn1_type)
            netsnmp_set_request_error(reqinfo, requests,
                                      SNMP_ERR_WRONGTYPE);
        break;
    case MODE_SET_RESERVE2: /* 1 */
        break;
    case MODE_SET_ACTION:   /* 2 */
	if (snmp_scalar_set(sh->sh_h, ys, requestvb, reqinfo, requests) < 0)
	    goto done;
        break;
    case MODE_SET_UNDO:     /* 5 */
	if (clicon_rpc_discard_changes(sh->sh_h) < 0)
	    goto done;	
        break;

    case MODE_SET_COMMIT:   /* 3 */
	if (clicon_rpc_commit(sh->sh_h) < 0)
	    goto done;	
	break;
    case MODE_SET_FREE:     /* 4 */
        break;
    }
    retval = SNMP_ERR_NOERROR;
 done:
    return retval;
}