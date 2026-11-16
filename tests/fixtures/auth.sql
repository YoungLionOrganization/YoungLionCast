-- Disposable test database only: minimal Supabase contracts, not production migration.
drop schema if exists yl_cast_private cascade;
drop table if exists public.yl_cast_connections cascade;
drop function if exists public.yl_cast_rpc(text,uuid,jsonb);
create schema if not exists auth;
do $$ begin
  if not exists(select 1 from pg_roles where rolname='anon') then create role anon; end if;
  if not exists(select 1 from pg_roles where rolname='authenticated') then create role authenticated; end if;
  if not exists(select 1 from pg_roles where rolname='service_role') then create role service_role bypassrls; end if;
end $$;
create table if not exists auth.users(id uuid primary key);
create table if not exists auth.sessions(id uuid primary key,user_id uuid references auth.users(id));
create table if not exists auth.identities(id uuid primary key default gen_random_uuid(),user_id uuid references auth.users(id),provider text not null,provider_id text not null);
truncate auth.identities;
create or replace function auth.uid() returns uuid language sql stable as $$ select nullif(current_setting('request.jwt.claim.sub',true),'')::uuid $$;
grant usage on schema auth to authenticated,service_role;
grant execute on function auth.uid() to authenticated,service_role;
