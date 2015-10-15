%%
%% Copyright (C) 2005-2015 Christoph Rupp (chris@crupp.de).
%%
%% This program is free software: you can redistribute it and/or modify
%% it under the terms of the GNU General Public License as published by
%% the Free Software Foundation, either version 3 of the License, or
%% (at your option) any later version.
%%
%% This program is distributed in the hope that it will be useful,
%% but WITHOUT ANY WARRANTY; without even the implied warranty of
%% MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
%% GNU General Public License for more details.
%%
%% You should have received a copy of the GNU General Public License
%% along with this program.  If not, see <http://www.gnu.org/licenses/>.

-module(ups_eqc3).

-include_lib("eqc/include/eqc.hrl").
-include_lib("eqc/include/eqc_statem.hrl").

-include("include/ups.hrl").

-compile(export_all).

-record(dbstate, {handle, % database handle
                flags = [], % database flags for ups_env_create_db
                parameters = [], % database parameters for ups_env_create_db,
                data = [] % ordset with the data
                }).

-record(state, {env_flags = [], % environment flags for ups_env_create
                env_parameters = [], % environment parameters for ups_env_create
                open_dbs = [], % list of open databases [{name, #dbstate}]
                closed_dbs = [] % list of closed databases [{name, #dbstate}]
                }).

run() ->
  eqc:module(?MODULE).


dbname() ->
  choose(1, 3).

dbhandle(State) ->
  elements(State#state.open_dbs).

initial_state() ->
  #state{}.

% ups_env_create_db ---------------------------------

% ignore record_number - it has the same characteristics as UPS_TYPE_UINT64,
% but adds lots of complexity to the test
create_db_flags() ->
  [elements([undefined])].

create_db_parameters() ->
  [
    {record_size, ?UPS_RECORD_SIZE_UNLIMITED},
    {key_type, ?UPS_TYPE_BINARY},
    {key_size, ?UPS_KEY_SIZE_UNLIMITED}
  ].

create_db_pre(State, [_EnvHandle, DbName, _DbFlags, _DbParameters]) ->
  lists:keymember(DbName, 1, State#state.open_dbs) == false
    andalso lists:keymember(DbName, 1, State#state.closed_dbs) == false.

create_db_command(_State) ->
  {call, ?MODULE, create_db, [{var, env}, dbname(), create_db_flags(),
                              create_db_parameters()]}.

create_db(EnvHandle, DbName, DbFlags, DbParams) ->
  case ups:env_create_db(EnvHandle, DbName, DbFlags, lists:flatten(DbParams)) of
    {ok, DbHandle} ->
      DbHandle;
    {error, What} ->
      {error, What}
  end.

create_db_post(_State, [_EnvHandle, _DbName, _DbFlags, _DbParams], Result) ->
  case Result of
    {error, _What} ->
      false;
    _ ->
      true
  end.

create_db_next(State, Result, [_EnvHandle, DbName, DbFlags, DbParams]) ->
  DbState = #dbstate{handle = Result,
                    flags = DbFlags,
                    parameters = DbParams,
                    data = orddict:new()},
  State#state{open_dbs = State#state.open_dbs ++ [{DbName, DbState}]}.

% ups_env_open_db -----------------------------------
open_db_pre(State, [_EnvHandle, DbName]) ->
  lists:keymember(DbName, 1, State#state.open_dbs) == false
    andalso lists:keymember(DbName, 1, State#state.closed_dbs) == true.

open_db_command(_State) ->
  {call, ?MODULE, open_db, [{var, env}, dbname()]}.

open_db(EnvHandle, DbName) ->
  case ups:env_open_db(EnvHandle, DbName) of
    {ok, DbHandle} ->
      DbHandle;
    {error, What} ->
      {error, What}
  end.

open_db_post(_State, [_EnvHandle, _DbName], Result) ->
  case Result of
    {error, _What} ->
      false;
    _ ->
      true
  end.

open_db_next(State, Result, [_EnvHandle, DbName]) ->
  {DbName, DbState} = lists:keyfind(DbName, 1, State#state.closed_dbs),
  DbState2 = DbState#dbstate{handle = Result},
  State#state{closed_dbs = lists:keydelete(DbName, 1, State#state.closed_dbs),
        open_dbs = State#state.open_dbs ++ [{DbName, DbState2}]}.

% ups_db_insert ------------------------------------
key(_DbState) ->
  binary().

record(_DbState) ->
  binary().

flags(_DbState) ->
  undefined.

insert_params(State) ->
  ?LET(Db, elements(State#state.open_dbs),
    begin
      {DbName, DbState} = Db,
      {{DbName, DbState#dbstate.handle}, [flags(DbState)],
                key(DbState), record(DbState)}
    end).

db_insert_pre(State) ->
  length(State#state.open_dbs) > 0.

db_insert_command(State) ->
  {call, ?MODULE, db_insert, [insert_params(State)]}.

db_insert({{_DbName, DbHandle}, Flags, Key, Record}) ->
  ups:db_insert(DbHandle, undefined, Key, Record, Flags).

db_insert_post(State, [{{DbName, _DbHandle}, _Flags, Key, _Record}], Result) ->
  {DbName, DbState} = lists:keyfind(DbName, 1, State#state.open_dbs),
  case orddict:find(Key, DbState#dbstate.data) of
    {ok, _} ->
      eq(Result, {error, duplicate_key});
    error ->
      eq(Result, ok)
  end.

db_insert_next(State, Result, [{{DbName, _DbHandle}, _Flags, Key, Record}]) ->
  {DbName, DbState} = lists:keyfind(DbName, 1, State#state.open_dbs),
  case Result of
    ok ->
      DbState2 = DbState#dbstate{data
                    = orddict:store(Key, byte_size(Record),
                                DbState#dbstate.data)},
      Databases = lists:keydelete(DbName, 1, State#state.open_dbs),
      State#state{open_dbs = Databases ++ [{DbName, DbState2}]};
    _ ->
      State
  end.

% ups_db_erase -------------------------------------
erase_params(State) ->
  ?LET(Db, elements(State#state.open_dbs),
    begin
      {DbName, DbState} = Db,
      {{DbName, DbState#dbstate.handle}, key(DbState)}
    end).

db_erase_pre(State) ->
  length(State#state.open_dbs) > 0.

db_erase_command(State) ->
  {call, ?MODULE, db_erase, [erase_params(State)]}.

db_erase({{_DbName, DbHandle}, Key}) ->
  ups:db_erase(DbHandle, Key).

db_erase_post(State, [{{DbName, _DbHandle}, Key}], Result) ->
  {DbName, DbState} = lists:keyfind(DbName, 1, State#state.open_dbs),
  case orddict:find(Key, DbState#dbstate.data) of
    {ok, _} ->
      eq(Result, ok);
    error ->
      eq(Result, {error, key_not_found})
  end.

db_erase_next(State, Result, [{{DbName, _DbHandle}, Key}]) ->
  {DbName, DbState} = lists:keyfind(DbName, 1, State#state.open_dbs),
  case Result of
    ok ->
      DbState2 = DbState#dbstate{data
                    = orddict:erase(Key, DbState#dbstate.data)},
      Databases = lists:keydelete(DbName, 1, State#state.open_dbs),
      State#state{open_dbs = Databases ++ [{DbName, DbState2}]};
    _ ->
      State
  end.

% ups_db_find --------------------------------------
find_params(State) ->
  ?LET(Db, elements(State#state.open_dbs),
    begin
      {DbName, DbState} = Db,
      {{DbName, DbState#dbstate.handle}, key(DbState)}
    end).

db_find_pre(State) ->
  length(State#state.open_dbs) > 0.

db_find_command(State) ->
  {call, ?MODULE, db_find, [erase_params(State)]}.

db_find({{_DbName, DbHandle}, Key}) ->
  ups:db_find(DbHandle, Key).

db_find_post(State, [{{DbName, _DbHandle}, Key}], Result) ->
  {DbName, DbState} = lists:keyfind(DbName, 1, State#state.open_dbs),
  case orddict:find(Key, DbState#dbstate.data) of
    {ok, RecordSize} ->
      {ok, Record} = Result,
      eq(byte_size(Record), RecordSize);
    error ->
      eq(Result, {error, key_not_found})
  end.

db_find_next(State, _, _) ->
  State.

% ups_env_close_db ---------------------------------
db_close_pre(State) ->
  length(State#state.open_dbs) > 0.

db_close_command(State) ->
  {call, ?MODULE, db_close, [dbhandle(State)]}.

db_close({_DbName, DbState}) ->
  ups:db_close(DbState#dbstate.handle).

db_close_post(_State, [_Database], Result) ->
  Result == ok.

db_close_next(State, _Result, [{DbName, _DbState}]) ->
  {DbName, DbState} = lists:keyfind(DbName, 1, State#state.open_dbs),
  case lists:member(in_memory, State#state.env_flags) of
    true -> % in-memory: remove database
      State#state{
              open_dbs = lists:keydelete(DbName, 1, State#state.open_dbs),
              closed_dbs = lists:keydelete(DbName, 1, State#state.closed_dbs)};
    false -> % disk-based: mark as "closed"
      State#state{
              open_dbs = lists:keydelete(DbName, 1, State#state.open_dbs),
              closed_dbs = State#state.closed_dbs ++ [{DbName, DbState}]}
  end.

env_flags() ->
  [undefined].

env_parameters() ->
  [{cache_size, 100000}, {page_size, 1024}].

weight(_State, db_insert) ->
  100;
weight(_State, db_erase) ->
  100;
weight(_State, _) ->
  10.

prop_ups3() ->
  ?FORALL({EnvFlags, EnvParams}, {env_flags(), env_parameters()},
    ?FORALL(Cmds, more_commands(500,
              commands(?MODULE, #state{env_flags = EnvFlags,
                              env_parameters = EnvParams})),
      begin
        % io:format("flags ~p, params ~p ~n", [EnvFlags, EnvParams]),
        {ok, EnvHandle} = ups:env_create("ups_eqc.db", EnvFlags, 0,
                                       EnvParams),
        {History, State, Result} = run_commands(?MODULE, Cmds,
                                            [{env, EnvHandle}]),
        eqc_statem:show_states(
          pretty_commands(?MODULE, Cmds, {History, State, Result},
            aggregate(command_names(Cmds),
              %%collect(length(Cmds),
                begin
                  ups:env_close(EnvHandle),
                  Result == ok
                end)))%%)
      end)).
