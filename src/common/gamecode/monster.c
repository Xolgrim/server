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
 *  based on:
 *
 * Atlantis v1.0  13 September 1993 Copyright 1993 by Russell Wallace
 * Atlantis v1.7                    Copyright 1996 by Alex Schr�der
 *
 * This program may not be used, modified or distributed without
 * prior permission by the authors of Eressea.
 * This program may not be sold or used commercially without prior written
 * permission from the authors.
 */

#include <config.h>
#include <kernel/eressea.h>
#include "monster.h"

/* gamecode includes */
#include "economy.h"
#include "give.h"

/* triggers includes */
#include <triggers/removecurse.h>

/* attributes includes */
#include <attributes/targetregion.h>
#include <attributes/hate.h>

/* spezialmonster */
#include <spells/alp.h>

/* kernel includes */
#include <kernel/build.h>
#include <kernel/equipment.h>
#include <kernel/faction.h>
#include <kernel/item.h>
#include <kernel/message.h>
#include <kernel/move.h>
#include <kernel/names.h>
#include <kernel/order.h>
#include <kernel/pathfinder.h>
#include <kernel/pool.h>
#include <kernel/race.h>
#include <kernel/region.h>
#include <kernel/reports.h>
#include <kernel/skill.h>
#include <kernel/terrain.h>
#include <kernel/terrainid.h>
#include <kernel/unit.h>

/* util includes */
#include <util/attrib.h>
#include <util/base36.h>
#include <util/bsdstring.h>
#include <util/event.h>
#include <util/lists.h>
#include <util/log.h>
#include <util/rand.h>
#include <util/rng.h>

/* libc includes */
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define MOVECHANCE                  25	/* chance fuer bewegung */

#define MAXILLUSION_TEXTS   3

static void
reduce_weight(unit * u)
{
  int horses = get_resource(u, oldresourcetype[R_HORSE]);
  int capacity = walkingcapacity(u);
  item ** itmp = &u->items;
  int weight = 0;

  if (horses > 0) {
    horses = MIN(horses, (u->number*2));
    change_resource(u, oldresourcetype[R_HORSE], - horses);
  }

  /* 1. get rid of anything that isn't silver or really lightweight or helpful in combat */
  while (capacity>0 && *itmp!=NULL) {
    item * itm = *itmp;
    const item_type * itype = itm->type;
    weight += itm->number*itype->weight;
    if (weight>capacity) {
      if (itype->weight>=10 && itype->rtype->wtype==0 && itype->rtype->atype==0) {
        if (itype->capacity < itype->weight) {
          int reduce = MIN(itm->number, -((capacity-weight)/itype->weight));
          give_item(reduce, itm->type, u, NULL, NULL);
          weight -= reduce * itype->weight;
        }
      }
    }
    if (*itmp==itm) itmp=&itm->next;
  }

  for (itmp = &u->items;*itmp && weight>capacity;) {
    item * itm = *itmp;
    const item_type * itype = itm->type;
    weight += itm->number*itype->weight;
    if (itype->capacity < itype->weight) {
      int reduce = MIN(itm->number, -((capacity-weight)/itype->weight));
      give_item(reduce, itm->type, u, NULL, NULL);
      weight -= reduce * itype->weight;
    }
    if (*itmp==itm) itmp=&itm->next;
  }
}

static boolean
is_waiting(const unit * u)
{
  if (fval(u, UFL_ISNEW|UFL_MOVED)) return true;
  return false;
}

static order *
monster_attack(unit * u, const unit * target)
{
  if (u->region!=target->region) return NULL;
  if (u->faction==target->faction) return NULL;
  if (!cansee(u->faction, u->region, target, 0)) return NULL;
  if (is_waiting(u)) return NULL;
  
  return create_order(K_ATTACK, u->faction->locale, "%i", target->no);
}


