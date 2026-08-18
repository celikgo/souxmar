// SPDX-License-Identifier: Apache-2.0

import { useState } from "react";
import { Card, CardActions, PrimaryButton, SecondaryButton } from "./Card";
import { invokeCommand } from "../tauri/bridge";

// The provider ids here are the same vocabulary `project.ai.toml` uses,
// because that is exactly what the choice gets written into. Keeping one
// spelling across the UI, the settings file and the engine config is why
// a user can start in the app and carry on from the CLI.
export type Provider =
  | "anthropic"
  | "openai"
  | "grok"
  | "deepseek"
  | "groq"
  | "mistral"
  | "openrouter"
  | "together"
  | "ollama"
  | "openai-compatible";

interface ProviderInfo {
  id: Provider;
  label: string;
  /** Empty for providers that need no credential. */
  keyPrefix: string;
  /** A model the service actually serves, prefilled so the field is never blank. */
  defaultModel: string;
  note?: string;
}

// Ordered by how likely someone is to want them, not alphabetically.
const PROVIDERS: ProviderInfo[] = [
  { id: "anthropic", label: "Anthropic (Claude)", keyPrefix: "sk-ant-...",
    defaultModel: "claude-sonnet-4-20250514", note: "Strongest tool use." },
  { id: "openai", label: "OpenAI (GPT)", keyPrefix: "sk-...",
    defaultModel: "gpt-5" },
  { id: "grok", label: "xAI (Grok)", keyPrefix: "xai-...",
    defaultModel: "grok-4" },
  { id: "deepseek", label: "DeepSeek", keyPrefix: "sk-...",
    defaultModel: "deepseek-chat" },
  { id: "groq", label: "Groq", keyPrefix: "gsk_...",
    defaultModel: "llama-3.3-70b-versatile" },
  { id: "mistral", label: "Mistral", keyPrefix: "...",
    defaultModel: "mistral-large-latest" },
  { id: "openrouter", label: "OpenRouter", keyPrefix: "sk-or-...",
    defaultModel: "anthropic/claude-sonnet-4",
    note: "One key, many models — prefix the model with its vendor." },
  { id: "together", label: "Together AI", keyPrefix: "...",
    defaultModel: "meta-llama/Llama-3.3-70B-Instruct-Turbo" },
  { id: "ollama", label: "Ollama (local — no key required)", keyPrefix: "",
    defaultModel: "qwen2.5-coder:14b",
    note: "Runs on this machine. Nothing leaves it." },
  { id: "openai-compatible", label: "Other (OpenAI-compatible endpoint)", keyPrefix: "...",
    defaultModel: "",
    note: "LM Studio, vLLM, llama.cpp. Set base_url in project.ai.toml." },
];

export interface BYOKChoice {
  provider: Provider;
  model: string;
  validated: boolean;
}

function infoFor(id: Provider): ProviderInfo {
  return PROVIDERS.find((p) => p.id === id) ?? PROVIDERS[0];
}

