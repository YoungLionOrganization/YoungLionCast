begin;
create table yl_cast_private.yl_cast_native_requests (
 id uuid primary key default gen_random_uuid(),
 owner_id uuid not null references auth.users(id) on delete cascade,
 session_id uuid not null,
 browser_session_id uuid,
 provider text not null check(provider in ('twitch','kick')),
 target uuid references public.yl_cast_connections(id) on delete cascade,
 ticket_hash text unique not null check(ticket_hash ~ '^[0-9a-f]{64}$'),
 proof_hash text not null check(proof_hash ~ '^[0-9a-f]{64}$'),
 status text not null default 'waiting' check(status in ('waiting','bound','cancelled')),
 transaction_id uuid unique references yl_cast_private.yl_cast_transactions(id) on delete cascade,
 expires_at timestamptz not null default now()+interval '10 minutes'
);
create index yl_cast_native_expiry on yl_cast_private.yl_cast_native_requests(expires_at);
alter table yl_cast_private.yl_cast_native_requests enable row level security;
revoke all on yl_cast_private.yl_cast_native_requests from public,anon,authenticated;
grant all on yl_cast_private.yl_cast_native_requests to service_role;

-- Preserve the already deployed browser protocol; all actions keep their original checks.
alter function public.yl_cast_rpc(text,uuid,jsonb) rename to yl_cast_rpc_v1;
revoke all on function public.yl_cast_rpc_v1(text,uuid,jsonb) from public,anon,authenticated;
create function public.yl_cast_rpc(p_action text,p_owner uuid default null,p_data jsonb default '{}')
returns jsonb language plpgsql security invoker set search_path='' as $$
declare req yl_cast_private.yl_cast_native_requests;
 tx yl_cast_private.yl_cast_transactions;
 result jsonb;
 sid uuid := (p_data->>'session_id')::uuid;
begin
 if p_action='lease' then
  delete from yl_cast_private.yl_cast_native_requests where expires_at<now()-interval '1 day';
 end if;
 if p_action='confirm' and exists(select 1 from yl_cast_private.yl_cast_native_requests where transaction_id=(p_data->>'transaction_id')::uuid) then
  raise exception 'invalid_state';
 end if;
 if p_action='consume' then
  result:=public.yl_cast_rpc_v1(p_action,p_owner,p_data);
  return result || jsonb_build_object('native',exists(select 1 from yl_cast_private.yl_cast_native_requests where transaction_id=(result->>'id')::uuid));
 end if;
 if left(p_action,7)<>'native_' then
  return public.yl_cast_rpc_v1(p_action,p_owner,p_data);
 end if;
 perform public.yl_cast_rpc_v1('admit',p_owner,jsonb_build_object('session_id',sid));
 if p_action='native_start' then
  perform pg_advisory_xact_lock(hashtextextended('cast_native:' || p_owner::text,0));
  if (select count(*) from yl_cast_private.yl_cast_native_requests where owner_id=p_owner and expires_at>now() and status<>'cancelled')>=10 then raise exception 'rate_limited'; end if;
  if p_data->>'target' is not null and not exists(select 1 from public.yl_cast_connections where id=(p_data->>'target')::uuid and owner_id=p_owner and provider=p_data->>'provider') then raise exception 'not_found'; end if;
  insert into yl_cast_private.yl_cast_native_requests(owner_id,session_id,provider,target,ticket_hash,proof_hash)
  values(p_owner,sid,p_data->>'provider',(p_data->>'target')::uuid,p_data->>'ticket_hash',p_data->>'proof_hash') returning * into req;
  return jsonb_build_object('request_id',req.id,'expires_at',req.expires_at);
 end if;
 select * into req from yl_cast_private.yl_cast_native_requests where id=(p_data->>'request_id')::uuid and owner_id=p_owner for update;
 if not found then raise exception 'not_found'; end if;
 perform public.yl_cast_rpc_v1('admit',p_owner,jsonb_build_object('session_id',req.session_id));
 if p_action in ('native_claim','native_bind') then
  if req.expires_at<=now() or req.status<>'waiting' or req.ticket_hash is distinct from p_data->>'ticket_hash' then raise exception 'invalid_state'; end if;
  if p_action='native_claim' then
   -- Read immutable provider/target so Edge can prepare the encrypted verifier.
   return jsonb_build_object('provider',req.provider,'target',req.target);
  end if;
  if req.provider<>p_data->>'provider' or req.target is distinct from (p_data->>'target')::uuid then raise exception 'invalid_state'; end if;
  result:=public.yl_cast_rpc_v1('start',p_owner,p_data);
  update yl_cast_private.yl_cast_native_requests
   set status='bound',transaction_id=(result->>'id')::uuid,browser_session_id=sid where id=req.id;
  return result;
 elsif p_action in ('native_status','native_cancel','native_confirm') then
  if req.session_id is distinct from sid or req.proof_hash is distinct from p_data->>'proof_hash' then raise exception 'invalid_state'; end if;
  if p_action='native_cancel' then
   if req.transaction_id is not null then perform public.yl_cast_rpc_v1('fail_transaction',p_owner,jsonb_build_object('transaction_id',req.transaction_id)); end if;
   update yl_cast_private.yl_cast_native_requests set status='cancelled' where id=req.id;
   return jsonb_build_object('status','cancelled');
  end if;
  if p_action='native_confirm' then
   if req.status<>'bound' or req.expires_at<=now() then raise exception 'invalid_state'; end if;
   perform public.yl_cast_rpc_v1('admit',p_owner,jsonb_build_object('session_id',req.browser_session_id));
   return public.yl_cast_rpc_v1('confirm',p_owner,jsonb_build_object('transaction_id',req.transaction_id));
  end if;
  if req.expires_at<=now() then return jsonb_build_object('status','expired'); end if;
  if req.status='cancelled' then return jsonb_build_object('status','cancelled'); end if;
  if req.transaction_id is null then return jsonb_build_object('status','waiting'); end if;
  select * into tx from yl_cast_private.yl_cast_transactions where id=req.transaction_id;
  if tx.status='pending' then
   perform public.yl_cast_rpc_v1('admit',p_owner,jsonb_build_object('session_id',tx.session_id));
   return jsonb_build_object('status','pending','transaction_id',tx.id,'provider',tx.provider,'account',tx.account);
  end if;
  return jsonb_build_object('status',case when tx.status in ('failed','confirmed') then tx.status else 'waiting' end);
 end if;
 raise exception 'not_found';
end $$;
revoke all on function public.yl_cast_rpc(text,uuid,jsonb) from public,anon,authenticated;
grant execute on function public.yl_cast_rpc(text,uuid,jsonb) to service_role;
commit;
