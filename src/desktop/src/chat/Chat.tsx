// SPDX-License-Identifier: Apache-2.0
//
// Sprint 11 push 4 — chat panel.
//
// The chat is the most-wired-up workbench surface today: it talks
// to the Provider abstraction (Sprint 10 push 9) via a Tauri command
// that forwards each message into the C++ side's ChatRequest builder
// and returns the assistant turn + any tool calls. Today the
// command itself (chat_send) is a stub on the Rust side; the wiring
// to libsouxmar-ai's dispatch_tool path lands when the souxmar-
// bridge FFI crate arrives (Sprint 12+).

import { useState, useRef, useEffect } from "react";
import {
  invokeCommand,
  type BridgeFeatureSet,
  type ChatSummary,
  type ChatToolCallSummary,
  type PendingToolSummary,
} from "../tauri/bridge";

type Role = "user" | "assistant" | "tool" | "system";

interface Message {
  role:        Role;
  text:        string;
  /** Tools the agent ran for this turn, straight from the dispatcher. */
  toolCalls?:  ChatToolCallSummary[];
}

interface Props {
  projectId: string;
  features:  BridgeFeatureSet;
}

export function Chat({ projectId, features }: Props) {
  const [messages, setMessages] = useState<Message[]>(() => [
    {
      role: "system",
      text:
        "I'm the souxmar agent. Open a project from the inspector " +
        "to give me something to look at, or ask me about plugins, " +
        "BCs, or solver capabilities.",
    },
  ]);
  const [draft, setDraft]   = useState("");
  const [busy,  setBusy]    = useState(false);
  // A turn that reached a tool needing approval. The agent is paused in
  // the engine until this is answered, so the composer is disabled and
  // the card below is the only way forward.
  const [pending, setPending] = useState<PendingToolSummary | null>(null);
  const scrollRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    scrollRef.current?.scrollTo({
      top: scrollRef.current.scrollHeight,
      behavior: "smooth",
    });
  }, [messages.length]);

  // Render whatever a turn produced: the tools it ran, its prose, and
  // any question it stopped on.
  const applyTurn = (summary: ChatSummary) => {
    if (summary.error) {
      setPending(null);
      setMessages((m) => [
        ...m,
        {
          role: "assistant",
          text: `(provider ${summary.provider} returned ${summary.error!.kind}: ${summary.error!.text})`,
          toolCalls: summary.tool_calls ?? [],
        },
      ]);
      return;
    }
    const calls = summary.tool_calls ?? [];
    if (calls.length > 0 || summary.reply_text) {
      setMessages((m) => [
        ...m,
        { role: "assistant", text: summary.reply_text, toolCalls: calls },
      ]);
    }
    setPending(summary.pending ?? null);
  };

  // Answer the agent's question. Declining is not a cancel: the model is
  // told, and gets a turn to say what it wanted to do.
  const decide = async (allow: boolean) => {
    if (!pending || busy) return;
    setBusy(true);
    setPending(null);
    setMessages((m) => [
      ...m,
      { role: "system", text: allow ? `Allowed \`${pending.tool_name}\`.` : `Declined \`${pending.tool_name}\`.` },
    ]);
    try {
      const summary = await invokeCommand<ChatSummary>("chat_confirm", {
        projectId: projectId || "",
        allow,
      });
      applyTurn(summary);
    } catch (err) {
      setMessages((m) => [
        ...m,
        { role: "assistant", text: `(could not resume the agent: ${String(err)})` },
      ]);
    } finally {
      setBusy(false);
    }
  };

  const send = async () => {
    const text = draft.trim();
    if (!text || busy || pending) return;
    setDraft("");
    setBusy(true);

    const userMsg: Message = { role: "user", text };
    setMessages((m) => [...m, userMsg]);

    try {
      // Sprint 14 push 4 — chat_send now returns a typed
      // ChatSummary through the bridge. When the C bridge isn't
      // linked (real-ffi off), the call comes back as
      // FeatureNotWired which Tauri surfaces as a string error;
      // otherwise we render reply_text + provider chip.
      const summary = await invokeCommand<ChatSummary>("chat_send", {
        message: text,
        projectId: projectId || "",
      });
      applyTurn(summary);
    } catch (err) {
      setMessages((m) => [
        ...m,
        {
          role: "assistant",
          text: `(provider call failed: ${String(err)})`,
        },
      ]);
    } finally {
      setBusy(false);
    }
  };

  return (
    <div style={containerStyle}>
      <header style={headerStyle}>
        <span style={{ fontWeight: 600 }}>Chat</span>
        <span style={{ marginLeft: "auto", color: "var(--fg-tertiary)", fontSize: 12 }}>
          {features.provider_call
            ? (projectId ? "scoped to project" : "no project context")
            : "scaffolding (provider not wired)"}
        </span>
      </header>

      <div ref={scrollRef} style={scrollStyle}>
        {messages.map((m, i) => (
          <Bubble key={i} role={m.role} text={m.text} toolCalls={m.toolCalls} />
        ))}
        {pending && (
          <ConfirmCard
            pending={pending}
            disabled={busy}
            onDecide={(allow) => void decide(allow)}
          />
        )}
        {busy && (
          <p style={{ color: "var(--fg-tertiary)", fontStyle: "italic" }}>
            Thinking…
          </p>
        )}
      </div>

      <footer style={footerStyle}>
        <textarea
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onKeyDown={(e) => {
            if (e.key === "Enter" && (e.metaKey || e.ctrlKey)) {
              e.preventDefault();
              void send();
            }
          }}
          placeholder={pending
            ? "Answer the request above to continue…"
            : "Ask the agent…   (Cmd/Ctrl+Enter to send)"}
          rows={3}
          disabled={Boolean(pending)}
          style={{ ...inputStyle, opacity: pending ? 0.6 : 1 }}
        />
        <button
          onClick={() => void send()}
          disabled={busy || !draft.trim()}
          style={sendBtnStyle}
        >
          Send
        </button>
      </footer>
    </div>
  );
}

