/* vi: set ts=2:
 +-------------------+  
 |                   |  Christian Schlittchen <corwin@amber.kn-bremen.de>
 | Eressea PBEM host |  Enno Rehling <enno@eressea-pbem.de>
 | (c) 1998 - 2004   |  Katja Zedel <katze@felidae.kn-bremen.de>
 |                   |
 +-------------------+  

 This program may not be used, modified or distributed
 without prior permission by the authors of Eressea.
*/

#include <config.h>
#include "eressea.h"
#include "script.h"

#include <kernel/unit.h>
#include <kernel/race.h>

#include <util/attrib.h>

#include <lua.hpp>
#include <luabind/luabind.hpp>

#include <cstdio>
#include <cstring>

static void 
free_script(attrib * a) {
  if (a->data.v!=NULL) {
    delete a->data.v;
  }
}

attrib_type at_script = {
  "script", 
  NULL, free_script, NULL, 
  NULL, NULL, ATF_UNIQUE 
};

int 
call_script(struct unit * u)
{
  const attrib * a = a_findc(u->attribs, &at_script);
  if (a==NULL) a = a_findc(u->race->attribs, &at_script);
  if (a!=NULL && a->data.v!=NULL) {
    luabind::functor<void> * func = (luabind::functor<void> *)a->data.v;
    func->operator()(u);
  }
  return -1;
}

void 
setscript(struct attrib ** ap, void * fptr)
{
  attrib * a = a_find(*ap, &at_script);
  if (a == NULL) {
    a = a_add(ap, a_new(&at_script));
  } else if (a->data.v!=NULL) {
    delete a->data.v;
  }
  a->data.v = fptr;
}
