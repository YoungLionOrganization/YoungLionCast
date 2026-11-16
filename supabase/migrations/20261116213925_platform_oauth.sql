begin;

create schema if not exists yl_cast_private;
revoke all on schema yl_cast_private from public, anon, authenticated;
grant usage on schema yl_cast_private to service_role;

create table yl_cast_private.yl_cast_owners (
  provider text not null check (provider in ('twitch','kick')),
  provider_user_id text not null check (length(provider_user_id) between 1 and 128),
  owner_id uuid not null references auth.users(id) on delete cascade,
  primary key (provider,provider_user_id)
);
create index yl_cast_owners_owner on yl_cast_private.yl_cast_owners(owner_id);

create table public.yl_cast_connections (
  id uuid primary key default gen_random_uuid(),
  owner_id uuid not null references auth.users(id) on delete cascade,
  provider text not null check(provider in ('twitch','kick')),
  provider_user_id text not null,
  display_name text not null,
  picture text,
  scopes text[] not null default '{}',
  expires_at timestamptz not null,
  status text not null check(status in ('connected','reauth_required','disconnected')),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  unique(provider,provider_user_id)
);
create index yl_cast_connections_owner on public.yl_cast_connections(owner_id,id);
alter table public.yl_cast_connections enable row level security;
revoke all on public.yl_cast_connections from public,anon,authenticated;
grant select on public.yl_cast_connections to authenticated;
grant all on public.yl_cast_connections to service_role;
create policy cast_owner_read on public.yl_cast_connections for select to authenticated
  using ((select auth.uid())=owner_id);

create table yl_cast_private.yl_cast_transactions (
  id uuid primary key default gen_random_uuid(),
  owner_id uuid not null references auth.users(id) on delete cascade,
  provider text not null check(provider in ('twitch','kick')),
  state_hash text unique not null,
  verifier jsonb not null,
  target uuid references public.yl_cast_connections(id) on delete cascade,
  account jsonb,
  status text not null default 'started' check(status in ('started','exchanging','pending','confirmed','failed')),
  created_at timestamptz not null default now(),
  expires_at timestamptz not null default now()+interval '10 minutes'
);
create index yl_cast_transactions_expiry on yl_cast_private.yl_cast_transactions(expires_at);

create table yl_cast_private.yl_cast_credentials (
  id uuid primary key,
  owner_id uuid not null references auth.users(id) on delete cascade,
  provider text not null check(provider in ('twitch','kick')),
  provider_user_id text,
  connection_id uuid unique references public.yl_cast_connections(id) on delete cascade,
  kind text not null check(kind in ('pending','publishing','login','cleanup','denied')),
  purpose text not null default 'publishing' check(purpose in ('login','publishing')),
  grant_data jsonb not null,
  scopes text[] not null default '{}',
  expires_at timestamptz not null,
  due_at timestamptz not null,
  lease uuid,
  lease_until timestamptz,
  version bigint not null default 1,
  attempts integer not null default 0,
  last_error text,
  created_at timestamptz not null default now()
);
create index yl_cast_credentials_due on yl_cast_private.yl_cast_credentials(due_at) where kind<>'denied';
create index yl_cast_credentials_owner on yl_cast_private.yl_cast_credentials(owner_id);
create table yl_cast_private.yl_cast_denied_sessions (
  session_id uuid primary key,
  owner_id uuid not null references auth.users(id) on delete cascade,
  created_at timestamptz not null default now()
);
create table yl_cast_private.yl_cast_rate_limits (
  bucket text primary key,
  window_start timestamptz not null,
  hits integer not null
);
alter table yl_cast_private.yl_cast_owners enable row level security;
alter table yl_cast_private.yl_cast_transactions enable row level security;
alter table yl_cast_private.yl_cast_credentials enable row level security;
alter table yl_cast_private.yl_cast_denied_sessions enable row level security;
alter table yl_cast_private.yl_cast_rate_limits enable row level security;
revoke all on all tables in schema yl_cast_private from public,anon,authenticated;
grant all on all tables in schema yl_cast_private to service_role;

-- Identity and publishing ownership use the same atomic constraint. This never merges users.
create function yl_cast_private.yl_cast_claim(p_provider text,p_identity text,p_owner uuid)
returns void language plpgsql security invoker set search_path='' as $$
declare claimed uuid;
begin
  insert into yl_cast_private.yl_cast_owners(provider,provider_user_id,owner_id)
  values(p_provider,p_identity,p_owner)
  on conflict(provider,provider_user_id) do update set owner_id=yl_cast_private.yl_cast_owners.owner_id
  returning owner_id into claimed;
  if claimed<>p_owner then raise exception 'owner_conflict'; end if;