function Bubble({
  role,
  text,
  toolCalls,
}: {
  role: Role;
  text: string;
  toolCalls?: ChatToolCallSummary[];
}) {
  const palette: Record<Role, { bg: string; fg: string; label: string }> = {
    user:      { bg: "var(--accent-soft)",    fg: "var(--fg-primary)",  label: "you"     },
    assistant: { bg: "var(--bg-elevated)",    fg: "var(--fg-primary)",  label: "agent"   },
    tool:      { bg: "var(--bg-canvas)",      fg: "var(--fg-secondary)",label: "tool"    },
    system:    { bg: "transparent",            fg: "var(--fg-tertiary)", label: "system"  },
  };
  const p = palette[role];
  return (
    <div style={{ marginBottom: "var(--space-3)" }}>
      <p
        style={{
          margin: 0,
          fontSize: 11,
          textTransform: "uppercase",
          letterSpacing: 0.5,
          color: "var(--fg-secondary)",
        }}
      >
        {p.label}
      </p>
      <p
        style={{
          margin: 0,
          marginTop: "var(--space-1)",
          padding: "var(--space-2) var(--space-3)",
          background: p.bg,
          color: p.fg,
          borderRadius: "var(--radius-md)",
          whiteSpace: "pre-wrap",
        }}
      >
        {text}
      </p>
      {/* What the agent actually did, from the dispatcher — shown even
          when the model says nothing, so a turn is never silent about
          having changed the project. */}
      {(toolCalls ?? []).map((call, i) => (
        <div key={`${call.name}-${i}`} style={toolRowStyle}>
          <span
            style={{
              ...toolBadgeStyle,
              color: call.ok
                ? "var(--success, var(--accent-default))"
                : call.refused
                  ? "var(--warning, #d9a441)"
                  : "var(--danger)",
            }}
          >
            {call.ok ? "ran" : call.refused ? "declined" : "failed"}
          </span>
          <code style={{ fontFamily: "var(--font-mono)" }}>{call.name}</code>
          <span style={{ color: "var(--fg-secondary)" }}>{call.summary}</span>
        </div>
      ))}
    </div>
  );
}

