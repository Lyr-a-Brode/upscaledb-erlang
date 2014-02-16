%%
%% Copyright (C) 2005-2014 Christoph Rupp (chris@crupp.de).
%%
%% This program is free software; you can redistribute it and/or modify it
%% under the terms of the GNU General Public License as published by the
%% Free Software Foundation; either version 2 of the License, or
%% (at your option) any later version.
%%
%% See files COPYING.* for License information.
%%
%%
-module(ham_eqc1).

-include_lib("eqc/include/eqc.hrl").
-include_lib("eqc/include/eqc_statem.hrl").
-include_lib("eunit/include/eunit.hrl").

-define(HAM_TYPE_BINARY, 0).
-define(HAM_TYPE_CUSTOM, 1).
-define(HAM_TYPE_UINT8, 3).
-define(HAM_TYPE_UINT16, 5).
-define(HAM_TYPE_UINT32, 7).
-define(HAM_TYPE_UINT64, 9).
-define(HAM_TYPE_REAL32, 11).
-define(HAM_TYPE_REAL64, 12).
-define(HAM_KEY_SIZE_UNLIMITED, 16#ffff).
-define(HAM_RECORD_SIZE_UNLIMITED, 16#ffffffff).

-compile(export_all).

-record(state, {env_flags = [], % environment flags for ham_env_create
                env_parameters = [], % environment parameters for ham_env_create
                open = [],      % list of open databases [{name, handle}]
                databases = []  % list of existing databases [name]
                }).

run_test_() ->
  {timeout, 60, [fun() -> run() end]}.

run() ->
  eqc:module(?MODULE).

dbname() ->
  choose(1, 20).

initial_state() ->
  #state{}.

% ham_env_create_db ---------------------------------

create_db_flags() ->
  [elements([undefined, enable_duplicate_keys, record_number])].

create_db_parameters() ->
  ?LET(KeySize, choose(1, 128),
    ?LET(RecordSize, choose(0, 1024 * 1024 * 4),
      [
        oneof([
                {record_size, ?HAM_RECORD_SIZE_UNLIMITED},
                {record_size, RecordSize}
              ]),
        oneof([
                [{key_type, ?HAM_TYPE_BINARY}, {key_size,
                                                ?HAM_KEY_SIZE_UNLIMITED}],
                [{key_type, ?HAM_TYPE_BINARY}, {key_size, KeySize}],
                {key_type, ?HAM_TYPE_UINT8},
                {key_type, ?HAM_TYPE_UINT16},
                {key_type, ?HAM_TYPE_UINT32},
                {key_type, ?HAM_TYPE_UINT64},
                {key_type, ?HAM_TYPE_REAL32},
                {key_type, ?HAM_TYPE_REAL64}
              ])
      ])).

create_db_pre(State) ->
  length(State#state.databases) < 30.

create_db_command(_State) ->
  {call, ?MODULE, create_db, [{var, env}, dbname(), create_db_flags(),
                              create_db_parameters()]}.

create_db(EnvHandle, DbName, DbFlags, DbParams) ->
  case ham:env_create_db(EnvHandle, DbName, DbFlags, lists:flatten(DbParams)) of
    {ok, DbHandle} ->
      DbHandle;
    {error, What} ->
      {error, What}
  end.

create_db_post(State, [_EnvHandle, DbName, _DbFlags, _DbParams], Result) ->
  case lists:member(DbName, State#state.databases) of
    true ->
      eq(Result, {error, database_already_exists});
    false ->
      true
  end.

create_db_next(State, Result, [_EnvHandle, DbName, _DbFlags, _DbParams]) ->
  case lists:member(DbName, State#state.databases) of
    true ->
      State;
    false ->
      State#state{databases = State#state.databases ++ [DbName],
                  open = State#state.open ++ [{DbName, Result}]}
  end.

% ham_env_open_db ---------------------------------

open_db_flags() ->
    [elements([undefined, read_only])].

open_db_pre(State) ->
  State#state.open /= []
    andalso not lists:member(in_memory, State#state.env_flags).

open_db_command(_State) ->
  {call, ?MODULE, open_db, [{var, env}, dbname(), open_db_flags()]}.

open_db(EnvHandle, DbName, DbFlags) ->
  case ham:env_open_db(EnvHandle, DbName, DbFlags) of
    {ok, DbHandle} ->
      DbHandle;
    {error, What} ->
      {error, What}
  end.

open_db_post(State, [_EnvHandle, DbName, _DbFlags], Result) ->
  case Result of
    {error, database_already_open} ->
      eq(lists:keymember(DbName, 1, State#state.open), true);
    {error, database_not_found} ->
      eq(lists:member(DbName, State#state.databases), false);
    {error, _} ->
      false;
    _Else ->
      true
  end.

open_db_next(State, Result, [_EnvHandle, DbName, _DbFlags]) ->
  case (lists:member(DbName, State#state.databases) == true
          andalso lists:keymember(DbName, 1, State#state.open) == false) of
    true ->
      State#state{open = State#state.open ++ [{DbName, Result}]};
    false ->
      State
  end.

% ham_env_erase_db ---------------------------------

erase_db_pre(State) ->
  State#state.databases /= []
    andalso not lists:member(in_memory, State#state.env_flags).

erase_db_pre(State, [_EnvHandle, DbName]) ->
  lists:keymember(DbName, 1, State#state.open) == false
    andalso lists:member(DbName, State#state.databases) == true.

erase_db_command(_State) ->
  {call, ?MODULE, erase_db, [{var, env}, dbname()]}.

erase_db(EnvHandle, DbName) ->
  ham:env_erase_db(EnvHandle, DbName).

erase_db_post(_State, [_EnvHandle, _DbName], Result) ->
  Result == ok.

erase_db_next(State, _Result, [_EnvHandle, DbName]) ->
  State#state{databases = lists:delete(DbName, State#state.databases)}.

% ham_env_rename_db ---------------------------------

rename_db_pre(State) ->
  State#state.databases /= []
    andalso not lists:member(in_memory, State#state.env_flags).

rename_db_pre(State, [_EnvHandle, OldName, NewName]) ->
  % names must not be identical
  OldName /= NewName
    % database must not be open
    andalso lists:keymember(OldName, 1, State#state.open) == false
    % database must exist
    andalso lists:keymember(OldName, 1, State#state.databases) == true
    % new name must not exist
    andalso lists:member(NewName, State#state.databases) == false.

rename_db_command(_State) ->
  {call, ?MODULE, rename_db, [{var, env}, dbname(), dbname()]}.

rename_db(EnvHandle, OldName, NewName) ->
  ham:env_rename_db(EnvHandle, OldName, NewName).

rename_db_post(_State, [_EnvHandle, _OldName, _NewName], Result) ->
  Result == ok.

rename_db_next(State, _Result, [_EnvHandle, OldName, NewName]) ->
  State#state{databases
              = lists:delete(OldName, State#state.databases ++ [NewName])}.

% ham_env_close_db ---------------------------------

dbhandle(OpenList) ->
  elements([H || {_N, H} <- OpenList]).

db_close_pre(State) ->
  State#state.open /= [].

db_close_command(State) ->
  {call, ?MODULE, db_close, [dbhandle(State#state.open)]}.

db_close(DbHandle) ->
  ham:db_close(DbHandle).

db_close_post(_State, [_DbHandle], Result) ->
  Result == ok.

db_close_next(State, _Result, [DbHandle]) ->
  {DbName, DbHandle} = lists:keyfind(DbHandle, 2, State#state.open),
  case lists:member(in_memory, State#state.env_flags) of
    true ->
      State#state{open = lists:keydelete(DbHandle, 2, State#state.open),
                  databases = lists:delete(DbName, State#state.databases)};
    false ->
      State#state{open = lists:keydelete(DbHandle, 2, State#state.open)}
  end.



env_flags() ->
  oneof([
    [in_memory],
    list(elements([enable_fsync, disable_mmap, cache_unlimited,
                     enable_recovery]))]).

env_parameters() ->
  list(elements([{cache_size, 100000}, {page_size, 1024 * 4}])).

is_valid_combination(EnvFlags, EnvParams) ->
  not (lists:member(in_memory, EnvFlags)
    andalso lists:keymember(cache_size, 1, EnvParams)).

prop_ham() ->
  ?FORALL({EnvFlags, EnvParams}, {env_flags(), env_parameters()},
    ?IMPLIES(is_valid_combination(EnvFlags, EnvParams),
      ?FORALL(Cmds, commands(?MODULE, #state{env_flags = EnvFlags,
                                           env_parameters = EnvParams}),
        begin
          io:format("flags ~p, params ~p ~n", [EnvFlags, EnvParams]),
          {ok, EnvHandle} = ham:env_create("ham_eqc.db", EnvFlags, 0,
                                         EnvParams),
          {History, State, Result} = run_commands(?MODULE, Cmds,
                                              [{env, EnvHandle}]),
          eqc_statem:show_states(
            pretty_commands(?MODULE, Cmds, {History, State, Result},
              aggregate(command_names(Cmds),
                collect(length(Cmds),
                  begin
                    ham:env_close(EnvHandle),
                    Result == ok
                  end))))
        end))).