end $$;
revoke all on function yl_cast_private.yl_cast_claim(text,text,uuid) from public,anon,authenticated;
grant execute on function yl_cast_private.yl_cast_claim(text,text,uuid) to service_role;

create function yl_cast_private.yl_cast_identity_owner()
returns trigger language plpgsql security definer set search_path='' as $$
begin
  if new.provider in ('twitch','custom:kick') then
    perform yl_cast_private.yl_cast_claim(case new.provider when 'custom:kick' then 'kick' else 'twitch' end,new.provider_id,new.user_id);
  end if;
  return new;
end $$;
revoke all on function yl_cast_private.yl_cast_identity_owner() from public,anon,authenticated;
create trigger yl_cast_identity_owner after insert or update of user_id,provider,provider_id
  on auth.identities for each row execute function yl_cast_private.yl_cast_identity_owner();
-- Fail atomically if existing identities already disagree; never silently select a winner.
do $$ declare r record; begin
  for r in select * from auth.identities where provider in ('twitch','custom:kick') loop
    perform yl_cast_private.yl_cast_claim(case r.provider when 'custom:kick' then 'kick' else 'twitch' end,r.provider_id,r.user_id);
  end loop;
end $$;

-- PostgREST entry point: SECURITY INVOKER, executable only by the backend role.
-- All credentials remain in an unexposed schema; p_owner comes only from a verified session.
-- Do not grant service_role broad access to Auth's private tables.
create function yl_cast_private.yl_cast_session_exists(p_owner uuid,p_session uuid)
returns boolean language sql stable security definer set search_path='' as $$
  select exists(select 1 from auth.sessions where id=p_session and user_id=p_owner)
$$;
create function yl_cast_private.yl_cast_account_active(p_owner uuid)
returns boolean language plpgsql stable security definer set search_path='' as $$
declare active boolean;
begin
  if to_regclass('public.yl_profiles') is null then return false; end if;
  execute 'select exists(select 1 from public.yl_profiles where user_id=$1 and account_status=''active'')' into active using p_owner;
  return active;
end $$;
revoke all on function yl_cast_private.yl_cast_session_exists(uuid,uuid),yl_cast_private.yl_cast_account_active(uuid) from public,anon,authenticated;
grant execute on function yl_cast_private.yl_cast_session_exists(uuid,uuid),yl_cast_private.yl_cast_account_active(uuid) to service_role;

create function public.yl_cast_rpc(p_action text,p_owner uuid,p_data jsonb default '{}')
returns jsonb language plpgsql security invoker set search_path='' as $$
declare
  tx yl_cast_private.yl_cast_transactions;
  conn public.yl_cast_connections;
  cred yl_cast_private.yl_cast_credentials;
  result jsonb;
  changed integer;
  current_hits integer;
  cid uuid;