static order *
get_money_for_dragon(region * r, unit * u, int wanted)
{
	unit *u2;
	int n;

	/* attackiere bewachende einheiten */
  for (u2 = r->units; u2; u2 = u2->next) {
    if (u2 != u && getguard(u2)&GUARD_TAX) {
      order * ord = monster_attack(u, u2);
      if (ord) addlist(&u->orders, ord);
    }
  }

	/* falls genug geld in der region ist, treiben wir steuern ein. */
  if (rmoney(r) >= wanted) {
    /* 5% chance, dass der drache aus einer laune raus attackiert */
    if (chance(1.0-u->race->aggression)) {
      return create_order(K_TAX, default_locale, NULL);
    }
  }

  /* falls der drache launisch ist, oder das regionssilber knapp, greift er alle an */
	n = 0;
  for (u2 = r->units; u2; u2 = u2->next) {
    if (u2->faction != u->faction && cansee(u->faction, r, u2, 0)) {
      int m = get_money(u2);
      if (m==0 || (getguard(u2) & GUARD_TAX)) continue;
      else {
        order * ord = monster_attack(u, u2);
        if (ord) {
          addlist(&u->orders, ord);
          n += m;
        }
      }
    }
  }

  /* falls die einnahmen erreicht werden, bleibt das monster noch eine
	 * runde hier. */
  if (n + rmoney(r) >= wanted) {
    return create_order(K_TAX, default_locale, NULL);
  }

  /* wenn wir NULL zur�ckliefern, macht der drache was anderes, z.b. weggehen */
  return NULL;
}

static int
all_money(region * r, faction * f)
{
	unit *u;
	int m;

	m = rmoney(r);
  for (u = r->units; u; u = u->next) {
    if (f!=u->faction) {
      m += get_money(u);
    }
	}
	return m;
}

static direction_t
richest_neighbour(region * r, faction * f, int absolut)
{

	/* m - maximum an Geld, d - Richtung, i - index, t = Geld hier */

	double m;
	double t;
	direction_t d = NODIRECTION, i;

	if (absolut == 1 || rpeasants(r) == 0) {
		m = (double) all_money(r, f);
	} else {
		m = (double) all_money(r, f) / (double) rpeasants(r);
	}

	/* finde die region mit dem meisten geld */

  for (i = 0; i != MAXDIRECTIONS; i++) {
    region * rn = rconnect(r, i);
		if (rn!=NULL && fval(rn->terrain, LAND_REGION)) {
			if (absolut == 1 || rpeasants(rn) == 0) {
				t = (double) all_money(rn, f);
			} else {
				t = (double) all_money(rn, f) / (double) rpeasants(rn);
			}

			if (t > m) {
				m = t;
				d = i;
			}
    }
  }
	return d;
}

static boolean
room_for_race_in_region(region *r, const race * rc)
{
	unit *u;
	int  c = 0;

	for(u=r->units;u;u=u->next) {
		if(u->race == rc) c += u->number;
	}

	if(c > (rc->splitsize*2))
		return false;

	return true;
}

static direction_t
random_neighbour(region * r, unit *u)
{
	direction_t i;
	region *rc;
	int rr, c = 0, c2 = 0;

	/* Nachsehen, wieviele Regionen in Frage kommen */

	for (i = 0; i != MAXDIRECTIONS; i++) {
		rc = rconnect(r, i);
		if (rc && can_survive(u, rc)) {
			if(room_for_race_in_region(rc, u->race)) {
				c++;
			}
			c2++;
		}
	}

	if (c == 0) {
		if(c2 == 0) {
			return NODIRECTION;
		} else {
			c  = c2;
			c2 = 0;		/* c2 == 0 -> room_for_race nicht beachten */
		}
	}

	/* Zuf�llig eine ausw�hlen */

	rr = rng_int() % c;

	/* Durchz�hlen */

	c = -1;
	for (i = 0; i != MAXDIRECTIONS; i++) {
		rc = rconnect(r, i);
		if (rc && can_survive(u, rc)) {
			if(c2 == 0) {
				c++;
			} else if(room_for_race_in_region(rc, u->race)) {
				c++;
			}
			if (c == rr) return i;
		}
	}

	assert(1 == 0);				/* Bis hierhin sollte er niemals kommen. */
	return NODIRECTION;
}

