/* vi: set ts=2:
 *
 *	
 *	Eressea PB(E)M host Copyright (C) 1998-2003
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea-pbem.de)
 *      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 */

#include <config.h>

#include "eressea.h"
#include "building.h"
#include "attrib.h"

/* kernel includes */
#include "item.h"
#include "curse.h" /* f�r C_NOCOST */
#include "unit.h"
#include "region.h"
#include "skill.h"
#include "magic.h"
#include "save.h"
#include "version.h"

/* util includes */
#include <util/base36.h>
#include <util/event.h>
#include <util/functions.h>
#include <util/language.h>
#include <util/lists.h>
#include <util/resolve.h>
#include <util/umlaut.h>

/* libc includes */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* attributes includes */
#include <attributes/matmod.h>

static const char * NULLSTRING = "(null)";

static void 
lc_init(struct attrib *a)
{
  a->data.v = calloc(1, sizeof(building_action));
}

static void 
lc_done(struct attrib *a)
{
  building_action * data = (building_action*)a->data.v;
  if (data->fname) free(data->fname);
  if (data->param) free(data->param);
  free(data);
}

static void 
lc_write(const struct attrib * a, FILE* F)
{
  building_action * data = (building_action*)a->data.v;
  const char * fname = data->fname;
  const char * fparam = data->param;
  building * b = data->b;

  write_building_reference(b, F);
  fwritestr(F, fname);
#if RELEASE_VERSION>=BACTION_VERSION
  fputc(' ', F);
  fwritestr(F, fparam?fparam:NULLSTRING);
#endif
  fputc(' ', F);
}

static int
lc_read(struct attrib * a, FILE* F)
{
  char lbuf[256];
  building_action * data = (building_action*)a->data.v;

  read_building_reference(&data->b, F);
  freadstr(F, lbuf, sizeof(lbuf));
  data->fname = strdup(lbuf);
  if (global.data_version>=BACTION_VERSION) {
    freadstr(F, lbuf, sizeof(lbuf));
    if (strcmp(lbuf, NULLSTRING)==0) data->param = NULL;
    else data->param = strdup(lbuf);
  } else {
    data->param = strdup(NULLSTRING);
  }
  return AT_READ_OK;
}

attrib_type at_building_action = {
  "lcbuilding", 
  lc_init, lc_done, 
  NULL, 
  lc_write, lc_read
};

typedef struct building_typelist {
  struct building_typelist * next;
  building_type * type;
} building_typelist;

static building_typelist *buildingtypes;

building_type *
bt_find(const char* name)
{
	const struct building_typelist * btl = buildingtypes;

	if (global.data_version < RELEASE_VERSION) {
		const char * translation[3][2] = { 
			{ "illusion", "illusioncastle" }, 
			{ "generic", "genericbuilding" }, 
			{ NULL, NULL } 
		};
		int i;
		for (i=0;translation[i][0];++i) {
			/* calling a building "illusion" was a bad idea" */
			if (strcmp(translation[i][0], name)==0) {
				name = translation[i][1];
				break;
			}
		}
	}
	while (btl && strcmp(btl->type->_name, name)) btl = btl->next;
	return btl?btl->type:NULL;
}

void
bt_register(building_type * type)
{
	struct building_typelist * btl = malloc(sizeof(building_type));
	if (type->init) type->init(type);
	btl->type = type;
	btl->next = buildingtypes;
	buildingtypes = btl;
}

int
buildingcapacity(const building * b)
{
	if (b->type->capacity>=0) {
    if (b->type->maxcapacity>=0) {
      return min(b->type->maxcapacity, b->size * b->type->capacity);
    }
		return b->size * b->type->capacity;
	}
  if (b->size>=b->type->maxsize) {
    if (b->type->maxcapacity>=0) {
      return b->type->maxcapacity;
    }
  }
	return 0;
}

attrib_type at_building_generic_type = {
	  "building_generic_type", NULL, NULL, NULL, a_writestring, a_readstring, ATF_UNIQUE
};

const char *
buildingtype(const building_type * btype, const building * b, int bsize)
{
	const char * s = NULL;
  static boolean init_generic = false;
	static const struct building_type * bt_generic;

  if (!init_generic) {
    init_generic = true;
    bt_generic = bt_find("generic");
  }

  if (btype == bt_generic) {
		const attrib *a = a_find(b->attribs, &at_building_generic_type);
		if (a) s = (const char*)a->data.v;
	}

	if (btype->name) s = btype->name(btype, bsize);
	if (s==NULL) s = btype->_name;
	return s;
}

