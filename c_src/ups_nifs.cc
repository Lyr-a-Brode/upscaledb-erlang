/*
 * Copyright (C) 2005-2017 Christoph Rupp (chris@crupp.de).
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <stdio.h>

#include "erl_nif_compat.h"
#include "ups/upscaledb.h"
#include "ups/upscaledb_uqi.h"

ERL_NIF_TERM g_atom_ok;
ERL_NIF_TERM g_atom_error;
ERL_NIF_TERM g_atom_key_not_found;
ERL_NIF_TERM g_atom_duplicate_key;
ErlNifResourceType *g_ups_env_resource;
ErlNifResourceType *g_ups_db_resource;
ErlNifResourceType *g_ups_txn_resource;
ErlNifResourceType *g_ups_cursor_resource;
ErlNifResourceType *g_ups_result_resource;

struct env_wrapper {
  ups_env_t *env;
  bool is_closed;
};

struct db_wrapper {
  ups_db_t *db;
  bool is_closed;
};

struct txn_wrapper {
  ups_txn_t *txn;
  bool is_closed;
};

struct cursor_wrapper {
  ups_cursor_t *cursor;
  bool is_closed;
};

struct result_wrapper {
  uqi_result_t *result;
  bool is_closed;
};

#define MAX_PARAMETERS   64
#define MAX_STRING     2048

static ERL_NIF_TERM
status_to_atom(ErlNifEnv *env, ups_status_t st)
{
  switch (st) {
    case UPS_SUCCESS:
      return (g_atom_ok);
    case UPS_INV_RECORD_SIZE:
      return (enif_make_atom(env, "inv_record_size"));
    case UPS_INV_KEY_SIZE:
      return (enif_make_atom(env, "inv_key_size"));
    case UPS_INV_PAGE_SIZE:
      return (enif_make_atom(env, "inv_page_size"));
    case UPS_OUT_OF_MEMORY:
      return (enif_make_atom(env, "out_of_memory"));
    case UPS_INV_PARAMETER:
      return (enif_make_atom(env, "inv_parameter"));
    case UPS_INV_FILE_HEADER:
      return (enif_make_atom(env, "inv_file_header"));
    case UPS_INV_FILE_VERSION:
      return (enif_make_atom(env, "inv_file_version"));
    case UPS_KEY_NOT_FOUND:
      return (g_atom_key_not_found);
    case UPS_DUPLICATE_KEY:
      return (g_atom_duplicate_key);
    case UPS_INTEGRITY_VIOLATED:
      return (enif_make_atom(env, "integrity_violated"));
    case UPS_INTERNAL_ERROR:
      return (enif_make_atom(env, "internal_error"));
    case UPS_WRITE_PROTECTED:
      return (enif_make_atom(env, "write_protected"));
    case UPS_BLOB_NOT_FOUND:
      return (enif_make_atom(env, "blob_not_found"));
    case UPS_IO_ERROR:
      return (enif_make_atom(env, "io_error"));
    case UPS_NOT_IMPLEMENTED:
      return (enif_make_atom(env, "not_implemented"));
    case UPS_FILE_NOT_FOUND:
      return (enif_make_atom(env, "file_not_found"));
    case UPS_WOULD_BLOCK:
      return (enif_make_atom(env, "would_block"));
    case UPS_NOT_READY:
      return (enif_make_atom(env, "not_ready"));
    case UPS_LIMITS_REACHED:
      return (enif_make_atom(env, "limits_reached"));
    case UPS_ALREADY_INITIALIZED:
      return (enif_make_atom(env, "already_initialized"));
    case UPS_NEED_RECOVERY:
      return (enif_make_atom(env, "need_recovery"));
    case UPS_CURSOR_STILL_OPEN:
      return (enif_make_atom(env, "cursor_still_open"));
    case UPS_FILTER_NOT_FOUND:
      return (enif_make_atom(env, "filter_not_found"));
    case UPS_TXN_CONFLICT:
      return (enif_make_atom(env, "txn_conflict"));
    case UPS_KEY_ERASED_IN_TXN:
      return (enif_make_atom(env, "key_erased_in_txn"));
    case UPS_TXN_STILL_OPEN:
      return (enif_make_atom(env, "txn_still_open"));
    case UPS_CURSOR_IS_NIL:
      return (enif_make_atom(env, "cursor_is_nil"));
    case UPS_DATABASE_NOT_FOUND:
      return (enif_make_atom(env, "database_not_found"));
    case UPS_DATABASE_ALREADY_EXISTS:
      return (enif_make_atom(env, "database_already_exists"));
    case UPS_DATABASE_ALREADY_OPEN:
      return (enif_make_atom(env, "database_already_open"));
    case UPS_ENVIRONMENT_ALREADY_OPEN:
      return (enif_make_atom(env, "environment_already_open"));
    case UPS_LOG_INV_FILE_HEADER:
      return (enif_make_atom(env, "log_inv_file_header"));
    case UPS_NETWORK_ERROR:
      return (enif_make_atom(env, "network_error"));
    default:
      return (g_atom_error);
  }
  return (0);
}

static int
get_parameters(ErlNifEnv *env, ERL_NIF_TERM term, ups_parameter_t *parameters,
            char *logdir_buf, char *aeskey_buf)
{
  unsigned i = 0;
  ERL_NIF_TERM cell;

  if (!enif_is_list(env, term))
    return (0);

  while (enif_get_list_cell(env, term, &cell, &term)) {
    int arity;
    char atom[128];
    const ERL_NIF_TERM *array;

    if (!enif_get_tuple(env, cell, &arity, &array) || arity != 2)
      return (0);
    if (enif_get_atom(env, array[0], &atom[0], sizeof(atom),
              ERL_NIF_LATIN1) <= 0)
      return (0);

    if (!strcmp(atom, "journal_compression")) {
      parameters[i].name = UPS_PARAM_JOURNAL_COMPRESSION;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "record_compression")) {
      parameters[i].name = UPS_PARAM_RECORD_COMPRESSION;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "key_compression")) {
      parameters[i].name = UPS_PARAM_KEY_COMPRESSION;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "cache_size")) {
      parameters[i].name = UPS_PARAM_CACHE_SIZE;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "page_size")) {
      parameters[i].name = UPS_PARAM_PAGE_SIZE;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "file_size_limit")) {
      parameters[i].name = UPS_PARAM_FILE_SIZE_LIMIT;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "key_size")) {
      parameters[i].name = UPS_PARAM_KEY_SIZE;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "record_size")) {
      parameters[i].name = UPS_PARAM_RECORD_SIZE;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "max_databases")) {
      parameters[i].name = UPS_PARAM_MAX_DATABASES;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "key_type")) {
      parameters[i].name = UPS_PARAM_KEY_TYPE;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "record_type")) {
      parameters[i].name = UPS_PARAM_RECORD_TYPE;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }
    if (!strcmp(atom, "log_directory")) {
      parameters[i].name = UPS_PARAM_LOG_DIRECTORY;
      if (enif_get_string(env, array[1], logdir_buf, MAX_STRING,
              ERL_NIF_LATIN1) <= 0)
        return (0);
      parameters[i].value = *(uint64_t *)logdir_buf;
      i++;
      continue;
    }
    if (!strcmp(atom, "encryption_key")) {
      parameters[i].name = UPS_PARAM_ENCRYPTION_KEY;
      if (enif_get_string(env, array[1], aeskey_buf, MAX_STRING,
              ERL_NIF_LATIN1) <= 0)
        return (0);
      parameters[i].value = *(uint64_t *)aeskey_buf;
      i++;
      continue;
    }
    if (!strcmp(atom, "network_timeout_sec")) {
      parameters[i].name = UPS_PARAM_NETWORK_TIMEOUT_SEC;
      if (!enif_get_uint64(env, array[1], &parameters[i].value))
        return (0);
      i++;
      continue;
    }

    // the following parameters are read-only; we do not need to
    // extract a value
    if (!strcmp(atom, "flags")) {
      parameters[i].name = UPS_PARAM_FLAGS;
      i++;
      continue;
    }
    if (!strcmp(atom, "filemode")) {
      parameters[i].name = UPS_PARAM_FILEMODE;
      i++;
      continue;
    }
    if (!strcmp(atom, "filename")) {
      parameters[i].name = UPS_PARAM_FILENAME;
      i++;
      continue;
    }
    if (!strcmp(atom, "database_name")) {
      parameters[i].name = UPS_PARAM_DATABASE_NAME;
      i++;
      continue;
    }
    if (!strcmp(atom, "max_keys_per_page")) {
      parameters[i].name = UPS_PARAM_MAX_KEYS_PER_PAGE;
      i++;
      continue;
    }

    // still here? that's an error
    return (0);
  }

  return (1);
}

ERL_NIF_TERM
ups_nifs_strerror(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ups_status_t st;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_int(env, argv[0], &st))
    return (enif_make_badarg(env));

  return (enif_make_string(env, ups_strerror(st), ERL_NIF_LATIN1));
}

ERL_NIF_TERM
ups_nifs_env_create(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ups_env_t *henv;
  uint32_t flags = 0;
  uint32_t mode = 0;
  char filename[MAX_STRING];
  ups_parameter_t params[MAX_PARAMETERS] = {{0, 0}};
  char logdir_buf[MAX_STRING];
  char aesdir_buf[MAX_STRING];

  if (argc != 4)
    return (enif_make_badarg(env));
  if (enif_get_string(env, argv[0], &filename[0], sizeof(filename),
              ERL_NIF_LATIN1) <= 0)
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[1], &flags))
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[2], &mode))
    return (enif_make_badarg(env));
  if (!get_parameters(env, argv[3], &params[0],
              &logdir_buf[0], &aesdir_buf[0]))
    return (enif_make_badarg(env));

  ups_status_t st = ups_env_create(&henv, filename, flags, mode, &params[0]);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  env_wrapper *ewrapper = (env_wrapper *)enif_alloc_resource_compat(env,
                                g_ups_env_resource, sizeof(*ewrapper));
  ewrapper->env = henv;
  ewrapper->is_closed = false;
  ERL_NIF_TERM result = enif_make_resource(env, ewrapper);
  enif_release_resource_compat(env, ewrapper);

  return (enif_make_tuple2(env, g_atom_ok, result));
}

ERL_NIF_TERM
ups_nifs_env_open(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ups_env_t *henv;
  uint32_t flags;
  char filename[MAX_STRING];
  ups_parameter_t params[MAX_PARAMETERS] = {{0, 0}};
  char logdir_buf[MAX_STRING];
  char aesdir_buf[MAX_STRING];

  if (argc != 3)
    return (enif_make_badarg(env));
  if (enif_get_string(env, argv[0], &filename[0], sizeof(filename),
              ERL_NIF_LATIN1) <= 0)
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[1], &flags))
    return (enif_make_badarg(env));
  if (!get_parameters(env, argv[2], &params[0],
              &logdir_buf[0], &aesdir_buf[0]))
    return (enif_make_badarg(env));

  ups_status_t st = ups_env_open(&henv, filename, flags, &params[0]);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  env_wrapper *ewrapper = (env_wrapper *)enif_alloc_resource_compat(env,
                                g_ups_env_resource, sizeof(*ewrapper));
  ewrapper->env = henv;
  ewrapper->is_closed = false;
  ERL_NIF_TERM result = enif_make_resource(env, ewrapper);
  enif_release_resource_compat(env, ewrapper);

  return (enif_make_tuple2(env, g_atom_ok, result));
}

ERL_NIF_TERM
ups_nifs_env_create_db(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ups_db_t *hdb;
  uint32_t dbname;
  uint32_t flags;
  ups_parameter_t params[MAX_PARAMETERS] = {{0, 0}};
  char logdir_buf[MAX_STRING];
  char aesdir_buf[MAX_STRING];
  env_wrapper *ewrapper;

  if (argc != 4)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_env_resource, (void **)&ewrapper)
          || ewrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[1], &dbname))
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[2], &flags))
    return (enif_make_badarg(env));
  if (!get_parameters(env, argv[3], &params[0],
              &logdir_buf[0], &aesdir_buf[0]))
    return (enif_make_badarg(env));

  ups_status_t st = ups_env_create_db(ewrapper->env, &hdb, dbname, flags,
          &params[0]);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  db_wrapper *dbwrapper = (db_wrapper *)enif_alloc_resource_compat(env,
                                g_ups_db_resource, sizeof(*dbwrapper));
  dbwrapper->db = hdb;
  dbwrapper->is_closed = false;
  ERL_NIF_TERM result = enif_make_resource(env, dbwrapper);
  enif_release_resource_compat(env, dbwrapper);

  return (enif_make_tuple2(env, g_atom_ok, result));
}

ERL_NIF_TERM
ups_nifs_env_open_db(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ups_db_t *hdb;
  uint32_t dbname;
  uint32_t flags;
  ups_parameter_t params[MAX_PARAMETERS] = {{0, 0}};
  char logdir_buf[MAX_STRING];
  char aesdir_buf[MAX_STRING];
  env_wrapper *ewrapper;

  if (argc != 4)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_env_resource, (void **)&ewrapper)
          || ewrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[1], &dbname))
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[2], &flags))
    return (enif_make_badarg(env));
  if (!get_parameters(env, argv[3], &params[0],
              &logdir_buf[0], &aesdir_buf[0]))
    return (enif_make_badarg(env));

  ups_status_t st = ups_env_open_db(ewrapper->env, &hdb, dbname, flags,
          &params[0]);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  db_wrapper *dbwrapper = (db_wrapper *)enif_alloc_resource_compat(env,
                                g_ups_db_resource, sizeof(*dbwrapper));
  dbwrapper->db = hdb;
  dbwrapper->is_closed = false;
  ERL_NIF_TERM result = enif_make_resource(env, dbwrapper);
  enif_release_resource_compat(env, dbwrapper);

  return (enif_make_tuple2(env, g_atom_ok, result));
}

ERL_NIF_TERM
ups_nifs_env_erase_db(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  uint32_t dbname;
  env_wrapper *ewrapper;

  if (argc != 2)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_env_resource, (void **)&ewrapper)
          || ewrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[1], &dbname))
    return (enif_make_badarg(env));

  ups_status_t st = ups_env_erase_db(ewrapper->env, dbname, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_uqi_select_range(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  char query[1024];
  env_wrapper *ewrapper;
  cursor_wrapper *cwrapper1;
  cursor_wrapper *cwrapper2;
  uqi_result_t *result;

  if (argc != 4)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_env_resource, (void **)&ewrapper)
          || ewrapper->is_closed)
    return (enif_make_badarg(env));
  if (enif_get_string(env, argv[1], query, sizeof(query), ERL_NIF_LATIN1) <= 0)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[2], g_ups_cursor_resource,
                          (void **)&cwrapper1)
          || cwrapper1->is_closed)
    cwrapper1 = 0;
  if (!enif_get_resource(env, argv[3], g_ups_cursor_resource,
                          (void **)&cwrapper2)
          || cwrapper2->is_closed)
    cwrapper2 = 0;

  ups_status_t st = uqi_select_range(ewrapper->env, query,
                 cwrapper1 ? cwrapper1->cursor : 0,
                 cwrapper2 ? cwrapper2->cursor : 0,
                 &result);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  result_wrapper *rwrapper = (result_wrapper *)enif_alloc_resource_compat(env,
                                g_ups_result_resource, sizeof(*rwrapper));
  rwrapper->result = result;
  rwrapper->is_closed = false;
  ERL_NIF_TERM retval = enif_make_resource(env, rwrapper);
  enif_release_resource_compat(env, rwrapper);
  return (enif_make_tuple2(env, g_atom_ok, retval));
}

ERL_NIF_TERM
ups_nifs_uqi_result_get_row_count(ErlNifEnv *env, int argc,
                const ERL_NIF_TERM argv[])
{
  result_wrapper *rwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_result_resource,
                          (void **)&rwrapper)
          || rwrapper->is_closed)
    return (enif_make_badarg(env));

  uint32_t rc = uqi_result_get_row_count(rwrapper->result);
  return (enif_make_tuple2(env, g_atom_ok, enif_make_int(env, (int)rc)));
}

ERL_NIF_TERM
ups_nifs_uqi_result_get_key_type(ErlNifEnv *env, int argc,
                const ERL_NIF_TERM argv[])
{
  result_wrapper *rwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_result_resource,
                          (void **)&rwrapper)
          || rwrapper->is_closed)
    return (enif_make_badarg(env));

  uint32_t rc = uqi_result_get_key_type(rwrapper->result);
  return (enif_make_tuple2(env, g_atom_ok, enif_make_int(env, (int)rc)));
}

ERL_NIF_TERM
ups_nifs_uqi_result_get_record_type(ErlNifEnv *env, int argc,
                const ERL_NIF_TERM argv[])
{
  result_wrapper *rwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_result_resource,
                          (void **)&rwrapper)
          || rwrapper->is_closed)
    return (enif_make_badarg(env));

  uint32_t rc = uqi_result_get_record_type(rwrapper->result);
  return (enif_make_tuple2(env, g_atom_ok, enif_make_int(env, (int)rc)));
}

ERL_NIF_TERM
ups_nifs_uqi_result_get_key(ErlNifEnv *env, int argc,
                const ERL_NIF_TERM argv[])
{
  int row;
  result_wrapper *rwrapper;

  if (argc != 2)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_result_resource,
                          (void **)&rwrapper)
          || rwrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_get_int(env, argv[1], &row))
    return (enif_make_badarg(env));

  ups_key_t key = {0};
  uqi_result_get_key(rwrapper->result, row, &key);

  ErlNifBinary bin;
  if (!enif_alloc_binary(key.size, &bin))
    return (enif_make_tuple2(env, g_atom_error,
                status_to_atom(env, UPS_OUT_OF_MEMORY)));
  memcpy(bin.data, key.data, key.size);
  bin.size = key.size;

  return (enif_make_tuple2(env, g_atom_ok, enif_make_binary(env, &bin)));
}

ERL_NIF_TERM
ups_nifs_uqi_result_get_record(ErlNifEnv *env, int argc,
                const ERL_NIF_TERM argv[])
{
  int row;
  result_wrapper *rwrapper;

  if (argc != 2)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_result_resource,
                          (void **)&rwrapper)
          || rwrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_get_int(env, argv[1], &row))
    return (enif_make_badarg(env));

  ups_record_t record = {0};
  uqi_result_get_record(rwrapper->result, row, &record);

  ErlNifBinary bin;
  if (!enif_alloc_binary(record.size, &bin))
    return (enif_make_tuple2(env, g_atom_error,
                status_to_atom(env, UPS_OUT_OF_MEMORY)));
  memcpy(bin.data, record.data, record.size);
  bin.size = record.size;

  return (enif_make_tuple2(env, g_atom_ok, enif_make_binary(env, &bin)));
}

ERL_NIF_TERM
ups_nifs_uqi_result_close(ErlNifEnv *env, int argc,
                const ERL_NIF_TERM argv[])
{
  result_wrapper *rwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_result_resource,
                          (void **)&rwrapper)
          || rwrapper->is_closed)
    return (enif_make_badarg(env));

  uqi_result_close(rwrapper->result);
  rwrapper->is_closed = true;

  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_env_rename_db(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  uint32_t oldname;
  uint32_t newname;
  env_wrapper *ewrapper;

  if (argc != 3)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_env_resource, (void **)&ewrapper)
          || ewrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[1], &oldname))
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[2], &newname))
    return (enif_make_badarg(env));

  ups_status_t st = ups_env_rename_db(ewrapper->env, oldname, newname, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_db_insert(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ups_key_t key = {0};
  ups_record_t rec = {0};
  uint32_t flags;
  ErlNifBinary binkey;
  ErlNifBinary binrec;
  db_wrapper *dwrapper;
  txn_wrapper *twrapper;

  if (argc != 5)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_db_resource, (void **)&dwrapper)
          || dwrapper->is_closed)
    return (enif_make_badarg(env));
  // arg[1] is the Transaction!
  if (!enif_get_resource(env, argv[1], g_ups_txn_resource, (void **)&twrapper))
    twrapper = 0;
  if (twrapper != 0 && twrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_inspect_binary(env, argv[2], &binkey))
    return (enif_make_badarg(env));
  if (!enif_inspect_binary(env, argv[3], &binrec))
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[4], &flags))
    return (enif_make_badarg(env));

  key.size = binkey.size;
  key.data = binkey.size ? binkey.data : 0;
  rec.size = binrec.size;
  rec.data = binrec.size ? binrec.data : 0;

  ups_status_t st = ups_db_insert(dwrapper->db, twrapper ? twrapper->txn : 0,
                                &key, &rec, flags);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_db_erase(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ups_key_t key = {0};
  ErlNifBinary binkey;
  db_wrapper *dwrapper;
  txn_wrapper *twrapper;

  if (argc != 3)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_db_resource, (void **)&dwrapper)
          || dwrapper->is_closed)
    return (enif_make_badarg(env));
  // arg[1] is the Transaction!
  if (!enif_get_resource(env, argv[1], g_ups_txn_resource, (void **)&twrapper))
    twrapper = 0;
  if (twrapper != 0 && twrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_inspect_binary(env, argv[2], &binkey))
    return (enif_make_badarg(env));

  key.data = binkey.data;
  key.size = binkey.size;

  ups_status_t st = ups_db_erase(dwrapper->db, twrapper ? twrapper->txn : 0,
                        &key, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_db_find(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ups_key_t key = {0};
  ups_record_t rec = {0};
  ErlNifBinary binkey;
  ErlNifBinary binrec;
  db_wrapper *dwrapper;
  txn_wrapper *twrapper;

  if (argc != 3)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_db_resource, (void **)&dwrapper)
          || dwrapper->is_closed)
    return (enif_make_badarg(env));
  // argv[1] is the Transaction!
  if (!enif_get_resource(env, argv[1], g_ups_txn_resource, (void **)&twrapper))
    twrapper = 0;
  if (twrapper != 0 && twrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_inspect_binary(env, argv[2], &binkey))
    return (enif_make_badarg(env));

  key.data = binkey.data;
  key.size = binkey.size;

  ups_status_t st = ups_db_find(dwrapper->db, twrapper ? twrapper->txn : 0,
                                &key, &rec, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  if (!enif_alloc_binary(rec.size, &binrec))
    return (enif_make_tuple2(env, g_atom_error,
                status_to_atom(env, UPS_OUT_OF_MEMORY)));

  memcpy(binrec.data, rec.data, rec.size);
  binrec.size = rec.size;

  return (enif_make_tuple2(env, g_atom_ok, enif_make_binary(env, &binrec)));
}

ERL_NIF_TERM
ups_nifs_db_find_flags(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  ups_key_t key = {0};
  ups_record_t rec = {0};
  uint32_t flags = 0;
  ErlNifBinary binkey;
  ErlNifBinary binrec;
  db_wrapper *dwrapper;
  txn_wrapper *twrapper;

  if (argc != 4)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_db_resource, (void **)&dwrapper)
          || dwrapper->is_closed)
    return (enif_make_badarg(env));
  // argv[1] is the Transaction!
  if (!enif_get_resource(env, argv[1], g_ups_txn_resource, (void **)&twrapper))
    twrapper = 0;
  if (twrapper != 0 && twrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_inspect_binary(env, argv[2], &binkey))
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[3], &flags))
    return (enif_make_badarg(env));

  key.data = binkey.data;
  key.size = binkey.size;

  ups_status_t st = ups_db_find(dwrapper->db, twrapper ? twrapper->txn : 0,
                                &key, &rec, flags);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  if (!enif_alloc_binary(rec.size, &binrec))
    return (enif_make_tuple2(env, g_atom_error,
                status_to_atom(env, UPS_OUT_OF_MEMORY)));
  if (flags) {
    if (!enif_alloc_binary(key.size, &binkey))
      return (enif_make_tuple2(env, g_atom_error,
                  status_to_atom(env, UPS_OUT_OF_MEMORY)));
    memcpy(binkey.data, key.data, key.size);
  }

  memcpy(binrec.data, rec.data, rec.size);
  binrec.size = rec.size;

  return (enif_make_tuple3(env, g_atom_ok,
              enif_make_binary(env, &binkey),
              enif_make_binary(env, &binrec)));
}

ERL_NIF_TERM
ups_nifs_txn_begin(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  env_wrapper *ewrapper;
  uint32_t flags;

  if (argc != 2)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_env_resource, (void **)&ewrapper)
          || ewrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[1], &flags))
    return (enif_make_badarg(env));

  ups_txn_t *txn;
  ups_status_t st = ups_txn_begin(&txn, ewrapper->env, 0, 0, flags);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  txn_wrapper *twrapper = (txn_wrapper *)enif_alloc_resource_compat(env,
                                g_ups_txn_resource, sizeof(*twrapper));
  twrapper->txn = txn;
  twrapper->is_closed = false;
  ERL_NIF_TERM result = enif_make_resource(env, twrapper);
  enif_release_resource_compat(env, twrapper);

  return (enif_make_tuple2(env, g_atom_ok, result));
}

ERL_NIF_TERM
ups_nifs_txn_abort(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  txn_wrapper *twrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_txn_resource, (void **)&twrapper)
          || twrapper->is_closed)
    return (enif_make_badarg(env));

  ups_status_t st = ups_txn_abort(twrapper->txn, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  twrapper->is_closed = true;
  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_txn_commit(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  txn_wrapper *twrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_txn_resource, (void **)&twrapper)
          || twrapper->is_closed)
    return (enif_make_badarg(env));

  ups_status_t st = ups_txn_commit(twrapper->txn, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  twrapper->is_closed = true;
  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_db_close(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  db_wrapper *dwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_db_resource, (void **)&dwrapper)
          || dwrapper->is_closed)
    return (enif_make_badarg(env));

  ups_status_t st = ups_db_close(dwrapper->db, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  dwrapper->is_closed = true;
  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_env_close(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  env_wrapper *ewrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_env_resource, (void **)&ewrapper)
          || ewrapper->is_closed)
    return (enif_make_badarg(env));

  ups_status_t st = ups_env_close(ewrapper->env, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  ewrapper->is_closed = true;
  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_cursor_create(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  db_wrapper *dwrapper;
  txn_wrapper *twrapper;

  if (argc != 2)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_db_resource, (void **)&dwrapper)
          || dwrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[1], g_ups_txn_resource, (void **)&twrapper))
    twrapper = 0;
  if (twrapper && twrapper->is_closed)
    return (enif_make_badarg(env));

  ups_cursor_t *cursor;
  ups_status_t st = ups_cursor_create(&cursor, dwrapper->db,
                                    twrapper ? twrapper ->txn : 0, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  cursor_wrapper *cwrapper = (cursor_wrapper *)enif_alloc_resource_compat(env,
                                g_ups_cursor_resource, sizeof(*cwrapper));
  cwrapper->cursor = cursor;
  cwrapper->is_closed = false;
  ERL_NIF_TERM result = enif_make_resource(env, cwrapper);
  enif_release_resource_compat(env, cwrapper);

  return (enif_make_tuple2(env, g_atom_ok, result));
}

ERL_NIF_TERM
ups_nifs_cursor_clone(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  cursor_wrapper *cwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_cursor_resource,
              (void **)&cwrapper)
          || cwrapper->is_closed)
    return (enif_make_badarg(env));

  ups_cursor_t *clone;
  ups_status_t st = ups_cursor_clone(cwrapper->cursor, &clone);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  cursor_wrapper *c2wrapper = (cursor_wrapper *)enif_alloc_resource_compat(env,
                                g_ups_cursor_resource, sizeof(*c2wrapper));
  c2wrapper->cursor = clone;
  c2wrapper->is_closed = false;
  ERL_NIF_TERM result = enif_make_resource(env, c2wrapper);
  enif_release_resource_compat(env, c2wrapper);

  return (enif_make_tuple2(env, g_atom_ok, result));
}

ERL_NIF_TERM
ups_nifs_cursor_move(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  cursor_wrapper *cwrapper;
  uint32_t flags;

  if (argc != 2)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_cursor_resource,
              (void **)&cwrapper)
          || cwrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[1], &flags))
    return (enif_make_badarg(env));

  ups_key_t key = {0};
  ups_record_t rec = {0};
  ups_status_t st = ups_cursor_move(cwrapper->cursor, &key, &rec, flags);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  ErlNifBinary binkey;
  if (!enif_alloc_binary(key.size, &binkey))
    return (enif_make_tuple2(env, g_atom_error,
                status_to_atom(env, UPS_OUT_OF_MEMORY)));
  memcpy(binkey.data, key.data, key.size);
  binkey.size = key.size;

  ErlNifBinary binrec;
  if (!enif_alloc_binary(rec.size, &binrec))
    return (enif_make_tuple2(env, g_atom_error,
                status_to_atom(env, UPS_OUT_OF_MEMORY)));
  memcpy(binrec.data, rec.data, rec.size);
  binrec.size = rec.size;

  return (enif_make_tuple3(env, g_atom_ok,
              enif_make_binary(env, &binkey),
              enif_make_binary(env, &binrec)));
}

ERL_NIF_TERM
ups_nifs_cursor_overwrite(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  cursor_wrapper *cwrapper;
  ErlNifBinary binrec;

  if (argc != 2)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_cursor_resource,
              (void **)&cwrapper)
          || cwrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_inspect_binary(env, argv[1], &binrec))
    return (enif_make_badarg(env));

  ups_record_t rec = {0};
  rec.data = binrec.data;
  rec.size = binrec.size;

  ups_status_t st = ups_cursor_overwrite(cwrapper->cursor, &rec, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_cursor_find(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  cursor_wrapper *cwrapper;
  ErlNifBinary binkey;

  if (argc != 2)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_cursor_resource,
              (void **)&cwrapper)
          || cwrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_inspect_binary(env, argv[1], &binkey))
    return (enif_make_badarg(env));

  ups_record_t rec = {0};
  ups_key_t key = {0};
  key.data = binkey.data;
  key.size = binkey.size;

  ups_status_t st = ups_cursor_find(cwrapper->cursor, &key, &rec, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  ErlNifBinary binrec;
  if (!enif_alloc_binary(rec.size, &binrec))
    return (enif_make_tuple2(env, g_atom_error,
                status_to_atom(env, UPS_OUT_OF_MEMORY)));
  memcpy(binrec.data, rec.data, rec.size);
  binrec.size = rec.size;

  return (enif_make_tuple2(env, g_atom_ok, enif_make_binary(env, &binrec)));
}

ERL_NIF_TERM
ups_nifs_cursor_insert(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  cursor_wrapper *cwrapper;
  ErlNifBinary binkey;
  ErlNifBinary binrec;
  uint32_t flags;

  if (argc != 4)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_cursor_resource,
              (void **)&cwrapper)
          || cwrapper->is_closed)
    return (enif_make_badarg(env));
  if (!enif_inspect_binary(env, argv[1], &binkey))
    return (enif_make_badarg(env));
  if (!enif_inspect_binary(env, argv[2], &binrec))
    return (enif_make_badarg(env));
  if (!enif_get_uint(env, argv[3], &flags))
    return (enif_make_badarg(env));

  ups_key_t key = {0};
  key.data = binkey.data;
  key.size = binkey.size;
  ups_record_t rec = {0};
  rec.data = binrec.data;
  rec.size = binrec.size;

  ups_status_t st = ups_cursor_insert(cwrapper->cursor, &key, &rec, flags);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_cursor_erase(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  cursor_wrapper *cwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_cursor_resource,
              (void **)&cwrapper)
          || cwrapper->is_closed)
    return (enif_make_badarg(env));

  ups_status_t st = ups_cursor_erase(cwrapper->cursor, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  return (g_atom_ok);
}

ERL_NIF_TERM
ups_nifs_cursor_get_duplicate_count(ErlNifEnv *env, int argc,
        const ERL_NIF_TERM argv[])
{
  cursor_wrapper *cwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_cursor_resource,
              (void **)&cwrapper)
          || cwrapper->is_closed)
    return (enif_make_badarg(env));

  uint32_t count;
  ups_status_t st = ups_cursor_get_duplicate_count(cwrapper->cursor, &count, 0);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  return (enif_make_tuple2(env, g_atom_ok, enif_make_int(env, (int)count)));
}

ERL_NIF_TERM
ups_nifs_cursor_get_record_size(ErlNifEnv *env, int argc,
        const ERL_NIF_TERM argv[])
{
  cursor_wrapper *cwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_cursor_resource,
              (void **)&cwrapper)
          || cwrapper->is_closed)
    return (enif_make_badarg(env));

  uint32_t size;
  ups_status_t st = ups_cursor_get_record_size(cwrapper->cursor, &size);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  return (enif_make_tuple2(env, g_atom_ok, enif_make_int64(env, (int)size)));
}

ERL_NIF_TERM
ups_nifs_cursor_close(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
  cursor_wrapper *cwrapper;

  if (argc != 1)
    return (enif_make_badarg(env));
  if (!enif_get_resource(env, argv[0], g_ups_cursor_resource,
              (void **)&cwrapper)
          || cwrapper->is_closed)
    return (enif_make_badarg(env));

  ups_status_t st = ups_cursor_close(cwrapper->cursor);
  if (st)
    return (enif_make_tuple2(env, g_atom_error, status_to_atom(env, st)));

  cwrapper->is_closed = true;
  return (g_atom_ok);
}

static void
env_resource_cleanup(ErlNifEnv *env, void *arg)
{
  env_wrapper *ewrapper = (env_wrapper *)arg;
  if (!ewrapper->is_closed)
    (void)ups_env_close(ewrapper->env, 0);
  ewrapper->is_closed = true;
}

static void
db_resource_cleanup(ErlNifEnv *env, void *arg)
{
  db_wrapper *dwrapper = (db_wrapper *)arg;
  if (!dwrapper->is_closed)
    (void)ups_db_close(dwrapper->db, 0);
  dwrapper->is_closed = true;
}

static void
txn_resource_cleanup(ErlNifEnv *env, void *arg)
{
  txn_wrapper *twrapper = (txn_wrapper *)arg;
  if (!twrapper->is_closed)
    (void)ups_txn_abort(twrapper->txn, 0);
  twrapper->is_closed = true;
}

static void
cursor_resource_cleanup(ErlNifEnv *env, void *arg)
{
  cursor_wrapper *cwrapper = (cursor_wrapper *)arg;
  if (!cwrapper->is_closed)
    (void)ups_cursor_close(cwrapper->cursor);
  cwrapper->is_closed = true;
}

static void
result_resource_cleanup(ErlNifEnv *env, void *arg)
{
  result_wrapper *rwrapper = (result_wrapper *)arg;
  if (!rwrapper->is_closed)
    (void)uqi_result_close(rwrapper->result);
  rwrapper->is_closed = true;
}

static int
on_load(ErlNifEnv *env, void **priv_data, ERL_NIF_TERM load_info)
{
  g_atom_ok = enif_make_atom(env, "ok");
  g_atom_error = enif_make_atom(env, "error");
  g_atom_key_not_found = enif_make_atom(env, "key_not_found");
  g_atom_duplicate_key = enif_make_atom(env, "duplicate_key");

  g_ups_env_resource = enif_open_resource_type(env, NULL, "ups_env_resource",
                            &env_resource_cleanup,
                            (ErlNifResourceFlags)(ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER),
                            0);
  g_ups_db_resource = enif_open_resource_type(env, NULL, "ups_db_resource",
                            &db_resource_cleanup,
                            (ErlNifResourceFlags)(ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER),
                            0);
  g_ups_txn_resource = enif_open_resource_type(env, NULL, "ups_txn_resource",
                            &txn_resource_cleanup,
                            (ErlNifResourceFlags)(ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER),
                            0);
  g_ups_cursor_resource = enif_open_resource_type(env, NULL, "ups_cursor_resource",
                            &cursor_resource_cleanup,
                            (ErlNifResourceFlags)(ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER),
                            0);
  g_ups_result_resource = enif_open_resource_type(env, NULL, "ups_result_resource",
                            &result_resource_cleanup,
                            (ErlNifResourceFlags)(ERL_NIF_RT_CREATE | ERL_NIF_RT_TAKEOVER),
                            0);
  return (0);
}

extern "C" {

static ErlNifFunc ups_nif_funcs[] =
{
  {"strerror", 1, ups_nifs_strerror},
  {"env_create", 4, ups_nifs_env_create},
  {"env_open", 3, ups_nifs_env_open},
  {"env_create_db", 4, ups_nifs_env_create_db},
  {"env_open_db", 4, ups_nifs_env_open_db},
  {"env_rename_db", 3, ups_nifs_env_rename_db},
  {"env_erase_db", 2, ups_nifs_env_erase_db},
  {"db_insert", 5, ups_nifs_db_insert},
  {"db_erase", 3, ups_nifs_db_erase},
  {"db_find", 3, ups_nifs_db_find},
  {"db_find_flags", 4, ups_nifs_db_find_flags},
  {"db_close", 1, ups_nifs_db_close},
  {"txn_begin", 2, ups_nifs_txn_begin},
  {"txn_abort", 1, ups_nifs_txn_abort},
  {"txn_commit", 1, ups_nifs_txn_commit},
  {"env_close", 1, ups_nifs_env_close},
  {"cursor_create", 2, ups_nifs_cursor_create},
  {"cursor_clone", 1, ups_nifs_cursor_clone},
  {"cursor_move", 2, ups_nifs_cursor_move},
  {"cursor_overwrite", 2, ups_nifs_cursor_overwrite},
  {"cursor_find", 2, ups_nifs_cursor_find},
  {"cursor_insert", 4, ups_nifs_cursor_insert},
  {"cursor_erase", 1, ups_nifs_cursor_erase},
  {"cursor_get_duplicate_count", 1, ups_nifs_cursor_get_duplicate_count},
  {"cursor_get_record_size", 1, ups_nifs_cursor_get_record_size},
  {"cursor_close", 1, ups_nifs_cursor_close},
  {"uqi_select_range", 4, ups_nifs_uqi_select_range},
  {"uqi_result_get_row_count", 1, ups_nifs_uqi_result_get_row_count},
  {"uqi_result_get_key_type", 1, ups_nifs_uqi_result_get_key_type},
  {"uqi_result_get_record_type", 1, ups_nifs_uqi_result_get_record_type},
  {"uqi_result_get_key", 2, ups_nifs_uqi_result_get_key},
  {"uqi_result_get_record", 2, ups_nifs_uqi_result_get_record},
  {"uqi_result_close", 1, ups_nifs_uqi_result_close},
};

ERL_NIF_INIT(ups_nifs, ups_nif_funcs, on_load, NULL, NULL, NULL);

}; // extern "C"