static direction_t
treeman_neighbour(region * r)
{
	direction_t i;
	int rr;
	int c = 0;

	/* Nachsehen, wieviele Regionen in Frage kommen */

	for (i = 0; i != MAXDIRECTIONS; i++) {
		if (rconnect(r, i)
			&& rterrain(rconnect(r, i)) != T_OCEAN
			&& rterrain(rconnect(r, i)) != T_GLACIER
			&& rterrain(rconnect(r, i)) != T_DESERT) {
			c++;
		}
	}

	if (c == 0) {
		return NODIRECTION;
	}
	/* Zuf�llig eine ausw�hlen */

	rr = rng_int() % c;

	/* Durchz�hlen */

	c = -1;
	for (i = 0; i != MAXDIRECTIONS; i++) {
		if (rconnect(r, i)
			&& rterrain(rconnect(r, i)) != T_OCEAN
			&& rterrain(rconnect(r, i)) != T_GLACIER
			&& rterrain(rconnect(r, i)) != T_DESERT) {
			c++;
			if (c == rr) {
				return i;
			}
		}
	}

	assert(1 == 0);				/* Bis hierhin sollte er niemals kommen. */
	return NODIRECTION;
}

static order *
monster_move(region * r, unit * u)
{
  direction_t d = NODIRECTION;

  if (is_waiting(u)) return NULL;
  switch(old_race(u->race)) {
    case RC_FIREDRAGON:
    case RC_DRAGON:
    case RC_WYRM:
      d = richest_neighbour(r, u->faction, 1);
      break;
    case RC_TREEMAN:
      d = treeman_neighbour(r);
      break;
    default:
      d = random_neighbour(r,u);
      break;
  }

  /* falls kein geld gefunden wird, zufaellig verreisen, aber nicht in
  * den ozean */

  if (d == NODIRECTION)
    return NULL;

  reduce_weight(u);
  return create_order(K_MOVE, u->faction->locale, "%s", LOC(u->faction->locale, directions[d]));
}

/* Wir machen das mal autoconf-style: */
#ifndef HAVE_DRAND48
#define drand48() (((double)rng_int()) / RAND_MAX)
#endif

static int
dragon_affinity_value(region *r, unit *u)
{
	int m = all_money(r, u->faction);

	if(u->race == new_race[RC_FIREDRAGON]) {
		return (int)(normalvariate(m,m/2));
	} else {
		return (int)(normalvariate(m,m/4));
	}
}

static attrib *
set_new_dragon_target(unit * u, region * r, int range)
{
	int max_affinity = 0;
	region *max_region = NULL;

#if 1
  region_list * rptr, * rlist = regions_in_range(r, range, allowed_dragon);
  for (rptr=rlist;rptr;rptr=rptr->next) {
    region * r2 = rptr->data;
    int affinity = dragon_affinity_value(r2, u);
    if (affinity > max_affinity) {
      max_affinity = affinity;
      max_region = r2;
    }
  }

  free_regionlist(rlist);
#else
  int x, y;
  for (x = r->x - range; x < r->x + range; x++) {
		for (y = r->y - range; y < r->y + range; y++) {
      region * r2 = findregion(x, y);
      if (r2!=NULL) {
        int affinity = dragon_affinity_value(r2, u);
        if (affinity > max_affinity) {
          if (koor_distance (r->x, r->y, x, y) <= range && path_exists(r, r2, range, allowed_dragon)) 
          {
            max_affinity = affinity;
            max_region = r2;
          }
				}
			}
		}
	}
#endif
	if (max_region && max_region != r) {
		attrib * a = a_find(u->attribs, &at_targetregion);
		if (!a) {
			a = a_add(&u->attribs, make_targetregion(max_region));
		} else {
			a->data.v = max_region;
		}
		return a;
	}
	return NULL;
}

static order *
make_movement_order(unit * u, const region * target, int moves, boolean (*allowed)(const region *, const region *))
{
  region * r = u->region;
  region ** plan;
  int bytes, position = 0;
  char zOrder[128], * bufp = zOrder;
  size_t size = sizeof(zOrder) - 1;

  if (is_waiting(u)) return NULL;

  plan = path_find(r, target, DRAGON_RANGE*5, allowed);
	if (plan==NULL) return NULL;

	bytes = (int)strlcpy(bufp, (const char *)LOC(u->faction->locale, keywords[K_MOVE]), size);
  if (wrptr(&bufp, &size, bytes)!=0) WARN_STATIC_BUFFER();

  while (position!=moves && plan[position+1]) {
		region * prev = plan[position];
		region * next = plan[++position];
		direction_t dir = reldirection(prev, next);
		assert(dir!=NODIRECTION && dir!=D_SPECIAL);
    if (size>1) {
      *bufp++ = ' ';
      --size;
    }
		bytes = (int)strlcpy(bufp, (const char *)LOC(u->faction->locale, directions[dir]), size);
    if (wrptr(&bufp, &size, bytes)!=0) WARN_STATIC_BUFFER();
	}

  *bufp = 0;
	return parse_order(zOrder, u->faction->locale);
}

