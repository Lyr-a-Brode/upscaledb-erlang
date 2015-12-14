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

-module(ups_eqc4).

-include_lib("eqc/include/eqc.hrl").
-include_lib("eqc/include/eqc_statem.hrl").

-include("include/ups.hrl").

-compile(export_all).

-record(state, {parameters = [], % database parameters for ups_env_create_db,
                data = [] % ordset with the data
                }).

run() ->
  eqc:module(?MODULE).

initial_state() ->
  #state{}.

create_db_parameters() ->
 [{record_size, 8}] ++
    oneof([
          [{key_type, ?UPS_TYPE_BINARY}, {key_size, ?UPS_KEY_SIZE_UNLIMITED}],
          [{key_type, ?UPS_TYPE_BINARY}, {key_size, 8}],
          [{key_type, ?UPS_TYPE_BINARY}, {key_size, 32}],
          [{key_type, ?UPS_TYPE_BINARY}, {key_size, 48}],
          [{key_type, ?UPS_TYPE_UINT8}]%,
          %[{key_type, ?UPS_TYPE_UINT16}],
          %[{key_type, ?UPS_TYPE_UINT32}],
          %[{key_type, ?UPS_TYPE_UINT64}],
          %[{key_type, ?UPS_TYPE_REAL32}],
          %[{key_type, ?UPS_TYPE_REAL64}]
        ]).

% ups_db_insert ------------------------------------
key([{record_size, _},
     {key_type, ?UPS_TYPE_BINARY}, {key_size, ?UPS_KEY_SIZE_UNLIMITED}]) ->
  binary();

key([{record_size, _},
     {key_type, ?UPS_TYPE_BINARY}, {key_size, Length}]) ->
  binary(Length);

key([{record_size, _},
     {key_type, ?UPS_TYPE_UINT8}]) ->
  binary(1);

key([{record_size, _},
     {key_type, ?UPS_TYPE_UINT16}]) ->
  binary(2);

key([{record_size, _},
     {key_type, ?UPS_TYPE_UINT32}]) ->
  binary(4);

key([{record_size, _},
     {key_type, ?UPS_TYPE_UINT64}]) ->
  binary(8);

key([{record_size, _},
     {key_type, ?UPS_TYPE_REAL32}]) ->
  binary(4);

key([{record_size, _},
     {key_type, ?UPS_TYPE_REAL64}]) ->
  binary(8).

record(_State) ->
  binary(8).

