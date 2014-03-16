#include <platform.h>
#include <kernel/types.h>
#include "tests.h"

#include <kernel/config.h>
#include <kernel/region.h>
#include <kernel/terrain.h>
#include <kernel/item.h>
#include <kernel/unit.h>
#include <kernel/race.h>
#include <kernel/faction.h>
#include <kernel/building.h>
#include <kernel/ship.h>
#include <kernel/spell.h>
#include <kernel/spellbook.h>
#include <kernel/terrain.h>
#include <util/functions.h>
#include <util/language.h>
#include <util/log.h>

#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct race *test_create_race(const char *name)
{
  race *rc = rc_add(rc_new(name));
  rc->flags |= RCF_PLAYERRACE;
  rc->maintenance = 10;
  return rc;
}

struct region *test_create_region(int x, int y, const terrain_type *terrain)
{
  region *r = new_region(x, y, NULL, 0);
  terraform_region(r, terrain);
  rsettrees(r, 0, 0);
  rsettrees(r, 1, 0);
  rsettrees(r, 2, 0);
  rsethorses(r, 0);
  rsetpeasants(r, terrain->size);
  return r;
}

struct faction *test_create_faction(const struct race *rc)
{
  faction *f = addfaction("nobody@eressea.de", NULL, rc?rc:rc_find("human"), default_locale, 0);
  return f;
}

struct unit *test_create_unit(struct faction *f, struct region *r)
{
  unit *u = create_unit(r, f, 1, f?f->race:rc_find("human"), 0, 0, 0);
  return u;
}

void test_cleanup(void)
{
  test_clear_terrains();
  test_clear_resources();
  global.functions.maintenance = NULL;
  global.functions.wage = NULL;
  default_locale = 0;
  free_locales();
  free_spells();
  free_spellbooks();
  free_gamedata();
}

terrain_type *
test_create_terrain(const char * name, unsigned int flags)
{
  terrain_type * t;

  assert(!get_terrain(name));
  t = (terrain_type*)calloc(1, sizeof(terrain_type));
  t->_name = _strdup(name);
  t->flags = flags;
  register_terrain(t);
  return t;
}

building * test_create_building(region * r, const building_type * btype)
{
  building * b = new_building(btype?btype:bt_find("castle"), r, default_locale);
  b->size = b->type->maxsize>0?b->type->maxsize:1;
  return b;
}

ship * test_create_ship(region * r, const ship_type * stype)
{
  ship * s = new_ship(stype?stype:st_find("boat"), r, default_locale);
  s->size = s->type->construction?s->type->construction->maxsize:1;
  return s;
}

ship_type * test_create_shiptype(const char ** names)
{
  ship_type * stype = (ship_type*)calloc(sizeof(ship_type), 1);
  stype->name[0] = _strdup(names[0]);
  stype->name[1] = _strdup(names[1]);
  locale_setstring(default_locale, names[0], names[0]);
  st_register(stype);
  return stype;
}

building_type * test_create_buildingtype(const char * name)
{
  building_type * btype = (building_type*)calloc(sizeof(building_type), 1);
  btype->flags = BTF_NAMECHANGE;
  btype->_name = _strdup(name);
  locale_setstring(default_locale, name, name);
  bt_register(btype);
  return btype;
}

item_type * test_create_itemtype(const char ** names) {
  resource_type * rtype;
  item_type * itype;

  rtype = new_resourcetype(names, NULL, RTF_ITEM);
  itype = new_itemtype(rtype, ITF_ANIMAL|ITF_BIG, 5000, 7000);

  return itype;
}

/** creates a small world and some stuff in it.
 * two terrains: 'plain' and 'ocean'
 * one race: 'human'
 * one ship_type: 'boat'
 * one building_type: 'castle'
 * in 0.0 and 1.0 is an island of two plains, around it is ocean.
 */
void test_create_world(void)
{
  terrain_type *t_plain, *t_ocean;
  region *island[2];
  int i;
  const char * names[] = { "horse", "horse_p", "boat", "boat_p", "iron", "iron_p", "stone", "stone_p" };

  make_locale("de");
  init_resources();
  assert(!olditemtype[I_HORSE]);

  olditemtype[I_HORSE] = test_create_itemtype(names+0);
  olditemtype[I_IRON] = test_create_itemtype(names+4);
  olditemtype[I_STONE] = test_create_itemtype(names+6);

  t_plain = test_create_terrain("plain", LAND_REGION | FOREST_REGION | WALK_INTO | CAVALRY_REGION);
  t_plain->size = 1000;
  t_ocean = test_create_terrain("ocean", SEA_REGION | SAIL_INTO | SWIM_INTO);
  t_ocean->size = 0;

  island[0] = test_create_region(0, 0, t_plain);
  island[1] = test_create_region(1, 0, t_plain);
  for (i = 0; i != 2; ++i) {
    int j;
    region *r = island[i];
    for (j = 0; j != MAXDIRECTIONS; ++j) {
      region *rn = r_connect(r, (direction_t)j);
      if (!rn) {
        rn = test_create_region(r->x + delta_x[j], r->y + delta_y[j], t_ocean);
      }
    }
  }

  test_create_race("human");

  test_create_buildingtype("castle");
  test_create_shiptype(names+2);
}