static order *
monster_seeks_target(region *r, unit *u)
{
	direction_t d;
	unit *target = NULL;
	int dist, dist2;
	direction_t i;
	region *nr;

	/* Das Monster sucht ein bestimmtes Opfer. Welches, steht
	 * in einer Referenz/attribut
	 * derzeit gibt es nur den alp
	 */

	switch( old_race(u->race) ) {
		case RC_ALP:
			target = alp_target(u);
			break;
		default:
			assert(!"Seeker-Monster gibt kein Ziel an");
	}

	/* TODO: pr�fen, ob target �berhaupt noch existiert... */
  if (!target) {
    log_error(("Monster '%s' hat kein Ziel!\n", unitname(u)));
    return NULL; /* this is a bug workaround! remove!! */
  }

  if(r == target->region ) { /* Wir haben ihn! */
    if (u->race == new_race[RC_ALP]) {
      alp_findet_opfer(u, r);
    }
    else {
      assert(!"Seeker-Monster hat keine Aktion fuer Ziel");
    }
    return NULL;
  }
  
  /* Simpler Ansatz: Nachbarregion mit gerinster Distanz suchen.
   * Sinnvoll momentan nur bei Monstern, die sich nicht um das
   * Terrain k�mmern.  Nebelw�nde & Co machen derzeit auch nix...
   */
  dist2 = distance(r, target->region);
  d = NODIRECTION;
  for( i = 0; i < MAXDIRECTIONS; i++ ) {
    nr = rconnect(r, i);
    assert(nr);
    dist = distance(nr, target->region);
    if( dist < dist2 ) {
      dist2 = dist;
      d = i;
    }
  }
  assert(d != NODIRECTION );
  
  return create_order(K_MOVE, u->faction->locale, "%s", LOC(u->faction->locale, directions[d]));
}

unit *
random_unit(const region * r)
{
	int c = 0;
	int n;
	unit *u;

	for (u = r->units; u; u = u->next) {
		if (u->race != new_race[RC_SPELL]) {
			c += u->number;
		}
	}

	if (c == 0) {
		return NULL;
	}
	n = rng_int() % c;
	c = 0;
	u = r->units;

	while (u && c < n) {
		if (u->race != new_race[RC_SPELL]) {
			c += u->number;
		}
		u = u->next;
	}

	return u;
}

static void
monster_attacks(unit * u)
{
  region * r = u->region;
  unit * u2;

  for (u2=r->units;u2;u2=u2->next) {
    if (u2->faction!=u->faction && chance(0.75)) {
      order * ord = monster_attack(u, u2);
      if (ord) addlist(&u->orders, ord);
    }
  }
}

static void
eaten_by_monster(unit * u)
{
	int n = 0;
	int horse = 0;

	switch (old_race(u->race)) {
	case RC_FIREDRAGON:
		n = rng_int()%80 * u->number;
		horse = get_item(u, I_HORSE);
		break;
	case RC_DRAGON:
		n = rng_int()%200 * u->number;
		horse = get_item(u, I_HORSE);
		break;
	case RC_WYRM:
		n = rng_int()%500 * u->number;
		horse = get_item(u, I_HORSE);
		break;
	default:
		n = rng_int()%(u->number/20+1);
	}

	if (n > 0) {
		n = lovar(n);
		n = MIN(rpeasants(u->region), n);

		if (n > 0) {
			deathcounts(u->region, n);
			rsetpeasants(u->region, rpeasants(u->region) - n);
			ADDMSG(&u->region->msgs, msg_message("eatpeasants", "unit amount", u, n));
		}
	}
	if (horse > 0) {
		set_item(u, I_HORSE, 0);
    ADDMSG(&u->region->msgs, msg_message("eathorse", "unit amount", u, horse));
	}
}