insert_params(State) ->
  {{var, db}, key(State#state.parameters), record(State), [undefined]}.

db_insert_command(State) ->
  {call, ?MODULE, db_insert, [insert_params(State)]}.

db_insert({Handle, Key, Record, Flags}) ->
  ups:db_insert(Handle, undefined, Key, Record, Flags).

db_insert_post(_State, [{_Handle, _Key, _Record, _Flags}], _Result) ->
  % we don't care about errors
  true.

db_insert_next(State, Result, [{_Handle, Key, Record, _Flags}]) ->
  case Result of
    ok ->
      State#state{data = orddict:store(Key, Record, State#state.data)};
    _ ->
      State
  end.

% ups_db_find --------------------------------------
find_params(State) ->
  {{var, db}, key(State#state.parameters),
    oneof([lt_match, leq_match, gt_match, geq_match])}. 

db_find_command(State) ->
  {call, ?MODULE, db_find, [find_params(State)]}.

db_find({Handle, Key, Flags}) ->
  ups:db_find(Handle, undefined, Key, [Flags]).

db_find_post(State, [{_Handle, Key, Flags}], Result) ->
  eq(Result, find_impl(Key, State#state.data, Flags)).

db_find_next(State, _, _) ->
  State.

find_impl(_Key, [], _) ->
  {error, key_not_found};
find_impl(Key, Data, lt_match) ->
  lt_find_impl(Key, Data);
find_impl(Key, Data, leq_match) ->
  leq_find_impl(Key, Data);
find_impl(Key, Data, gt_match) ->
  gt_find_impl(Key, Data);
find_impl(Key, Data, geq_match) ->
  geq_find_impl(Key, Data).

lt_find_impl(Key, [{Head, _}|_Tail]) when Key =< Head ->
  {error, key_not_found};
lt_find_impl(Key, [Head|Tail]) ->
  lt_find_impl2(Key, Head, Tail).
lt_find_impl2(Key, {Last, Record}, []) when Key > Last ->
  {ok, Last, Record};
lt_find_impl2(_Key, _Last, []) ->
  {error, key_not_found};
lt_find_impl2(Key, {Last, Record}, [{Head, _}|_Tail])
        when Key > Last andalso Key =< Head ->
  {ok, Last, Record};
lt_find_impl2(Key, _Last, [Head|Tail]) ->
  lt_find_impl2(Key, Head, Tail).

leq_find_impl(Key, [{Head, _}|_Tail]) when Key < Head ->
  {error, key_not_found};
leq_find_impl(Key, [{Head, Record}|_Tail]) when Key == Head ->
  {ok, Head, Record};
leq_find_impl(Key, [Head|Tail]) ->
  leq_find_impl2(Key, Head, Tail).
leq_find_impl2(Key, {Last, Record}, []) when Key >= Last ->
  {ok, Last, Record};
leq_find_impl2(_Key, _Last, []) ->
  {error, key_not_found};
leq_find_impl2(Key, {Last, Record}, [{Head, _}|_Tail])
        when Key >= Last andalso Key < Head ->
  {ok, Last, Record};
leq_find_impl2(Key, _Last, [Head|Tail]) ->
  leq_find_impl2(Key, Head, Tail).

gt_find_impl(Key, [{Head, Record}|_Tail]) when Key < Head ->
  {ok, Head, Record};
gt_find_impl(Key, [Head|Tail]) ->
  gt_find_impl2(Key, Head, Tail).
gt_find_impl2(Key, {Last, Record}, []) when Key < Last ->
  {ok, Last, Record};
gt_find_impl2(_Key, _Last, []) ->
  {error, key_not_found};
gt_find_impl2(Key, {Last, _}, [{Head, Record}|_Tail])
        when Key >= Last andalso Key < Head->
  {ok, Head, Record};
gt_find_impl2(Key, _Last, [Head|Tail]) ->
  gt_find_impl2(Key, Head, Tail).

geq_find_impl(Key, [{Head, Record}|_Tail]) when Key =< Head ->
  {ok, Head, Record};
geq_find_impl(Key, [Head|Tail]) ->
  geq_find_impl2(Key, Head, Tail).
geq_find_impl2(Key, {Last, Record}, []) when Key =< Last ->
  {ok, Last, Record};
geq_find_impl2(_Key, _Last, []) ->
  {error, key_not_found};
geq_find_impl2(Key, {Last, _}, [{Head, Record}|_Tail])
        when Key > Last andalso Key =< Head ->
  {ok, Head, Record};
geq_find_impl2(Key, _Last, [Head|Tail]) ->
  geq_find_impl2(Key, Head, Tail).

weight(_State, db_insert) ->
  100;
weight(_State, db_find) ->
  100;
weight(_State, _) ->
  10.

prop_ups4() ->
  ?FORALL(DbParams, create_db_parameters(),
    ?FORALL(Cmds, more_commands(50,
                        commands(?MODULE, #state{parameters = DbParams})),
      begin
        % io:format("params ~p ~n", [DbParams]),
        {ok, EnvHandle} = ups:env_create("ups_eqc.db", [undefined], 0,
                                         [{cache_size, 1000000},
                                          {page_size, 1024}]),
        {ok, DbHandle} = ups:env_create_db(EnvHandle, 1,
                                         [force_records_inline], DbParams),
        {History, State, Result} = run_commands(?MODULE, Cmds,
                                             [{db, DbHandle}]),
        eqc_statem:show_states(
          pretty_commands(?MODULE, Cmds, {History, State, Result},
            aggregate(command_names(Cmds),
              collect(length(Cmds),
                begin
                  ups:db_close(DbHandle),
                  ups:env_close(EnvHandle),
                  Result == ok
                end))))
      end)).