#define BMAXHASH 7919
static building *buildhash[BMAXHASH];
void
bhash(building * b)
{
  building *old = buildhash[b->no % BMAXHASH];

  buildhash[b->no % BMAXHASH] = b;
  b->nexthash = old;
}

void
bunhash(building * b)
{
	building **show;

	for (show = &buildhash[b->no % BMAXHASH]; *show; show = &(*show)->nexthash) {
		if ((*show)->no == b->no)
			break;
	}
	if (*show) {
		assert(*show == b);
		*show = (*show)->nexthash;
		b->nexthash = 0;
	}
}

static building *
bfindhash(int i)
{
	building *old;

	for (old = buildhash[i % BMAXHASH]; old; old = old->nexthash)
		if (old->no == i)
			return old;
	return 0;
}

building *
findbuilding(int i)
{
	return bfindhash(i);
}
/* ** old building types ** */


/** Building: Fortification */
enum {
	B_SITE,
	B_TRADEPOST,
	B_FORTIFICATION,
	B_TOWER,
	B_CASTLE,
	B_FORTRESS,
	B_CITADEL,
	MAXBUILDINGS
};

static int
sm_smithy(const unit * u, const region * r, skill_t sk, int value) /* skillmod */
{
	if (sk==SK_WEAPONSMITH || sk==SK_ARMORER) {
		if (u->region == r) return value + 1;
	}
	return value;
}
static int
mm_smithy(const unit * u, const resource_type * rtype, int value) /* material-mod */
{
	if (rtype == oldresourcetype[R_IRON]) return value * 2;
	return value;
}
static void
init_smithy(struct building_type * bt)
{
	a_add(&bt->attribs, make_skillmod(NOSKILL, SMF_PRODUCTION, sm_smithy, 1.0, 0));
	a_add(&bt->attribs, make_matmod(mm_smithy));
}

static const char *
castle_name(const struct building_type* btype, int bsize)
{
	const char * fname[MAXBUILDINGS] = {
	  "site",
		"tradepost",
	  "fortification",
	  "tower",
	  "castle",
	  "fortress",
	  "citadel" };
	const construction * ctype;
	int i = 0;

	ctype = btype->construction;
	while (ctype && ctype->maxsize != -1 && ctype->maxsize<=bsize) {
		bsize-=ctype->maxsize;
		ctype=ctype->improvement;
		++i;
	}
	return fname[i];
}

#ifdef WDW_PYRAMID

static const char *
pyramid_name(const struct building_type* btype, int bsize)
{
  static char p_name_buf[32];
  int level=0;
  const construction * ctype;

  ctype = btype->construction;
  
	while (ctype && ctype->maxsize != -1 && ctype->maxsize<=bsize) {
    bsize-=ctype->maxsize;
    ctype=ctype->improvement;
    ++level;
  }

  sprintf(p_name_buf, "pyramid%d", level);

  return p_name_buf;
}

int
wdw_pyramid_level(const struct building *b)
{
  const construction *ctype = b->type->construction;
  int completed = b->size;
  int level = 0;

  while(ctype->improvement != NULL &&
      ctype->improvement != ctype &&
      ctype->maxsize > 0 &&
      ctype->maxsize <= completed)
  {
    ++level;
    completed-=ctype->maxsize;
    ctype = ctype->improvement;
  }

  return level;
}
#endif

/* for finding out what was meant by a particular building string */

static local_names * bnames;

const building_type *
findbuildingtype(const xmlChar * name, const struct locale * lang)
{
  variant type;
	local_names * bn = bnames;

	while (bn) {
		if (bn->lang==lang) break;
		bn=bn->next;
	}
	if (!bn) {
		struct building_typelist * btl = buildingtypes;
		bn = calloc(sizeof(local_names), 1);
		bn->next = bnames;
		bn->lang = lang;
		while (btl) {
			const xmlChar * n = locale_string(lang, btl->type->_name);
      type.v = (void*)btl->type;
			addtoken(&bn->names, n, type);
			btl=btl->next;
		}
		bnames = bn;
	}
	if (findtoken(&bn->names, name, &type)==E_TOK_NOMATCH) return NULL;
	return (const building_type*)type.v;
}


void
register_buildings(void)
{
	register_function((pf_generic)init_smithy, "init_smithy");
	register_function((pf_generic)castle_name, "castle_name");
#ifdef WDW_PYRAMID
	register_function((pf_generic)pyramid_name, "pyramid_name");
#endif
}

