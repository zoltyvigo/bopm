/*
 *  Copyright (c) 2002-2003 Andy Smith
 *  Copyright (c) 2014-2017 ircd-hybrid development team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 *  USA
 */

/*
 * A Negative caching implementation for IPv4/IPv6 addresses.  The idea is that
 * every time an IP address is seen, it is checked against a patricia trie.  If
 * the IP address was previously seen and within an acceptable period of time,
 * it is not scanned again.  Otherwise, the address is scanned as normal.  If
 * it is proven to be OK (i.e.  it doesn't run an open proxy) then it is added
 * to the trie.
 */

#include "setup.h"

#include <time.h>

#include "list.h"
#include "patricia.h"
#include "negcache.h"
#include "config.h"
#include "memory.h"
#include "log.h"


extern unsigned int OPT_DEBUG;

static list_t negcache_list;
static patricia_tree_t *negcache_trie;


/*
 * Initialise the patricia trie we use for storing our negative cache.
 */
void
negcache_init(void)
{
  if (negcache_trie)
    /* Cache already exists */
    return;

  negcache_trie = patricia_new(PATRICIA_MAXBITS);
}

/*
 * Check whether an IP address is in our negative cache and was added
 * recently enough.  Return a pointer to its node if so, NULL otherwise.
 */
struct negcache_item *
negcache_check(const char *ipstr)
{
  if (OptionsItem->negcache == 0)
    return NULL;

  patricia_node_t *pnode = patricia_try_search_exact(negcache_trie, ipstr);
  if (pnode)
  {
    struct negcache_item *n = pnode->data;

    if (time(NULL) - n->seen <= OptionsItem->negcache)
      return n;
  }

  return NULL;
}

/*
 * Prepare an ASCII string representing an IPv4/IPv6 address for inserting into
 * our negative cache.
 */
void
negcache_insert(const char *ipstr)
{
  patricia_node_t *pnode = patricia_make_and_lookup(negcache_trie, ipstr);
  if (!pnode || pnode->data)
    return;  /* Malformed IP address or already added to the trie */

  struct negcache_item *n = xcalloc(sizeof(*n));
  n->seen = time(NULL);

  pnode->data = n;
  list_add(&negcache_list, node_create(pnode));
}

/*
 * Wrapper for recursive rebuild function.
 */
void
negcache_rebuild(void)
{
  node_t *node, *node_next;

  LIST_FOREACH_SAFE(node, node_next, negcache_list.head)
  {
    patricia_node_t *pnode = node->data;
    struct negcache_item *n = pnode->data;

    if (n->seen + OptionsItem->negcache < time(NULL))
    {
      if (OPT_DEBUG >= 2)
        log_printf("NEGCACHE -> Deleting expired negcache node for %s added at %lu",
                   patricia_prefix_toa(pnode->prefix, 0), n->seen);

      list_remove(&negcache_list, node);
      node_free(node);
      xfree(n);
      patricia_remove(negcache_trie, pnode);
    }
  }
}
