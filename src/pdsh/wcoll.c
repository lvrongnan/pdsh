/*****************************************************************************\
 *
 *  $Id$
 *  $Source$
 *
 *  Copyright (C) 1998-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick (garlick@llnl.gov>
 *  UCRL-CODE-980038
 *  
 *  This file is part of PDSH, a parallel remote shell program.
 *  For details, see <http://www.llnl.gov/linux/pdsh/>.
 *  
 *  PDSH is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  PDSH is distributed in the hope that it will be useful, but WITHOUT 
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
 *  for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with PDSH; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if     HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <assert.h>
#if	HAVE_UNISTD_H
#include <unistd.h>	/* for R_OK, access() */
#endif
#include <stdlib.h>	/* atoi */
 
#include "dsh.h"
#include "err.h"
#include "list.h"
#include "xmalloc.h"
#include "xstring.h"
#include "xpopen.h"
#include "wcoll.h"
#include "hostlist.h"

#if	HAVE_RMS
#include <qsw/types.h>
#include <rms/rmsapi.h>
#endif

/* 
 * Read wcoll from specified file or from the specified FILE pointer.
 * (one of the arguments must be NULL).  
 *	file (IN)	name of wcoll file (or NULL)
 *	f (IN)		FILE pointer to wcoll file (or NULL)	
 *	range_op (IN)	string containing single-char range delimiter
 *	RETURN		new list containing hostnames
 */
hostlist_t 
read_wcoll(char *file, FILE *f, char *range_op)
{
	char buf[LINEBUFSIZE], *p, *word;
	hostlist_t new = hostlist_create("");
	FILE *fp;

	assert(f != NULL || file != NULL);
	if (!new)
		errx("%p: hostlist_create failed\n");

	if (f == NULL) {		/* read_wcoll("file", NULL) */
		if (access(file, R_OK) == -1 || !(fp = fopen(file, "r")))
			errx("%p: %s: %m\n", file);
	} else				/* read_wcoll(NULL, fp) */ 
		fp = f;

	while (fgets(buf, LINEBUFSIZE, fp) != NULL) {
		/* zap text following comment char and whitespace */
		if ((p = strchr(buf, '#')) != NULL)
			*p = '\0';
		xstrcln(buf, NULL);

		if (hostlist_push(new, buf) == 0)
			err("%p: warning: target '%s' not parsed\n", buf);
	}
	if (f == NULL)
		fclose(fp);

	return new;
}

#if	HAVE_GENDERS
hostlist_t 
read_genders(char *attr, int iopt)
{
	FILE *f;
	hostlist_t new = hostlist_create("");
	char cmd[LINEBUFSIZE];
	char buf[LINEBUFSIZE];

	sprintf(cmd, "%s -%sn %s", _PATH_NODEATTR, iopt ? "" : "r", attr);
	f = xpopen(cmd, "r");
	if (f == NULL)
		errx("%p: error running %s\n", _PATH_NODEATTR);
	while (fgets(buf, LINEBUFSIZE, f) != NULL) {
		xstrcln(buf, NULL);
		if (hostlist_push_host(new, buf) == 0)
			err("%p: warning: target '%s' not parsed\n", buf);
	}
	if (xpclose(f) != 0) 
		errx("%p: error running %s\n", _PATH_NODEATTR);

	return new;
}
#endif /* HAVE_GENDERS */

#if	HAVE_SDR
static int 
sdr_numswitchplanes(void)
{
	FILE *f;
	list_t words;
	char cmd[LINEBUFSIZE];
	char buf[LINEBUFSIZE];
	int n;

	sprintf(cmd, "%s -x SP number_switch_planes", _PATH_SDRGETOBJECTS);

	f = xpopen(cmd, "r");

	if (f == NULL)
       		errx("%p: error running %s\n", _PATH_SDRGETOBJECTS);
	while (fgets(buf, LINEBUFSIZE, f) != NULL) {
		words = list_split(NULL, buf);
		assert(list_length(words) == 1);
		n = atoi(list_nth(words, 0));
		list_free(&words);
	}
	if (xpclose(f) != 0)
		err("%p: nonzero return code from %s\n", _PATH_SDRGETOBJECTS);

	return n;
}

