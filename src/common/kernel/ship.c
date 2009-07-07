/* vi: set ts=2:
 *
 *	
 *	Eressea PB(E)M host Copyright (C) 1998-2003
 *      Christian Schlittchen (corwin@amber.kn-bremen.de)
 *      Katja Zedel (katze@felidae.kn-bremen.de)
 *      Henning Peters (faroul@beyond.kn-bremen.de)
 *      Enno Rehling (enno@eressea.de)
 *      Ingo Wilken (Ingo.Wilken@informatik.uni-oldenburg.de)
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 */

#include <config.h>
#include <kernel/eressea.h>
#include "ship.h"

/* kernel includes */
#include "build.h"
#include "unit.h"
#include "item.h"
#include "region.h"
#include "skill.h"

/* util includes */
#include <util/attrib.h>
#include <util/base36.h>
#include <util/event.h>
#include <util/language.h>
#include <util/lists.h>
#include <util/umlaut.h>
#include <util/storage.h>
#include <util/xml.h>

/* libc includes */
#include <assert.h>
#include <stdlib.h>
#include <string.h>


ship_typelist *shiptypes = NULL;

static local_names * snames;

const ship_type *
findshiptype(const char * name, const struct locale * lang)
{
	local_names * sn = snames;
	variant var;

	while (sn) {
		if (sn->lang==lang) break;
		sn=sn->next;
	}
	if (!sn) {
		struct ship_typelist * stl = shiptypes;
		sn = calloc(sizeof(local_names), 1);
		sn->next = snames;
		sn->lang = lang;
		while (stl) {
      variant var;
			const char * n = locale_string(lang, stl->type->name[0]);
      var.v = (void*)stl->type;
			addtoken(&sn->names, n, var);
			stl = stl->next;
		}
		snames = sn;
	}
	if (findtoken(&sn->names, name, &var)==E_TOK_NOMATCH) return NULL;
	return (const ship_type*)var.v;
}

const ship_type *
st_find(const char* name)
{
	const struct ship_typelist * stl = shiptypes;
	while (stl && strcmp(stl->type->name[0], name)) stl = stl->next;
	return stl?stl->type:NULL;
}

void
st_register(const ship_type * type) {
	struct ship_typelist * stl = malloc(sizeof(ship_type));
	stl->type = type;
	stl->next = shiptypes;
	shiptypes = stl;
}

#define SMAXHASH 7919
ship *shiphash[SMAXHASH];
void
shash(ship * s)
{
	ship *old = shiphash[s->no % SMAXHASH];

	shiphash[s->no % SMAXHASH] = s;
	s->nexthash = old;
}

void
sunhash(ship * s)
{
	ship **show;

	for (show = &shiphash[s->no % SMAXHASH]; *show; show = &(*show)->nexthash) {
		if ((*show)->no == s->no)
			break;
	}
	if (*show) {
		assert(*show == s);
		*show = (*show)->nexthash;
		s->nexthash = 0;
	}
}

static ship *
sfindhash(int i)
{
	ship *old;

	for (old = shiphash[i % SMAXHASH]; old; old = old->nexthash)
		if (old->no == i)
			return old;
	return 0;
}

struct ship *
findship(int i)
{
	return sfindhash(i);
}

struct ship *
findshipr(const region *r, int n)
{
  ship * sh;

  for (sh = r->ships; sh; sh = sh->next) {
    if (sh->no == n) {
      assert(sh->region == r);
      return sh;
    }
  }
  return 0;
}

void
damage_ship(ship * sh, double percent)
{
	double damage = DAMAGE_SCALE * percent * sh->size + sh->damage;
	sh->damage = (int)damage;
}

unit *
captain(ship *sh)
{
	unit *u;

	for(u = sh->region->units; u; u = u->next)
		if(u->ship == sh && fval(u, UFL_OWNER)) return u;

	return NULL;
}

/* Alte Schiffstypen: */
static ship * deleted_ships;

ship *
new_ship(const ship_type * stype, const struct locale * lang, region * r)
{
  static char buffer[7 + IDSIZE + 1];
  ship *sh = (ship *) calloc(1, sizeof(ship));

  sh->no = newcontainerid();
  sh->coast = NODIRECTION;
  sh->type = stype;
  sh->region = r;

  sprintf(buffer, "%s %s", LOC(lang, stype->name[0]), shipid(sh));
  sh->name = strdup(buffer);
  shash(sh);
  addlist(&r->ships, sh);
  return sh;
}

void
remove_ship(ship ** slist, ship * sh)
{
  region * r = sh->region;
  unit * u = r->units;

  handle_event(sh->attribs, "destroy", sh);
  while (u) {
    if (u->ship == sh) {
      leave_ship(u);
    }
    u = u->next;
  }
  sunhash(sh);
  while (*slist && *slist!=sh) slist = &(*slist)->next;
  assert(*slist);
  *slist = sh->next;
  sh->next = deleted_ships;
  deleted_ships = sh;
  sh->region = NULL;
}

void
free_ship(ship * s)
{
  while (s->attribs) a_remove(&s->attribs, s->attribs);
  free(s->name);
  free(s->display);
  free(s);
}

void
free_ships(void)
{
  while (deleted_ships) {
    ship * s = deleted_ships;
    deleted_ships = s->next;
  }
}

const char *
write_shipname(const ship * sh, char * ibuf, size_t size)
{
  snprintf(ibuf, size, "%s (%s)", sh->name, itoa36(sh->no));
  ibuf[size-1] = 0;
  return ibuf;
}

const char *
shipname(const ship * sh)
{
  typedef char name[OBJECTIDSIZE + 1];
  static name idbuf[8];
  static int nextbuf = 0;
  char *ibuf = idbuf[(++nextbuf) % 8];
  return write_shipname(sh, ibuf, sizeof(name));
}

int
shipcapacity (const ship * sh)
{
	int i;

	/* sonst ist construction:: size nicht ship_type::maxsize */
	assert(!sh->type->construction || sh->type->construction->improvement==NULL);

	if (sh->type->construction && sh->size!=sh->type->construction->maxsize)
		return 0;

#ifdef SHIPDAMAGE
	i = ((sh->size * DAMAGE_SCALE - sh->damage) / DAMAGE_SCALE)
		* sh->type->cargo / sh->size;
	i += ((sh->size * DAMAGE_SCALE - sh->damage) % DAMAGE_SCALE)
		* sh->type->cargo / (sh->size*DAMAGE_SCALE);
#else
	i = sh->type->cargo;
#endif
	return i;
}

void
getshipweight(const ship * sh, int *sweight, int *scabins)
{
	unit * u;
	*sweight = 0;
	*scabins = 0;
	for (u = sh->region->units; u; u = u->next)
	if (u->ship == sh) {
		*sweight += weight(u);
		*scabins += u->number;
	}
}

unit *
shipowner(const ship * sh)
{
	unit *u;
	unit *first = NULL;

        const region * r = sh->region;

        /* Pr�fen ob Eigent�mer am leben. */
	for (u = r->units; u; u = u->next) {
		if (u->ship == sh) {
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

void
write_ship_reference(const struct ship * sh, struct storage * store)
{
  store->w_id(store, (sh && sh->region)?sh->no:0);
}

void
ship_setname(ship * self, const char * name)
{
  free(self->name);
  if (name) self->name = strdup(name);
  else self->name = NULL;
}

const char *
ship_getname(const ship * self)
{
  return self->name;
}
