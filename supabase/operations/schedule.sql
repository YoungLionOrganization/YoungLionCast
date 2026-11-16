-- Administrative setup after enabling Cron, pg_net and Vault in the Dashboard.
-- Vault entries: cast_functions_url and cast_maintenance_secret. No secrets in SQL history.
select cron.schedule(
  'younglion-cast-oauth-maintenance',
  '* * * * *',
  $job$
  select net.http_post(
    url := (select decrypted_secret from vault.decrypted_secrets where name='cast_functions_url') || '/oauth-maintenance',
    headers := jsonb_build_object(
      'Content-Type','application/json',
      'X-Cast-Maintenance',(select decrypted_secret from vault.decrypted_secrets where name='cast_maintenance_secret')
    ),
    body := '{}'::jsonb,
    timeout_milliseconds := 60000
  );
  $job$
);