begin
  if p_action in ('admit','admit_session') then
    if not yl_cast_private.yl_cast_session_exists(p_owner,(p_data->>'session_id')::uuid)
      or exists(select 1 from yl_cast_private.yl_cast_denied_sessions where session_id=(p_data->>'session_id')::uuid)
      then raise exception 'session_revoked'; end if;
    if p_action='admit' and not yl_cast_private.yl_cast_account_active(p_owner) then raise exception 'account_inactive'; end if;
    return 'true';
  elsif p_action='rate' then
    insert into yl_cast_private.yl_cast_rate_limits values(p_data->>'bucket',date_trunc('minute',now()),1)
    on conflict(bucket) do update set
      hits=case when yl_cast_private.yl_cast_rate_limits.window_start<date_trunc('minute',now()) then 1 else yl_cast_private.yl_cast_rate_limits.hits+1 end,
      window_start=date_trunc('minute',now()) returning hits into current_hits;
    return to_jsonb(current_hits<=least((p_data->>'limit')::integer,1000));
  elsif p_action='start' then
    if p_data->>'target' is not null then
      select * into conn from public.yl_cast_connections where id=(p_data->>'target')::uuid and owner_id=p_owner and provider=p_data->>'provider';
      if not found then raise exception 'not_found'; end if;
    end if;
    if (select count(*) from yl_cast_private.yl_cast_transactions where owner_id=p_owner and expires_at>now() and status<>'confirmed')>=10 then raise exception 'rate_limited'; end if;
    insert into yl_cast_private.yl_cast_transactions(id,owner_id,provider,state_hash,verifier,target)
    values(coalesce((p_data->>'id')::uuid,gen_random_uuid()),p_owner,p_data->>'provider',p_data->>'state_hash',p_data->'verifier',(p_data->>'target')::uuid)
    returning * into tx;
    return to_jsonb(tx);
  elsif p_action='consume' then
    update yl_cast_private.yl_cast_transactions set status='exchanging'
      where state_hash=p_data->>'state_hash' and provider=p_data->>'provider' and status='started' and expires_at>now()
      returning * into tx;
    if not found then raise exception 'invalid_state'; end if;
    return to_jsonb(tx);
  elsif p_action in ('save_grant','ready','pending','confirm','fail_transaction') then
    select * into tx from yl_cast_private.yl_cast_transactions where id=(p_data->>'transaction_id')::uuid and owner_id=p_owner for update;
    if not found then raise exception 'not_found'; end if;
    if p_action='fail_transaction' then
      if tx.status<>'confirmed' then
        update yl_cast_private.yl_cast_transactions set status='failed' where id=tx.id;
        update yl_cast_private.yl_cast_credentials set kind='cleanup',due_at=now(),version=version+1,lease=null,lease_until=null where id=tx.id;
      end if;
      return 'true';
    end if;
    if tx.expires_at<=now() then raise exception 'invalid_state'; end if;
    if p_action='save_grant' then
      if tx.status<>'exchanging' then raise exception 'invalid_state'; end if;
      insert into yl_cast_private.yl_cast_credentials(id,owner_id,provider,kind,grant_data,scopes,expires_at,due_at)
      values(tx.id,p_owner,tx.provider,'pending',p_data->'grant',array(select jsonb_array_elements_text(p_data->'scopes')),(p_data->>'expires_at')::timestamptz,tx.expires_at);
      return 'true';
    elsif p_action='ready' then
      if tx.status<>'exchanging' or not exists(select 1 from yl_cast_private.yl_cast_credentials where id=tx.id) then raise exception 'invalid_state'; end if;
      update yl_cast_private.yl_cast_transactions set status='pending',account=p_data->'account' where id=tx.id;
      update yl_cast_private.yl_cast_credentials set provider_user_id=p_data->'account'->>'id' where id=tx.id;
      return 'true';
    elsif p_action='pending' then
      if tx.status<>'pending' then raise exception 'invalid_state'; end if;
      return jsonb_build_object('transaction_id',tx.id,'provider',tx.provider,'account',tx.account,'expires_at',tx.expires_at);
    end if;
    if tx.status='confirmed' then
      select * into conn from public.yl_cast_connections where owner_id=p_owner and provider=tx.provider and provider_user_id=tx.account->>'id';
      return to_jsonb(conn);
    end if;
    if tx.status<>'pending' then raise exception 'invalid_state'; end if;
    select * into cred from yl_cast_private.yl_cast_credentials where id=tx.id and kind='pending' for update;
    if not found then raise exception 'invalid_state'; end if;
    perform yl_cast_private.yl_cast_claim(tx.provider,tx.account->>'id',p_owner);
    if tx.target is not null and not exists(select 1 from public.yl_cast_connections where id=tx.target and provider_user_id=tx.account->>'id' and owner_id=p_owner)
      then raise exception 'account_mismatch'; end if;
    insert into public.yl_cast_connections(owner_id,provider,provider_user_id,display_name,picture,scopes,expires_at,status)
      values(p_owner,tx.provider,tx.account->>'id',tx.account->>'name',tx.account->>'picture',cred.scopes,cred.expires_at,'connected')
      on conflict(provider,provider_user_id) do update set display_name=excluded.display_name,picture=excluded.picture,scopes=excluded.scopes,expires_at=excluded.expires_at,status='connected',updated_at=now()
      returning * into conn;
    update yl_cast_private.yl_cast_credentials set kind='cleanup',connection_id=null,due_at=now(),version=version+1,lease=null,lease_until=null where connection_id=conn.id;
    update yl_cast_private.yl_cast_credentials set kind='publishing',connection_id=conn.id,due_at=least(now()+interval '45 minutes',expires_at-interval '1 minute') where id=tx.id;
    update yl_cast_private.yl_cast_transactions set status='confirmed',verifier='{}' where id=tx.id;
    return to_jsonb(conn);
  elsif p_action='list' then
    select coalesce(jsonb_agg(to_jsonb(c) order by c.id),'[]') into result from
      (select * from public.yl_cast_connections where owner_id=p_owner and (p_data->>'after' is null or id>(p_data->>'after')::uuid) order by id limit 50) c;
    return result;
  elsif p_action in ('get','disconnect') then
    select * into conn from public.yl_cast_connections where id=(p_data->>'connection_id')::uuid and owner_id=p_owner for update;
    if not found then raise exception 'not_found'; end if;
    if p_action='disconnect' then
      update public.yl_cast_connections set status='disconnected',updated_at=now() where id=conn.id;
      update yl_cast_private.yl_cast_credentials set kind='cleanup',due_at=now(),version=version+1,lease=null,lease_until=null where connection_id=conn.id;
    end if;
    return to_jsonb(conn);
  elsif p_action='login_register' then
    cid=(p_data->>'session_id')::uuid;
    if exists(select 1 from yl_cast_private.yl_cast_denied_sessions where session_id=cid) then raise exception 'session_revoked'; end if;
    perform yl_cast_private.yl_cast_claim('twitch',p_data->>'provider_user_id',p_owner);
    insert into yl_cast_private.yl_cast_credentials(id,owner_id,provider,provider_user_id,kind,purpose,grant_data,expires_at,due_at)
      values(cid,p_owner,'twitch',p_data->>'provider_user_id','login','login',p_data->'grant',(p_data->>'expires_at')::timestamptz,now()+interval '45 minutes')
      on conflict(id) do nothing;
    return 'true';
  elsif p_action='orphan' then
    insert into yl_cast_private.yl_cast_credentials(id,owner_id,provider,kind,purpose,grant_data,expires_at,due_at)
      values((p_data->>'id')::uuid,p_owner,p_data->>'provider','cleanup',p_data->>'purpose',p_data->'grant',(p_data->>'expires_at')::timestamptz,now());
    return 'true';
  elsif p_action='lease' then
    -- Expired pending grants and obsolete login sessions become durable revocation work.
    update yl_cast_private.yl_cast_credentials c set kind='cleanup',due_at=now(),version=version+1,lease=null,lease_until=null
      where (kind='pending' and due_at<=now()) or (kind='login' and not yl_cast_private.yl_cast_session_exists(c.owner_id,c.id));
    delete from yl_cast_private.yl_cast_transactions t where expires_at<now()-interval '1 day' and not exists(select 1 from yl_cast_private.yl_cast_credentials c where c.id=t.id);
    delete from yl_cast_private.yl_cast_rate_limits where window_start<now()-interval '1 hour';
    with candidates as (
      select id from yl_cast_private.yl_cast_credentials where kind not in ('denied','pending') and due_at<=now() and (lease_until is null or lease_until<now()) order by due_at limit 10 for update skip locked
    ), leased as (
      update yl_cast_private.yl_cast_credentials c set lease=gen_random_uuid(),lease_until=now()+interval '2 minutes' from candidates t where c.id=t.id returning c.*
    ) select coalesce(jsonb_agg(to_jsonb(leased)),'[]') into result from leased;
    return result;
  elsif p_action in ('finish','retry','deny','removed') then
    select * into cred from yl_cast_private.yl_cast_credentials where id=(p_data->>'credential_id')::uuid;
    if cred.connection_id is not null then
      perform 1 from public.yl_cast_connections where id=cred.connection_id for update;
    end if;
    select * into cred from yl_cast_private.yl_cast_credentials where id=(p_data->>'credential_id')::uuid and lease=(p_data->>'lease')::uuid and version=(p_data->>'version')::bigint and lease_until>now() for update;
    if not found then return 'false'; end if;
    -- Metadata is locked before credentials, matching confirm/disconnect lock ordering.
    if p_action='finish' then
      if cred.kind not in ('publishing','login') then return 'false'; end if;
      update yl_cast_private.yl_cast_credentials set grant_data=p_data->'grant',expires_at=(p_data->>'expires_at')::timestamptz,due_at=least(now()+interval '45 minutes',(p_data->>'expires_at')::timestamptz-interval '1 minute'),lease=null,lease_until=null,attempts=0,version=version+1 where id=cred.id;
      if cred.connection_id is not null then update public.yl_cast_connections set expires_at=(p_data->>'expires_at')::timestamptz,updated_at=now() where id=cred.connection_id and status='connected'; end if;
    elsif p_action='removed' then
      delete from yl_cast_private.yl_cast_credentials where id=cred.id;
    elsif p_action='retry' then
      update yl_cast_private.yl_cast_credentials set attempts=attempts+1,due_at=now()+make_interval(secs=>least(3600,30*power(2,least(attempts,7))::integer)),last_error='provider_unavailable',lease=null,lease_until=null where id=cred.id;
    else
      if cred.kind='login' then insert into yl_cast_private.yl_cast_denied_sessions(session_id,owner_id) values(cred.id,cred.owner_id) on conflict do nothing; end if;
      update yl_cast_private.yl_cast_credentials set kind='cleanup',due_at=now(),last_error='reauth_required',lease=null,lease_until=null,version=version+1 where id=cred.id;
      if cred.connection_id is not null then update public.yl_cast_connections set status='reauth_required',updated_at=now() where id=cred.connection_id and status='connected'; end if;
    end if;
    return 'true';
  end if;
  raise exception 'invalid_action';
end $$;
revoke all on function public.yl_cast_rpc(text,uuid,jsonb) from public,anon,authenticated;
grant execute on function public.yl_cast_rpc(text,uuid,jsonb) to service_role;

commit;