// The agent has asked to run something that needs a decision. It is
// paused in the engine until this is answered — declining is a real
// answer, not a cancel, and the model is told either way.
function ConfirmCard({
  pending,
  disabled,
  onDecide,
}: {
  pending: PendingToolSummary;
  disabled: boolean;
  onDecide: (allow: boolean) => void;
}) {
  return (
    <div style={confirmCardStyle}>
      <p style={{ margin: 0, fontSize: 11, textTransform: "uppercase", letterSpacing: 0.5, color: "var(--warning, #d9a441)" }}>
        needs your approval
      </p>
      <p style={{ margin: "var(--space-2) 0 0" }}>
        The agent wants to run <code style={{ fontFamily: "var(--font-mono)" }}>{pending.tool_name}</code>.
      </p>
      {pending.arguments && pending.arguments !== "{}" && (
        <pre style={confirmArgsStyle}>{pending.arguments}</pre>
      )}
      <div style={{ display: "flex", gap: "var(--space-2)", marginTop: "var(--space-3)" }}>
        <button type="button" disabled={disabled} onClick={() => onDecide(true)} style={allowButtonStyle}>
          Allow
        </button>
        <button type="button" disabled={disabled} onClick={() => onDecide(false)} style={denyButtonStyle}>
          Decline
        </button>
      </div>
    </div>
  );
}

const toolRowStyle: React.CSSProperties = {
  display: "flex",
  gap: "var(--space-2)",
  alignItems: "baseline",
  marginTop: "var(--space-1)",
  marginLeft: "var(--space-2)",
  fontSize: 12,
  lineHeight: 1.5,
};

const toolBadgeStyle: React.CSSProperties = {
  fontFamily: "var(--font-mono)",
  fontSize: 10,
  textTransform: "uppercase",
  letterSpacing: 0.5,
  flexShrink: 0,
};

const confirmCardStyle: React.CSSProperties = {
  marginBottom: "var(--space-3)",
  padding: "var(--space-3)",
  background: "var(--bg-elevated)",
  border: "1px solid var(--warning, #d9a441)",
  borderRadius: "var(--radius-md)",
};

const confirmArgsStyle: React.CSSProperties = {
  margin: "var(--space-2) 0 0",
  padding: "var(--space-2)",
  background: "var(--bg-canvas)",
  borderRadius: "var(--radius-sm)",
  fontFamily: "var(--font-mono)",
  fontSize: 11,
  overflowX: "auto",
  whiteSpace: "pre-wrap",
};

const allowButtonStyle: React.CSSProperties = {
  padding: "var(--space-2) var(--space-4)",
  background: "var(--accent-default)",
  color: "#fff",
  border: "none",
  borderRadius: "var(--radius-md)",
  fontSize: 13,
  fontWeight: 500,
  cursor: "pointer",
};

const denyButtonStyle: React.CSSProperties = {
  padding: "var(--space-2) var(--space-4)",
  background: "transparent",
  color: "var(--fg-primary)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-md)",
  fontSize: 13,
  cursor: "pointer",
};

const containerStyle: React.CSSProperties = {
  display: "flex",
  flexDirection: "column",
  height: "100%",
};

const headerStyle: React.CSSProperties = {
  display: "flex",
  alignItems: "center",
  padding: "var(--space-3) var(--space-4)",
  borderBottom: "1px solid var(--border-subtle)",
  fontSize: 13,
};

const scrollStyle: React.CSSProperties = {
  flex: 1,
  overflow: "auto",
  padding: "var(--space-3) var(--space-4)",
};

const footerStyle: React.CSSProperties = {
  display: "grid",
  gridTemplateColumns: "1fr auto",
  gap: "var(--space-2)",
  padding: "var(--space-3) var(--space-4)",
  borderTop: "1px solid var(--border-subtle)",
};

const inputStyle: React.CSSProperties = {
  width: "100%",
  padding: "var(--space-2)",
  background: "var(--bg-elevated)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-md)",
  color: "var(--fg-primary)",
  fontFamily: "inherit",
  fontSize: 13,
  resize: "none",
};

const sendBtnStyle: React.CSSProperties = {
  padding: "var(--space-2) var(--space-3)",
  background: "var(--accent-default)",
  color: "var(--fg-on-accent)",
  border: "none",
  borderRadius: "var(--radius-md)",
  fontWeight: 500,
  cursor: "pointer",
};
