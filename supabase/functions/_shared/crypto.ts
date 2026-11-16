import { AppError } from "./http.ts";

const encode = new TextEncoder();
function base64(bytes: Uint8Array): string { return btoa(String.fromCharCode(...bytes)); }
function bytes(value: string): Uint8Array<ArrayBuffer> { return Uint8Array.from(atob(value), c => c.charCodeAt(0)); }
function url64(value: Uint8Array): string { return base64(value).replace(/=/g, "").replace(/\+/g, "-").replace(/\//g, "_"); }

export function randomToken() { return url64(crypto.getRandomValues(new Uint8Array(32))); }
export async function digest(value: string) {
  return [...new Uint8Array(await crypto.subtle.digest("SHA-256", encode.encode(value)))].map(b => b.toString(16).padStart(2, "0")).join("");
}
export async function challenge(verifier: string) {
  return url64(new Uint8Array(await crypto.subtle.digest("SHA-256", encode.encode(verifier))));
}

export interface Envelope { v: 1; kid: string; iv: string; ciphertext: string }

export class TokenVault {
  private keys = new Map<string, Promise<CryptoKey>>();
  constructor(private current: string, keyring: Record<string, string>) {
    for (const [id, value] of Object.entries(keyring)) {
      const raw = bytes(value);
      if (raw.length !== 32) throw new Error("vault_key_configuration");
      this.keys.set(id, crypto.subtle.importKey("raw", raw, "AES-GCM", false, ["encrypt", "decrypt"]));
    }
    if (!this.keys.has(current)) throw new Error("vault_key_configuration");
  }
  async seal(value: unknown, context: string): Promise<Envelope> {
    const iv = crypto.getRandomValues(new Uint8Array(12));
    const payload = encode.encode(JSON.stringify(value));
    if (payload.length > 40000) throw new Error("vault_payload_too_large");
    const encrypted = await crypto.subtle.encrypt({ name: "AES-GCM", iv, additionalData: encode.encode(context) }, await this.keys.get(this.current)!, payload);
    return { v: 1, kid: this.current, iv: base64(iv), ciphertext: base64(new Uint8Array(encrypted)) };
  }
  async open<T = Record<string, unknown>>(envelope: Envelope, context: string): Promise<T> {
    try {
      if (envelope.v !== 1 || !this.keys.has(envelope.kid)) throw new Error();
      const decoded = await crypto.subtle.decrypt({ name: "AES-GCM", iv: bytes(envelope.iv), additionalData: encode.encode(context) }, await this.keys.get(envelope.kid)!, bytes(envelope.ciphertext));
      return JSON.parse(new TextDecoder().decode(decoded)) as T;
    } catch { throw new AppError("vault_invalid", 500); }
  }
}