static void 
sdr_getswitchname(char *switchName)
{
	FILE *f;
	list_t words;
	char cmd[LINEBUFSIZE];
	char buf[LINEBUFSIZE];
		
	sprintf(cmd, "%s -x Switch switch_number==1 switch_name",
	    _PATH_SDRGETOBJECTS);
	f = xpopen(cmd, "r");
	if (f == NULL)
       		errx("%p: error running %s\n", _PATH_SDRGETOBJECTS);
	while (fgets(buf, LINEBUFSIZE, f) != NULL) {
		words = list_split(NULL, buf);
		assert(list_length(words) == 1);
		strcpy(switchName, list_nth(words, 0));
		list_free(&words);
	}
	xpclose(f);
}

/*
 * Query the SDR for switch_responds or host_responds for all nodes and return
 * the results in an array indexed by node number.
 *	Gopt (IN)	pass -G to SDRGetObjects
 *	nameType (IN)	either "switch_responds" or "host_responds"
 *	resp (OUT)	array of boolean, indexed by node number
 */
static void 
sdr_getresp(bool Gopt, char *nameType, bool resp[])
{
	int nn, switchplanes;
	FILE *f;
	list_t words;
	char cmd[LINEBUFSIZE];
	char buf[LINEBUFSIZE];
	char *attr = "host_responds";

	switchplanes = 1;

	/* deal with Colony switch attribute name change */
	if (!strcmp(nameType, "switch_responds")) {
		sdr_getswitchname(buf);
		if (!strcmp(buf, "SP_Switch2")) {
			switchplanes = sdr_numswitchplanes();
			attr = (switchplanes == 1) ?  "switch_responds0" : 
				"switch_responds0 switch_responds1";
		} else
			attr = "switch_responds";
	}
		
	sprintf(cmd, "%s %s -x %s node_number %s", 
	    _PATH_SDRGETOBJECTS, Gopt ? "-G" : "", nameType, attr);
	f = xpopen(cmd, "r");
	if (f == NULL)
       		errx("%p: error running %s\n", _PATH_SDRGETOBJECTS);
	while (fgets(buf, LINEBUFSIZE, f) != NULL) {
		words = list_split(NULL, buf);
		assert(list_length(words) == (1+switchplanes));
		nn = atoi(list_nth(words, 0));
		assert(nn >= 0 && nn <= MAX_SP_NODE_NUMBER);
		if (switchplanes == 1) 
			resp[nn] = (atoi(list_nth(words, 1)) == 1);
		else if (switchplanes == 2)
			resp[nn] = (atoi(list_nth(words, 1)) == 1 ||
				    atoi(list_nth(words, 1)) == 1);
		else
			errx("%p: number_switch_planes > 2 not supported\n");
		list_free(&words);
	}
	xpclose(f);
}

/*
 * Query the SDR for hostnames of all nodes and return the results in an 
 * array indexed by node number.
 *	Gopt (IN)	pass -G to SDRGetObjects
 *	nameType (IN)	either "initial_hostname" or "reliable_hostname"
 *	resp (OUT)	array of hostnames indexed by node number (heap cpy)
 */
static void 
sdr_getnames(bool Gopt, char *nameType, char *nodes[])
{
	int nn;
	FILE *f;
	list_t words;
	char cmd[LINEBUFSIZE];
	char buf[LINEBUFSIZE];

	sprintf(cmd, "%s %s -x Node node_number %s", 
	    _PATH_SDRGETOBJECTS, Gopt ? "-G" : "", nameType);
	f = xpopen(cmd, "r");
	if (f == NULL)
		errx("%p: error running %s\n", _PATH_SDRGETOBJECTS);
	while (fgets(buf, LINEBUFSIZE, f) != NULL) {
		words = list_split(NULL, buf);
		assert(list_length(words) == 2);

		nn = atoi(list_nth(words, 0));
		assert (nn >= 0 && nn <= MAX_SP_NODE_NUMBER);
		nodes[nn] = Strdup(list_nth(words, 1));
		list_free(&words);
	}
	xpclose(f);
}

/*
 * Get the wcoll from the SDR.  
 *	Gopt (IN)	pass -G to SDRGetObjects
 *	altnames (IN)	ask for initial_hostname instead of reliable_hostname
 *	vopt (IN)	verify switch_responds/host_responds
 *	RETURN		new list containing hostnames
 */