void *
resolve_building(variant id) {
   return findbuilding(id.i);
}

void
write_building_reference(const struct building * b, FILE * F)
{
	fprintf(F, "%s ", b?itoa36(b->no):"0");
}

int
read_building_reference(struct building ** b, FILE * F)
{
	variant var;
	char zText[16];
	fscanf(F, "%s ", zText);
	var.i = atoi36(zText);
	if (var.i==0) {
		*b = NULL;
		return AT_READ_FAIL;
	}
	else {
		*b = findbuilding(var.i);
		if (*b==NULL) ur_add(var, (void**)b, resolve_building);
		return AT_READ_OK;
	}
}

void
free_buildinglist(building_list *blist)
{
  while (blist) {
    building_list * rl2 = blist->next;
    free(blist);
    blist = rl2;
  }
}

void
add_buildinglist(building_list **blist, building *b)
{
  building_list *rl2 = (building_list*)malloc(sizeof(building_list));

  rl2->data = b;
  rl2->next = *blist;

  *blist = rl2;
}

building *
new_building(const struct building_type * btype, region * r, const struct locale * lang)
{
	building *b = (building *) calloc(1, sizeof(building));

	b->no  = newcontainerid();
	bhash(b);

	b->type = btype;
	b->region = r;
	addlist(&r->buildings, b);

	{
		static char buffer[IDSIZE + 1 + NAMESIZE + 1];
		if (b->type->name)
			sprintf(buffer, "%s", locale_string(lang, btype->_name));
		else
			sprintf(buffer, "%s", LOC(lang, buildingtype(btype, b, 0)));
		set_string(&b->name, buffer);
	}
	return b;
}

void
destroy_building(building * b)
{
  unit *u;
  direction_t d;
  static const struct building_type * bt_caravan, * bt_dam, * bt_tunnel;
  boolean init = false;

  if (!init) {
    init = true;
    bt_caravan = bt_find("caravan");
    bt_dam = bt_find("dam");
    bt_tunnel = bt_find("tunnel");
  }

  if (!bfindhash(b->no)) return;
  for (u=b->region->units; u; u=u->next) {
    if (u->building == b) leave(b->region, u);
  }
  
  b->size = 0;
  update_lighthouse(b);
  bunhash(b);
  
  /* Falls Karawanserei, Damm oder Tunnel einst�rzen, wird die schon
   * gebaute Stra�e zur H�lfte vernichtet */
  if (b->type == bt_caravan || b->type == bt_dam || b->type == bt_tunnel) {
    region * r = b->region;
    for (d=0;d!=MAXDIRECTIONS;++d) if (rroad(r, d) > 0) {
      rsetroad(r, d, rroad(r, d) / 2);
    }
  }

  /* Stattdessen nur aus Liste entfernen, aber im Speicher halten. */
  choplist(&b->region->buildings, b);
  handle_event(b->attribs, "destroy", b);
}

extern struct attrib_type at_icastle;

int
buildingeffsize(const building * b, boolean img)
{
	int i = b->size, n = 0;
	const construction * cons;
	static const struct building_type * bt_castle;
	if (!bt_castle) bt_castle = bt_find("castle");
	assert(bt_castle);

	if (b==NULL) return 0;

	if (b->type!=bt_castle) {
		if (img) {
			const attrib * a = a_find(b->attribs, &at_icastle);
			if (!a || a->data.v != bt_castle) return 0;
		} else return 0;
	}
	cons = bt_castle->construction;
	assert(cons);

	while (cons && cons->maxsize != -1 && i>=cons->maxsize) {
		i-=cons->maxsize;
		cons = cons->improvement;
		++n;
	}

	return n;
}

unit *
buildingowner(const region * r, const building * b)
{
	unit *u = NULL;
	unit *first = NULL;
#ifndef BROKEN_OWNERS
	assert(r == b->region);
#endif
	/* Pr�fen ob Eigent�mer am leben. */

	for (u = r->units; u; u = u->next) {
		if (u->building == b) {
			if (!first && u->number > 0)
				first = u;
			if (fval(u, UFL_OWNER) && u->number > 0)
				return u;
			if (u->number == 0)
				freset(u, UFL_OWNER);
		}
	}

	/* Eigent�mer tot oder kein Eigent�mer vorhanden. Erste lebende Einheit
	 * nehmen. */

	if (first)
		fset(first, UFL_OWNER);
	return first;
}
