#include <bcon.h>
#include <mongoc.h>
#include <mongoc-collection-private.h>
#include <mongoc-find-and-modify-private.h>

#include "TestSuite.h"

#include "test-libmongoc.h"
#include "test-conveniences.h"
#include "mongoc-tests.h"
#include "mock_server/future-functions.h"
#include "mock_server/mock-server.h"

static void
auto_ismaster (mock_server_t *server,
               int32_t max_wire_version,
               int32_t max_message_size,
               int32_t max_bson_size,
               int32_t max_batch_size)
{
   char *response = bson_strdup_printf (
      "{'ismaster': true, "
      " 'maxWireVersion': %d,"
      " 'maxBsonObjectSize': %d,"
      " 'maxMessageSizeBytes': %d,"
      " 'maxWriteBatchSize': %d }",
      max_wire_version, max_bson_size, max_message_size, max_batch_size);

   mock_server_auto_ismaster (server, response);

   bson_free (response);
}

static mongoc_collection_t *
get_test_collection (mongoc_client_t *client,
                     const char      *prefix)
{
   mongoc_collection_t *ret;
   char *str;

   str = gen_collection_name (prefix);
   ret = mongoc_client_get_collection (client, "test", str);
   bson_free (str);

   return ret;
}

static void
test_find_and_modify_write_concern (int wire_version)
{
   mongoc_collection_t *collection;
   mongoc_client_t *client;
   mock_server_t *server;
   request_t *request;
   future_t *future;
   bson_error_t error;
   bson_t *update;
   bson_t doc = BSON_INITIALIZER;
   bson_t reply;
   mongoc_find_and_modify_opts_t *opts;
   mongoc_write_concern_t *write_concern;

   server = mock_server_new ();
   mock_server_run (server);

   client = mongoc_client_new_from_uri (mock_server_get_uri (server));
   ASSERT (client);

   collection = mongoc_client_get_collection (client, "test", "test_find_and_modify");

   auto_ismaster (server,
                  wire_version,          /* max_wire_version */
                  48000000,   /* max_message_size */
                  16777216,   /* max_bson_size */
                  1000);      /* max_write_batch_size */

   BSON_APPEND_INT32 (&doc, "superduper", 77889);

   update = BCON_NEW ("$set", "{",
                         "superduper", BCON_INT32 (1234),
                      "}");

   write_concern = mongoc_write_concern_new ();
   mongoc_write_concern_set_w (write_concern, 42);
   opts = mongoc_find_and_modify_opts_new ();
   mongoc_find_and_modify_opts_set_write_concern (opts, write_concern);
   mongoc_find_and_modify_opts_set_update (opts, update);
   mongoc_find_and_modify_opts_set_flags (opts, MONGOC_FIND_AND_MODIFY_RETURN_NEW);

   future = future_collection_find_and_modify_with_opts (collection,
                                               &doc,
                                               opts,
                                               &reply,
                                               &error);

   if (wire_version >= 4) {
      request = mock_server_receives_command (server, "test", MONGOC_QUERY_NONE,
       "{ 'findAndModify' : 'test_find_and_modify', "
         "'query' : { 'superduper' : 77889 },"
         "'update' : { '$set' : { 'superduper' : 1234 } },"
         "'new' : true,"
         "'writeConcern' : { 'w' : 42 } }");
   } else {
      request = mock_server_receives_command (server, "test", MONGOC_QUERY_NONE,
       "{ 'findAndModify' : 'test_find_and_modify', "
         "'query' : { 'superduper' : 77889 },"
         "'update' : { '$set' : { 'superduper' : 1234 } },"
         "'new' : true }");
   }

   mock_server_replies_simple (request, "{ 'value' : null, 'ok' : 1 }");
   ASSERT_OR_PRINT (future_get_bool (future), error);

   future_destroy (future);

   mongoc_find_and_modify_opts_destroy (opts);
   bson_destroy (&reply);
   bson_destroy (update);

   mongoc_write_concern_destroy (write_concern);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
   bson_destroy (&doc);
}

static void
test_find_and_modify_write_concern_wire_32 (void)
{
   test_find_and_modify_write_concern (4);
}

static void
test_find_and_modify_write_concern_wire_pre_32 (void)
{
   test_find_and_modify_write_concern (2);
}

static void
test_find_and_modify (void)
{
   mongoc_collection_t *collection;
   mongoc_client_t *client;
   bson_error_t error;
   bson_iter_t iter;
   bson_iter_t citer;
   bson_t *update;
   bson_t doc = BSON_INITIALIZER;
   bson_t reply;
   mongoc_find_and_modify_opts_t *opts;

   client = test_framework_client_new ();
   ASSERT (client);

   collection = get_test_collection (client, "test_find_and_modify");
   ASSERT (collection);

   BSON_APPEND_INT32 (&doc, "superduper", 77889);

   ASSERT_OR_PRINT (mongoc_collection_insert (
      collection, MONGOC_INSERT_NONE, &doc, NULL, &error), error);

   update = BCON_NEW ("$set", "{",
                         "superduper", BCON_INT32 (1234),
                      "}");

   opts = mongoc_find_and_modify_opts_new ();
   mongoc_find_and_modify_opts_set_update (opts, update);
   mongoc_find_and_modify_opts_set_fields (opts, tmp_bson ("{'superduper': 1}"));
   mongoc_find_and_modify_opts_set_sort (opts, tmp_bson ("{'superduper': 1}"));
   mongoc_find_and_modify_opts_set_flags (opts, MONGOC_FIND_AND_MODIFY_RETURN_NEW);

   ASSERT_OR_PRINT (mongoc_collection_find_and_modify_with_opts (collection,
                                                                 &doc,
                                                                 opts,
                                                                 &reply,
                                                                 &error), error);

   assert (bson_iter_init_find (&iter, &reply, "value"));
   assert (BSON_ITER_HOLDS_DOCUMENT (&iter));
   assert (bson_iter_recurse (&iter, &citer));
   assert (bson_iter_find (&citer, "superduper"));
   assert (BSON_ITER_HOLDS_INT32 (&citer));
   assert (bson_iter_int32 (&citer) == 1234);

   assert (bson_iter_init_find (&iter, &reply, "lastErrorObject"));
   assert (BSON_ITER_HOLDS_DOCUMENT (&iter));
   assert (bson_iter_recurse (&iter, &citer));
   assert (bson_iter_find (&citer, "updatedExisting"));
   assert (BSON_ITER_HOLDS_BOOL (&citer));
   assert (bson_iter_bool (&citer));

   bson_destroy (&reply);
   bson_destroy (update);

   ASSERT_OR_PRINT (mongoc_collection_drop (collection, &error), error);

   mongoc_find_and_modify_opts_destroy (opts);
   mongoc_collection_destroy (collection);
   mongoc_client_destroy (client);
   bson_destroy (&doc);
}




void
test_find_and_modify_install (TestSuite *suite)
{
   TestSuite_Add (suite, "/find_and_modify/find_and_modify", test_find_and_modify);
   TestSuite_Add (suite, "/find_and_modify/find_and_modify/write_concern",
                  test_find_and_modify_write_concern_wire_32);
   TestSuite_Add (suite, "/find_and_modify/find_and_modify/write_concern_pre_32",
                  test_find_and_modify_write_concern_wire_pre_32);
}