static void
absorbed_by_monster(unit * u)
{
	int n;

	switch (old_race(u->race)) {
	default:
		n = rng_int()%(u->number/20+1);
	}

	if(n > 0) {
		n = lovar(n);
		n = MIN(rpeasants(u->region), n);
		if (n > 0){
			rsetpeasants(u->region, rpeasants(u->region) - n);
			scale_number(u, u->number + n);
			ADDMSG(&u->region->msgs, msg_message("absorbpeasants", 
        "unit race amount", u, u->race, n));
		}
	}
}

static int
scareaway(region * r, int anzahl)
{
	int n, p, diff = 0, emigrants[MAXDIRECTIONS];
	direction_t d;

	anzahl = MIN(MAX(1, anzahl),rpeasants(r));

	/* Wandern am Ende der Woche (normal) oder wegen Monster. Die
	 * Wanderung wird erst am Ende von demographics () ausgefuehrt.
	 * emigrants[] ist local, weil r->newpeasants durch die Monster
	 * vielleicht schon hochgezaehlt worden ist. */

	for (d = 0; d != MAXDIRECTIONS; d++)
		emigrants[d] = 0;

	p = rpeasants(r);
	assert(p >= 0 && anzahl >= 0);
	for (n = MIN(p, anzahl); n; n--) {
		direction_t dir = (direction_t)(rng_int() % MAXDIRECTIONS);
		region * rc = rconnect(r, dir);

		if (rc && fval(rc->terrain, LAND_REGION)) {
			++diff;
			rc->land->newpeasants++;
			emigrants[dir]++;
		}
	}
	rsetpeasants(r, p-diff);
	assert(p >= diff);
	return diff;
}

static void
scared_by_monster(unit * u)
{
	int n;

	switch (old_race(u->race)) {
	case RC_FIREDRAGON:
		n = rng_int()%160 * u->number;
		break;
	case RC_DRAGON:
		n = rng_int()%400 * u->number;
		break;
	case RC_WYRM:
		n = rng_int()%1000 * u->number;
		break;
	default:
		n = rng_int()%(u->number/4+1);
	}

	if(n > 0) {
		n = lovar(n);
		n = MIN(rpeasants(u->region), n);
		if(n > 0) {
			n = scareaway(u->region, n);
			if(n > 0) {
        ADDMSG(&u->region->msgs, msg_message("fleescared", 
          "amount unit", n, u));
			}
		}
	}
}

static const char *
random_growl(void)
{
	switch(rng_int()%5) {
	case 0:
		return "Groammm";
	case 1:
		return "Roaaarrrr";
	case 2:
		return "Chhhhhhhhhh";
	case 3:
		return "Tschrrrkk";
	case 4:
		return "Schhhh";
	}
	return "";
}

extern struct attrib_type at_direction;

static order *
monster_learn(unit *u)
{
  int c = 0;
  int n;
  skill * sv;
  const struct locale * lang = u->faction->locale;
  
  /* Monster lernt ein zuf�lliges Talent aus allen, in denen es schon
   * Lerntage hat. */
  
  for (sv = u->skills; sv != u->skills + u->skill_size; ++sv) {
    if (sv->level>0) ++c;
  }
  
  if(c == 0) return NULL;
  
  n = rng_int()%c + 1;
  c = 0;
  
  for (sv = u->skills; sv != u->skills + u->skill_size; ++sv) {
    if (sv->level>0) {
      if (++c == n) {
        return create_order(K_STUDY, lang, "'%s'", skillname(sv->id, lang));
      }
    }
  }
  return NULL;
}

void
monsters_kill_peasants(unit * u)
{
  if (!is_waiting(u)) {
    if (u->race->flags & RCF_SCAREPEASANTS) {
      scared_by_monster(u);
    }
    if (u->race->flags & RCF_KILLPEASANTS) {
      eaten_by_monster(u);
    }
    if (u->race->flags & RCF_ABSORBPEASANTS) {
      absorbed_by_monster(u);
    }
  }
}

static boolean
check_overpopulated(unit *u)
{
  unit *u2;
  int c = 0;

  for(u2 = u->region->units; u2; u2 = u2->next) {
    if(u2->race == u->race && u != u2) c += u2->number;
  }

  if(c > u->race->splitsize * 2) return true;

  return false;
}