hostlist_t 
sdr_wcoll(bool Gopt, bool iopt, bool vopt)
{
	hostlist_t new;
	char *inodes[MAX_SP_NODE_NUMBER + 1], *rnodes[MAX_SP_NODE_NUMBER + 1];
	bool sresp[MAX_SP_NODE_NUMBER + 1], hresp[MAX_SP_NODE_NUMBER + 1];
	int nn;


	/*
	 * Build arrays of hostnames indexed by node number.  Array is size 
	 * MAX_SP_NODE_NUMBER, with NULL pointers set for unused nodes.
	 */
	for (nn = 0; nn <= MAX_SP_NODE_NUMBER; nn++) { 
		inodes[nn] = NULL;
		rnodes[nn] = NULL;
	}
	if (iopt)
		sdr_getnames(Gopt, "initial_hostname", inodes);
	else
		sdr_getnames(Gopt, "reliable_hostname", rnodes);

	/*
	 * Gather data needed to process -v.
	 */
	if (vopt) {
		if (iopt)
			sdr_getresp(Gopt, "switch_responds", sresp);
		sdr_getresp(Gopt, "host_responds", hresp);
	}
		
	/*
	 * Collect and return the nodes.  If -v was specified and a node is 
	 * not responding, substitute the alternate name; if that is not 
	 * responding, skip the node.
	 */
	new = hostlist_create("");
	for (nn = 0; nn <= MAX_SP_NODE_NUMBER; nn++) {
		if (inodes[nn] != NULL || rnodes[nn] != NULL) {
			if (vopt) { 			    /* initial_host */
				if (iopt && sresp[nn] && hresp[nn]) 
					hostlist_push_host(new, inodes[nn]);
				else if (!iopt && hresp[nn])/* reliable_host */
					hostlist_push_host(new, rnodes[nn]);
			} else {
				if (iopt)		    /* initial_host */
					hostlist_push_host(new, inodes[nn]);
				else			    /* reliable_host */
					hostlist_push_host(new, rnodes[nn]);
			}
			if (inodes[nn] != NULL)		    /* free heap cpys */
				Free((void **)&inodes[nn]);
			if (rnodes[nn] != NULL)
				Free((void **)&rnodes[nn]);
		}
	}

	return new;
}
#endif /* HAVE_SDR */

#if HAVE_RMS
/* 
 * Helper for rms_wcoll() - RMS provides no API to get the list of nodes 
 * once allocated, so we query the msql database with 'rmsquery'.
 * part (IN)		partition name
 * rid (IN)		resource id
 * result (RETURN)	NULL or a list of hostnames
 */
static hostlist_t
rms_rid_to_nodes(char *part, int rid)
{
	FILE *f;
	char tmp[256];

	/* XXX how to specify partition?  do we need to? */
	sprintf(tmp, "%s \"select hostnames from resources where name='%d'\"",
			_PATH_RMSQUERY, rid);
	f = xpopen(tmp, "r");
	if (f == NULL)
		errx("%p: error running %s\n", _PATH_RMSQUERY);
	*tmp = '\0';
	while (fgets(tmp, sizeof(tmp), f) != NULL)
		;
	xpclose(f);
	/* should either have empty string or host[n-m] range */
	/* turn elanid range into list of hostnames */
	xstrcln(tmp, "\r\n\t "); /* drop trailing \n */
	return hostlist_create(tmp);
}

/*
 * If RMS_RESOURCE is set, return wcoll corresponding to RMS res allocation.
 * result (RETURN)	NULL or a list of hostnames
 */
hostlist_t
rms_wcoll(void)
{
	char *rhs;
	hostlist_t result = NULL;

	/* extract partition and resource ID from environment, if present */
	if ((rhs = getenv("RMS_RESOURCEID"))) {
		char *part, *ridstr = strchr(rhs, '.');
		int rid;

		if (!ridstr)
			errx("%p: malformed RMS_RESOURCEID value\n");
		*ridstr++ = '\0';
		rid = atoi(ridstr);
		part = rhs;

		result = rms_rid_to_nodes(part, rid);
	}

	/*
	 * Depend on PAM to keep user from setting RMS_RESOURCEID to
	 * someone else's allocation and stealing cycles.  pam_rms should 
	 * check to see if user has allocated the node before allowing qshd
	 * authorization to succede.
	 */

	return result;
}
#endif /* HAVE_RMS */
