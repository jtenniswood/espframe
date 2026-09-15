import type { ConfigurationCapabilities, ConfigurationSnapshot, ConfigurationUpdateResponse, ConfigurationValues } from "./web_contracts";

export type ApiErrorKind = "offline" | "timeout" | "unavailable" | "validation" | "conflict" | "server";

export class EspframeApiError extends Error {
  constructor(
    readonly kind: ApiErrorKind,
    message: string,
    readonly status?: number,
    readonly code?: string,
    readonly field?: string,
  ) { super(message); this.name = "EspframeApiError"; }
}

export interface LegacySettingWrite {
  key: string;
  domain: string;
  url: string;
  value: string | number | boolean;
}

function object(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

export class EspframeApiClient {
  private queue: Promise<unknown> = Promise.resolve();
  private capabilities: Promise<ConfigurationCapabilities | null> | null = null;
  private negotiated: ConfigurationCapabilities | null | undefined;

  constructor(private readonly generated: ConfigurationCapabilities, private readonly timeoutMs = 5000) {}

  waitForWrites(): Promise<unknown> { return this.queue; }

  private enqueue<T>(command: () => Promise<T>): Promise<T> {
    const request = this.queue.then(command);
    this.queue = request.catch(() => undefined);
    return request;
  }

  private error(status: number, message: string, payload?: unknown): EspframeApiError {
    const body = object(payload) ? payload : {};
    const kind: ApiErrorKind = status === 404 || status === 405 ? "unavailable" :
      status === 409 ? "conflict" : status === 400 || status === 422 ? "validation" : "server";
    return new EspframeApiError(kind, typeof body.error === "string" ? body.error : message, status,
      typeof body.error === "string" ? body.error : undefined, typeof body.field === "string" ? body.field : undefined);
  }

  private async request(url: string, init: RequestInit = {}): Promise<Response> {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      return await fetch(url, { ...init, signal: controller.signal });
    } catch (error) {
      if (error && typeof error === "object" && (error as { name?: string }).name === "AbortError") {
        throw new EspframeApiError("timeout", "request_timeout");
      }
      throw new EspframeApiError("offline", "device_offline");
    } finally { clearTimeout(timer); }
  }

  private async json(response: Response, message: string): Promise<unknown> {
    let payload: unknown = null;
    try { payload = await response.json(); } catch (_) {
      if (!response.ok) throw this.error(response.status, message);
      throw new EspframeApiError("server", "invalid_server_response", response.status);
    }
    if (!response.ok) throw this.error(response.status, message, payload);
    return payload;
  }

  private parseCapabilities(value: unknown): ConfigurationCapabilities | null {
    if (!object(value) || typeof value.contract_version !== "number" || typeof value.api_version !== "number" ||
        typeof value.base_path !== "string" || typeof value.capabilities_path !== "string" ||
        typeof value.configuration_path !== "string" || value.update_mode !== "atomic" ||
        typeof value.configuration_available !== "boolean" || typeof value.configuration_read !== "boolean" ||
        typeof value.configuration_write !== "boolean" || typeof value.legacy_entity_api !== "boolean") return null;
    return value as unknown as ConfigurationCapabilities;
  }

  private negotiate(): Promise<ConfigurationCapabilities | null> {
    if (this.negotiated !== undefined) return Promise.resolve(this.negotiated);
    if (!this.capabilities) {
      this.capabilities = this.request(this.generated.capabilities_path, { cache: "no-store" })
        .then(response => this.json(response, "capabilities_unavailable"))
        .then(payload => {
          const capabilities = this.parseCapabilities(payload);
          // Older firmware may route this unknown path through the generic web
          // handler. Treat an unrecognised capabilities payload as legacy mode.
          if (!capabilities) { this.negotiated = null; return null; }
          this.negotiated = capabilities.configuration_available && capabilities.configuration_read && capabilities.configuration_write
            ? capabilities : null;
          return this.negotiated;
        }).catch(error => {
          if (error instanceof EspframeApiError && error.kind === "unavailable") { this.negotiated = null; return null; }
          this.capabilities = null;
          throw error;
        });
    }
    return this.capabilities;
  }

  async getConfigurationSnapshot(): Promise<ConfigurationSnapshot> {
    const capabilities = await this.negotiate();
    if (!capabilities) throw new EspframeApiError("unavailable", "configuration_api_unavailable");
    const payload = await this.json(await this.request(capabilities.configuration_path, { cache: "no-store" }), "configuration_api_failed");
    if (!object(payload) || payload.api_version !== capabilities.api_version || !object(payload.values) ||
        !Array.isArray(payload.unavailable) || !payload.unavailable.every(value => typeof value === "string")) {
      throw new EspframeApiError("server", "invalid_configuration_snapshot");
    }
    let apiKeyConfigured = typeof payload.api_key_configured === "boolean" ? payload.api_key_configured : undefined;
    const values: ConfigurationValues = {};
    for (const [key, value] of Object.entries(payload.values)) {
      if (key === "api_key") {
        if (apiKeyConfigured !== undefined) throw new EspframeApiError("server", "invalid_configuration_snapshot");
        apiKeyConfigured = !!value;
        continue;
      }
      if (typeof value !== "string" && typeof value !== "number" && typeof value !== "boolean") {
        throw new EspframeApiError("server", "invalid_configuration_snapshot");
      }
      values[key] = value;
    }
    if (apiKeyConfigured === undefined) apiKeyConfigured = false;
    return {
      api_version: capabilities.api_version,
      api_key_configured: apiKeyConfigured,
      values,
      unavailable: payload.unavailable as string[],
    };
  }

  private async legacyPost(url: string, body?: string): Promise<Response> {
    const response = await this.request(url, body === undefined ? { method: "POST" } : {
      method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded" }, body,
    });
    if (!response.ok) { let payload: unknown = null; try { payload = await response.json(); } catch (_) {} throw this.error(response.status, "legacy_write_failed", payload); }
    return response;
  }

  private async legacyWrite(setting: LegacySettingWrite): Promise<void> {
    if (setting.domain === "switch") { await this.legacyPost(setting.url + (setting.value ? "/turn_on" : "/turn_off")); return; }
    const body = new URLSearchParams({ [setting.domain === "select" ? "option" : "value"]: String(setting.value) }).toString();
    await this.legacyPost(setting.url + "/set", body);
  }

  updateSettings(values: ConfigurationValues, legacy: LegacySettingWrite[]): Promise<ConfigurationUpdateResponse | null> {
    return this.enqueue(async () => {
      const capabilities = await this.negotiate();
      if (!capabilities) { for (const setting of legacy) await this.legacyWrite(setting); return null; }
      const body = new URLSearchParams({ [capabilities.configuration_parameter || "configuration"]: JSON.stringify({ api_version: capabilities.api_version, values }) }).toString();
      try {
        const response = await this.request(capabilities.configuration_path, {
          method: "POST", headers: { "Content-Type": capabilities.configuration_encoding || "application/x-www-form-urlencoded" }, body,
        });
        const payload = await this.json(response, "configuration_update_failed");
        if (!object(payload) || payload.status !== "accepted") throw new EspframeApiError("server", "configuration_update_failed", response.status);
        await new Promise(resolve => setTimeout(resolve, 100));
        return payload as unknown as ConfigurationUpdateResponse;
      } catch (error) {
        if (!(error instanceof EspframeApiError) || error.kind !== "unavailable") throw error;
        this.negotiated = null;
        for (const setting of legacy) await this.legacyWrite(setting);
        return null;
      }
    });
  }

  post(url: string, params?: Record<string, string | number | boolean>): Promise<Response> {
    const query = params ? "?" + new URLSearchParams(Object.entries(params).map(([key, value]) => [key, String(value)])).toString() : "";
    return this.enqueue(() => this.legacyPost(url + query));
  }

  postText(url: string, value: string, useQueryFallback = false): Promise<Response> {
    return this.enqueue(() => {
      const body = new URLSearchParams({ value }).toString();
      const query = useQueryFallback && (url + "?" + body).length <= 120 ? "?" + body : "";
      return this.legacyPost(url + query, body);
    });
  }

  get(url: string): Promise<Response> { return this.request(url, { cache: "no-store" }); }
}