static void
recruit_dracoids(unit * dragon, int size)
{
  faction * f = dragon->faction;
  region * r = dragon->region;
  const struct item * weapon = NULL;
  order * new_order = NULL;
  unit *un = createunit(r, f, size, new_race[RC_DRACOID]);

  fset(un, UFL_ISNEW|UFL_MOVED);

  name_unit(un);
  change_money(dragon, -un->number * 50);
  equip_unit(un, get_equipment("recruited_dracoid"));

  setstatus(un, ST_FIGHT);
  for (weapon=un->items;weapon;weapon=weapon->next) {
    const weapon_type * wtype = weapon->type->rtype->wtype;
    if (wtype && (wtype->flags & WTF_MISSILE)) {
      setstatus(un, ST_BEHIND);
    }
    new_order = create_order(K_STUDY, f->locale, "'%s'",
      skillname(weapon->type->rtype->wtype->skill, f->locale));
  }

  if (new_order!=NULL) {
    addlist(&un->orders, new_order);
  }
}

static order *
plan_dragon(unit * u)
{
  attrib * ta = a_find(u->attribs, &at_targetregion);
  region * r = u->region;
  region * tr = NULL;
  boolean move = false;
  order * long_order = NULL;

  reduce_weight(u);

  if (ta==NULL) {
    move |= (r->land==0 || r->land->peasants==0); /* when no peasants, move */
    move |= (r->land==0 || r->land->money==0); /* when no money, move */
  }
  move |= chance(0.04); /* 4% chance to change your mind */

  if (u->race==new_race[RC_WYRM] && !move) {
    unit * u2;
    for (u2=r->units;u2;u2=u2->next) {
      /* wyrme sind einzelg�nger */
      if (u2==u) {
        /* we do not make room for newcomers, so we don't need to look at them */
        break;
      }
      if (u2!=u && u2->race==u->race && chance(0.5)) {
        move = true;
        break;
      }
    }
  }

  if (move) {
    /* dragon gets bored and looks for a different place to go */
    ta = set_new_dragon_target(u, u->region, DRAGON_RANGE);
  }
  else ta = a_find(u->attribs, &at_targetregion);
  if (ta!=NULL) {
    tr = (region *) ta->data.v;
    if (tr==NULL || !path_exists(u->region, tr, DRAGON_RANGE, allowed_dragon)) {
      ta = set_new_dragon_target(u, u->region, DRAGON_RANGE);
      if (ta) tr = findregion(ta->data.sa[0], ta->data.sa[1]);
    }
  }
  if (tr!=NULL) {
    assert(long_order==NULL);
    switch(old_race(u->race)) {
    case RC_FIREDRAGON:
      long_order = make_movement_order(u, tr, 4, allowed_dragon);
      break;
    case RC_DRAGON:
      long_order = make_movement_order(u, tr, 3, allowed_dragon);
      break;
    case RC_WYRM:
      long_order = make_movement_order(u, tr, 1, allowed_dragon);
      break;
    }
    if (rng_int()%100 < 15) {
      const struct locale * lang = u->faction->locale;
      /* do a growl */
      if (rname(tr, lang)) {
        addlist(&u->orders, create_order(K_MAIL, lang, "%s '%s... %s %s %s'",
          LOC(lang, parameters[P_REGION]), random_growl(), 
          u->number==1?"Ich rieche":"Wir riechen",
          "etwas in", rname(tr, u->faction->locale)));
      }
    }
  } else {
    /* we have no target. do we like it here, then? */
    long_order = get_money_for_dragon(u->region, u, income(u));
    if (long_order==NULL) {
      /* money is gone, need a new target */
      set_new_dragon_target(u, u->region, DRAGON_RANGE);
    }
    else if (u->race != new_race[RC_FIREDRAGON]) {
      /* neue dracoiden! */
      if (r->land && !fval(r->terrain, FORBIDDEN_REGION)) {
        int ra = 20 + rng_int() % 100;
        if (get_money(u) > ra * 50 + 100 && rng_int() % 100 < 50) {
          recruit_dracoids(u, ra);
        }
      }
    }
  }
  if (long_order==NULL) {
    long_order = create_order(K_STUDY, u->faction->locale, "'%s'", 
      skillname(SK_PERCEPTION, u->faction->locale));
  }
  return long_order;
}

