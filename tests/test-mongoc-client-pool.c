#include <mongoc.h>
#include "TestSuite.h"


static void
test_mongoc_client_pool_basic (void)
{
   mongoc_client_pool_t *pool;
   mongoc_client_t *client;
   mongoc_uri_t *uri;

   uri = mongoc_uri_new("mongodb://127.0.0.1?maxpoolsize=1&minpoolsize=1");
   pool = mongoc_client_pool_new(uri);
   client = mongoc_client_pool_pop(pool);
   assert(client);
   mongoc_client_pool_push(pool, client);
   mongoc_uri_destroy(uri);
   mongoc_client_pool_destroy(pool);
}


static void
test_mongoc_client_pool_try_pop (void)
{
   mongoc_client_pool_t *pool;
   mongoc_client_t *client;
   mongoc_uri_t *uri;

   uri = mongoc_uri_new("mongodb://127.0.0.1?maxpoolsize=1&minpoolsize=1");
   pool = mongoc_client_pool_new(uri);
   client = mongoc_client_pool_pop(pool);
   assert(client);
   assert(!mongoc_client_pool_try_pop(pool));
   mongoc_client_pool_push(pool, client);
   mongoc_uri_destroy(uri);
   mongoc_client_pool_destroy(pool);
}


static void
test_mongoc_client_pool_dispose_old (void)
{
   mongoc_client_pool_t *pool;
   mongoc_client_t *client;
   mongoc_uri_t *uri;
   int c;

      uri = mongoc_uri_new("mongodb://127.0.0.1?maxpoolsize=3&minpoolsize=2");
      pool = mongoc_client_pool_new(uri);

      mongoc_client_t *clients[3];

      for (c=0;c<3;c++){
         client = mongoc_client_pool_pop(pool);
         clients[c] = client ;
         assert(client);
      }
      for (c=0;c<3;c++){
         mongoc_client_pool_push(pool,clients[c]);
      }
      assert(mongoc_client_pool_get_size(pool) == 2);
      mongoc_uri_destroy(uri);
      mongoc_client_pool_destroy(pool);
   }

void
test_client_pool_install (TestSuite *suite)
{
   TestSuite_Add (suite, "/ClientPool/basic", test_mongoc_client_pool_basic);
   TestSuite_Add (suite, "/ClientPool/try_pop", test_mongoc_client_pool_try_pop);
   TestSuite_Add (suite, "/ClientPool/dispose_old", test_mongoc_client_pool_dispose_old);

}