export function BYOKStep({
  onNext, onSkip,
}: {
  onNext: (choice: BYOKChoice) => void;
  onSkip: () => void;
}) {
  const [provider, setProvider] = useState<Provider>("anthropic");
  const [model, setModel] = useState(infoFor("anthropic").defaultModel);
  const [key, setKey] = useState("");
  const [status, setStatus] = useState<"idle" | "saving" | "ok" | "error">("idle");
  const [errMsg, setErrMsg] = useState("");

  const info = infoFor(provider);
  const needsKey = provider !== "ollama";

  const changeProvider = (next: Provider) => {
    setProvider(next);
    // Carry the new provider's default across rather than leaving the
    // previous service's model id behind, which would 404.
    setModel(infoFor(next).defaultModel);
    setKey("");
    setStatus("idle");
    setErrMsg("");
  };

  const save = async () => {
    setStatus("saving");
    setErrMsg("");
    try {
      if (needsKey) {
        await invokeCommand("byok_store_key", { provider, key });
      }
      // Record the choice. Storing the key alone is not enough — the
      // engine needs to know which provider to route to, and it reads
      // that from the project config this preference is written into.
      await invokeCommand("byok_set_active", { provider, model: model.trim() });

      // Optional connectivity test. Ollama is the only provider for
      // which a network test is cheap + non-billed; for the hosted
      // services we skip it to avoid charging a "first launch" call
      // against the user's account.
      let validated = false;
      if (provider === "ollama") {
        validated = await invokeCommand<boolean>("byok_test_connection", { provider });
      } else {
        validated = await invokeCommand<boolean>("byok_has_key", { provider });
      }
      setStatus("ok");
      onNext({ provider, model: model.trim(), validated });
    } catch (e) {
      setStatus("error");
      setErrMsg(String(e));
    }
  };

  const incomplete =
    (needsKey && key.trim().length === 0) ||
    (provider !== "openai-compatible" && model.trim().length === 0);

  return (
    <Card>
      <h1 style={{ margin: 0, fontSize: 24, fontWeight: 600 }}>
        Connect a model
      </h1>
      <p style={{ color: "var(--fg-secondary)", marginTop: "var(--space-3)" }}>
        Bring your own key. It is stored in the OS keychain — Keychain on
        macOS, Credential Manager on Windows, libsecret on Linux — never
        written to disk in plaintext and never placed in your project files.
        Requests go straight from this machine to the provider.
      </p>

      <div style={{ marginTop: "var(--space-5)" }}>
        <label htmlFor="provider" style={labelStyle}>Provider</label>
        <select
          id="provider"
          value={provider}
          onChange={(e) => changeProvider(e.target.value as Provider)}
          style={inputStyle}
        >
          {PROVIDERS.map((p) => (
            <option key={p.id} value={p.id}>{p.label}</option>
          ))}
        </select>
        {info.note && (
          <p style={hintStyle}>{info.note}</p>
        )}
      </div>

      <div style={{ marginTop: "var(--space-4)" }}>
        <label htmlFor="model" style={labelStyle}>Model</label>
        <input
          id="model"
          type="text"
          value={model}
          onChange={(e) => setModel(e.target.value)}
          placeholder={info.defaultModel || "model id"}
          autoComplete="off"
          spellCheck={false}
          style={{ ...inputStyle, fontFamily: "var(--font-mono)" }}
        />
        <p style={hintStyle}>
          Model ids are not defaulted for you — services rename and retire
          them on their own schedule. Use one your account can reach.
        </p>
      </div>

      {needsKey && (
        <div style={{ marginTop: "var(--space-4)" }}>
          <label htmlFor="key" style={labelStyle}>API key</label>
          <input
            id="key"
            type="password"
            value={key}
            onChange={(e) => setKey(e.target.value)}
            placeholder={info.keyPrefix}
            autoComplete="off"
            spellCheck={false}
            style={{ ...inputStyle, fontFamily: "var(--font-mono)" }}
          />
        </div>
      )}

      {status === "error" && (
        <p style={{ color: "var(--danger)", marginTop: "var(--space-3)" }}>
          {errMsg}
        </p>
      )}

      <CardActions>
        <SecondaryButton onClick={onSkip} disabled={status === "saving"}>
          Skip for now
        </SecondaryButton>
        <PrimaryButton
          onClick={save}
          disabled={status === "saving" || incomplete}
        >
          {status === "saving" ? "Saving…" : "Save and continue"}
        </PrimaryButton>
      </CardActions>
    </Card>
  );
}

const labelStyle: React.CSSProperties = {
  display: "block",
  marginBottom: "var(--space-2)",
  fontSize: 12,
  fontWeight: 500,
  color: "var(--fg-secondary)",
  textTransform: "uppercase",
  letterSpacing: 0.5,
};

const inputStyle: React.CSSProperties = {
  width: "100%",
  padding: "var(--space-2) var(--space-3)",
  background: "var(--bg-elevated)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-md)",
  color: "var(--fg-primary)",
  fontSize: 14,
};

const hintStyle: React.CSSProperties = {
  margin: "var(--space-2) 0 0",
  fontSize: 12,
  lineHeight: 1.5,
  color: "var(--fg-tertiary, var(--fg-secondary))",
};