void
plan_monsters(void)
{
  region *r;
  faction *f = get_monsters();

  if (!f) return;
  f->lastorders = turn;

  for (r = regions; r; r = r->next) {
    unit *u;
    double attack_chance = MONSTERATTACK;
    boolean attacking = false;

    for (u = r->units; u; u = u->next) {
      attrib * ta;
      order * long_order = NULL;

      /* Ab hier nur noch Befehle f�r NPC-Einheiten. */
      if (!is_monsters(u->faction)) continue;

      if (attack_chance>0.0) {
        if (chance(attack_chance)) attacking = true;
        attack_chance = 0.0;
      }

      if (u->status>ST_BEHIND) {
        setstatus(u, ST_FIGHT);
        /* all monsters fight */
      }
      /* Monster bekommen jede Runde ein paar Tage Wahrnehmung dazu */
      produceexp(u, SK_PERCEPTION, u->number);

      /* Befehle m�ssen jede Runde neu gegeben werden: */
      free_orders(&u->orders);

      if (attacking) {
        monster_attacks(u);
      }
      /* units with a plan to kill get ATTACK orders: */
      ta = a_find(u->attribs, &at_hate);
      if (ta && !is_waiting(u)) {
        unit * tu = (unit *)ta->data.v;
        if (tu && tu->region==r) {
          addlist(&u->orders, create_order(K_ATTACK, u->faction->locale, "%i", tu->no));
        } else if (tu) {
          tu = findunitg(ta->data.i, NULL);
          if (tu!=NULL) {
            long_order = make_movement_order(u, tu->region, 2, allowed_walk);
          }
        }
        else a_remove(&u->attribs, ta);
      }

      /* All monsters guard the region: */
      if (!is_waiting(u) && r->land) {
        addlist(&u->orders, create_order(K_GUARD, u->faction->locale, NULL));
      }

      /* Einheiten mit Bewegungsplan kriegen ein NACH: */
      if (long_order==NULL) {
        attrib * ta = a_find(u->attribs, &at_targetregion);
        if (ta) {
          if (u->region == (region*)ta->data.v) {
            a_remove(&u->attribs, ta);
          }
        } else if (u->race->flags & RCF_MOVERANDOM) {
          if (rng_int()%100<MOVECHANCE || check_overpopulated(u)) {
            long_order = monster_move(r, u);
          }
        }
      }

      if (long_order==NULL) {
        /* Einheiten, die Waffenlosen Kampf lernen k�nnten, lernen es um 
        * zu bewachen: */
        if (u->race->bonus[SK_WEAPONLESS] != -99) {
          if (eff_skill(u, SK_WEAPONLESS, u->region) < 1) {
            long_order = create_order(K_STUDY, f->locale, "'%s'", skillname(SK_WEAPONLESS, f->locale));
          }
        }
      }

      if (long_order==NULL) {
        /* Ab hier noch nicht generalisierte Spezialbehandlungen. */

        switch (old_race(u->race)) {
          case RC_SEASERPENT:
            long_order = create_order(K_PIRACY, f->locale, NULL);
            break;
          case RC_ALP:
            long_order = monster_seeks_target(r, u);
            break;
          case RC_FIREDRAGON:
          case RC_DRAGON:
          case RC_WYRM:
            long_order = plan_dragon(u);
            break;
          default:
            if (u->race->flags & RCF_LEARN) {
              long_order = monster_learn(u);
            }
            break;
        }
      }
      if (long_order) {
        set_order(&u->thisorder, copy_order(long_order));
        addlist(&u->orders, long_order);
      }
    }
  }
  pathfinder_cleanup();
}

static double
chaosfactor(region * r)
{
  attrib * a = a_find(r->attribs, &at_chaoscount);
  if (!a) return 0;
  return ((double) a->data.i / 1000.0);
}

static int
nrand(int start, int sub)
{
  int res = 0;

  do {
    if (rng_int() % 100 < start)
      res++;
    start -= sub;
  } while (start > 0);

  return res;
}

