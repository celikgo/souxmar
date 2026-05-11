// SPDX-License-Identifier: Apache-2.0

import { useState } from "react";
import { Card, CardActions, PrimaryButton, SecondaryButton } from "./Card";
import { invokeCommand } from "../tauri/bridge";

export type Provider = "anthropic" | "openai" | "ollama";
export interface BYOKChoice {
  provider: Provider;
  validated: boolean;
}

export function BYOKStep({
  onNext, onSkip,
}: {
  onNext: (choice: BYOKChoice) => void;
  onSkip: () => void;
}) {
  const [provider, setProvider] = useState<Provider>("anthropic");
  const [key, setKey] = useState("");
  const [status, setStatus] = useState<"idle" | "saving" | "ok" | "error">("idle");
  const [errMsg, setErrMsg] = useState("");

  const save = async () => {
    setStatus("saving");
    setErrMsg("");
    try {
      await invokeCommand("byok_store_key", { provider, key });
      // Optional connectivity test. Ollama is the only provider for
      // which a network test is cheap + non-billed; for Anthropic /
      // OpenAI we skip the test to avoid charging a "first launch"
      // 1-token call against the user's account.
      let validated = false;
      if (provider === "ollama") {
        validated = await invokeCommand<boolean>("byok_test_connection", { provider });
      }
      setStatus("ok");
      onNext({ provider, validated });
    } catch (e) {
      setStatus("error");
      setErrMsg(String(e));
    }
  };

  return (
    <Card>
      <h1 style={{ margin: 0, fontSize: 24, fontWeight: 600 }}>
        Configure your AI provider
      </h1>
      <p style={{ color: "var(--fg-secondary)", marginTop: "var(--space-3)" }}>
        Bring your own key. Stored in the OS keychain — Keychain on
        macOS, Credential Manager on Windows, libsecret on Linux —
        never written to disk in plaintext.
      </p>

      <div style={{ marginTop: "var(--space-5)" }}>
        <label htmlFor="provider" style={labelStyle}>Provider</label>
        <select
          id="provider"
          value={provider}
          onChange={(e) => setProvider(e.target.value as Provider)}
          style={inputStyle}
        >
          <option value="anthropic">Anthropic (Claude)</option>
          <option value="openai">OpenAI (GPT)</option>
          <option value="ollama">Ollama (local — no key required)</option>
        </select>
      </div>

      {provider !== "ollama" && (
        <div style={{ marginTop: "var(--space-4)" }}>
          <label htmlFor="key" style={labelStyle}>API key</label>
          <input
            id="key"
            type="password"
            value={key}
            onChange={(e) => setKey(e.target.value)}
            placeholder={provider === "anthropic" ? "sk-ant-..." : "sk-..."}
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
          disabled={status === "saving" ||
                    (provider !== "ollama" && key.length === 0)}
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
