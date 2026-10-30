// Touch-SNES relay helper for Cloudflare Workers.
// Hands out short-lived TURN logins from Cloudflare's relay, only to someone who knows your access code.
// Settings (Worker > Settings > Variables and secrets):
//   TURN_KEY_ID         your TURN key's ID (Realtime > TURN in the dashboard)
//   TURN_KEY_API_TOKEN  that key's API token (add it as a secret)
//   ACCESS_CODE         any code you make up; Touch-SNES sends it with each request (add it as a secret)
export default {
  async fetch(req, env) {
    const cors = { 'access-control-allow-origin': '*', 'access-control-allow-headers': 'content-type' };
    const json = (body, status = 200) => new Response(JSON.stringify(body), { status, headers: { ...cors, 'content-type': 'application/json', 'cache-control': 'no-store' } });
    if (req.method === 'OPTIONS') return new Response(null, { headers: cors });
    const url = new URL(req.url);
    if (!env.ACCESS_CODE || url.searchParams.get('code') !== env.ACCESS_CODE) return json({ error: 'Wrong access code' }, 403);
    if (!env.TURN_KEY_ID || !env.TURN_KEY_API_TOKEN) return json({ error: 'The Worker is missing TURN_KEY_ID or TURN_KEY_API_TOKEN' }, 500);
    const ttl = Math.min(86400, Math.max(600, Number(url.searchParams.get('ttl')) || 43200));
    const r = await fetch(`https://rtc.live.cloudflare.com/v1/turn/keys/${env.TURN_KEY_ID}/credentials/generate-ice-servers`, {
      method: 'POST',
      headers: { authorization: `Bearer ${env.TURN_KEY_API_TOKEN}`, 'content-type': 'application/json' },
      body: JSON.stringify({ ttl }),
    });
    if (!r.ok) return json({ error: `Cloudflare answered ${r.status}` }, 502);
    const data = await r.json();
    // Port 53 is blocked by browsers, so those addresses only slow things down.
    const iceServers = [].concat(data.iceServers || []).map(s => ({ ...s, urls: [].concat(s.urls).filter(u => !/:53(\?|$)/.test(u)) }));
    return json({ iceServers, expires: Date.now() + ttl * 1000 });
  },
};