/** Drachen und Seeschlangen k�nnen entstehen */
void
spawn_dragons(void)
{
  region * r;
  faction * monsters = get_monsters();

  for (r = regions; r; r = r->next) {
    unit * u;

    if (fval(r->terrain, SEA_REGION) && rng_int()%10000 < 1) {
      u = createunit(r, monsters, 1, new_race[RC_SEASERPENT]);
      fset(u, UFL_ISNEW|UFL_MOVED);
      equip_unit(u, get_equipment("monster_seaserpent"));
    }

    if ((r->terrain == newterrain(T_GLACIER) || r->terrain == newterrain(T_SWAMP) || r->terrain == newterrain(T_DESERT)) && rng_int() % 10000 < (5 + 100 * chaosfactor(r))) 
    {
      if (chance(0.80)) {
        u = createunit(r, monsters, nrand(60, 20) + 1, new_race[RC_FIREDRAGON]);
      } else {
        u = createunit(r, monsters, nrand(30, 20) + 1, new_race[RC_DRAGON]);
      }
      fset(u, UFL_ISNEW|UFL_MOVED);
      equip_unit(u, get_equipment("monster_dragon"));

      if (!quiet) {
        log_printf("%d %s in %s.\n", u->number,
          LOC(default_locale, rc_name(u->race, u->number!=1)), regionname(r, NULL));
      }

      name_unit(u);

      /* add message to the region */
      ADDMSG(&r->msgs,
        msg_message("sighting", "region race number", r, u->race, u->number));
    }
  }
}

/** Untote k�nnen entstehen */
void
spawn_undead(void)
{
  region * r;
  faction * monsters = get_monsters();

  for (r = regions; r; r = r->next) {
    int unburied = deathcount(r);
    static const curse_type * ctype = NULL;

    if (!ctype) ctype = ct_find("holyground");
    if (ctype && curse_active(get_curse(r->attribs, ctype))) continue;

    /* Chance 0.1% * chaosfactor */
    if (r->land && unburied > r->land->peasants / 20 && rng_int() % 10000 < (100 + 100 * chaosfactor(r))) {
      unit * u;
      /* es ist sinnfrei, wenn irgendwo im Wald 3er-Einheiten Untote entstehen.
      * Lieber sammeln lassen, bis sie mindestens 5% der Bev�lkerung sind, und
      * dann erst auferstehen. */
      int undead = unburied / (rng_int() % 2 + 1);
      const race * rc = NULL;
      int i;
      if (r->age<100) undead = undead * r->age / 100; /* newbie-regionen kriegen weniger ab */

      if (!undead || r->age < 20) continue;

      switch(rng_int()%3) {
      case 0:
        rc = new_race[RC_SKELETON]; break;
      case 1:
        rc = new_race[RC_ZOMBIE]; break;
      default:
        rc = new_race[RC_GHOUL]; break;
      }

      u = createunit(r, monsters, undead, rc);
      fset(u, UFL_ISNEW|UFL_MOVED);
      if ((rc == new_race[RC_SKELETON] || rc == new_race[RC_ZOMBIE]) && rng_int()%10 < 4) {
        equip_unit(u, get_equipment("rising_undead"));
      }

      for (i=0;i < MAXSKILLS;i++) {
        if (rc->bonus[i] >= 1) {
          set_level(u, (skill_t)i, 1);
        }
      }
      u->hp = unit_max_hp(u) * u->number;

      deathcounts(r, -undead);
      name_unit(u);

      if (!quiet) {
        log_printf("%d %s in %s.\n", u->number,
          LOC(default_locale, rc_name(u->race, u->number!=1)), regionname(r, NULL));
      }

      {
        message * msg = msg_message("undeadrise", "region", r);
        add_message(&r->msgs, msg);
        for (u=r->units;u;u=u->next) freset(u->faction, FFL_SELECT);
        for (u=r->units;u;u=u->next) {
          if (fval(u->faction, FFL_SELECT)) continue;
          fset(u->faction, FFL_SELECT);
          add_message(&u->faction->msgs, msg);
        }
        msg_release(msg);
      }
    } else {
      int i = deathcount(r);
      if (i) {
        /* Gr�ber verwittern, 3% der Untoten finden die ewige Ruhe */
        deathcounts(r, (int)(-i*0.03));
      }
    }
  }
}
