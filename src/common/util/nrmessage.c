/* vi: set ts=2:
 +-------------------+  Christian Schlittchen <corwin@amber.kn-bremen.de>
 |                   |  Enno Rehling <enno@eressea-pbem.de>
 | Eressea PBEM host |  Katja Zedel <katze@felidae.kn-bremen.de>
 | (c) 1998 - 2001   |  Henning Peters <faroul@beyond.kn-bremen.de>
 |                   |  Ingo Wilken <Ingo.Wilken@informatik.uni-oldenburg.de>
 +-------------------+  Stefan Reich <reich@halbling.de>

 This program may not be used, modified or distributed 
 without prior permission by the authors of Eressea.
 $Id: nrmessage.c,v 1.2 2001/02/25 20:10:43 enno Exp $
*/

#include <config.h>
#include "nrmessage.h"

#include "message.h"
#include "translation.h"

#include <string.h>
#include <stdlib.h>

typedef struct nrmessage_type {
	const struct message_type * mtype;
	const struct locale * lang;
	const char * string;
	const char * vars;
	struct nrmessage_type * next;
} nrmessage_type;

static nrmessage_type * messagetypes;

static nrmessage_type * 
nrt_find(const struct locale * lang, const struct message_type * mtype)
{
	nrmessage_type * found = NULL;
	nrmessage_type * type = messagetypes;
	while (type) {
		if (type->mtype==mtype) {
			if (found==NULL) found = type;
			else if (type->lang==NULL) found = type;
			if (lang==type->lang) break;
		}
		type = type->next;
	}
	return found;
}

void 
nrt_register(const struct message_type * mtype, const struct locale * lang, const char * string)
{
	nrmessage_type * nrt = messagetypes;
	while (nrt && (nrt->lang!=lang || nrt->mtype!=mtype)) {
		nrt = nrt->next;
	}
	if (!nrt) {
		int i;
		char zNames[256];
		char * c = zNames;
		nrt = malloc(sizeof(nrmessage_type));
		nrt->lang = lang;
		nrt->mtype = mtype;
		nrt->next = messagetypes;
		messagetypes = nrt;
		nrt->string = strdup(string);
		for (i=0;i!=mtype->nparameters;++i) {
			if (i!=0) *c++ = ' ';
			c+= strlen(strcpy(c, mtype->pnames[i]));
		}
		nrt->vars = strdup(zNames);
		/* TODO: really necessary to strdup them all? here? better to extend va_translate! */
	}
}


int
nr_render(const struct message * msg, const struct locale * lang, char * buffer)
{
	struct nrmessage_type * nrt = nrt_find(lang, msg->type);

	if (nrt) {
		strcpy(buffer, translate(nrt->string, nrt->vars, msg->parameters));
		return 0;
	}
	return -1;
}
