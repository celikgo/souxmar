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
import { invokeCommand } from "../tauri/bridge";

type Role = "user" | "assistant" | "tool" | "system";

interface Message {
  role:        Role;
  text:        string;
  toolCalls?:  Array<{ id: string; name: string; argumentsJson: string }>;
}

interface Props {
  projectId: string;
}

export function Chat({ projectId }: Props) {
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
  const scrollRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    scrollRef.current?.scrollTo({
      top: scrollRef.current.scrollHeight,
      behavior: "smooth",
    });
  }, [messages.length]);

  const send = async () => {
    const text = draft.trim();
    if (!text || busy) return;
    setDraft("");
    setBusy(true);

    const userMsg: Message = { role: "user", text };
    setMessages((m) => [...m, userMsg]);

    try {
      // Sprint 12+ wires this to a real provider call through the
      // souxmar-bridge crate. Today the command is a stub returning
      // a deterministic reply so the UI can be exercised in dev.
      const reply = await invokeCommand<string>("chat_send", {
        message: text,
        projectId: projectId || "",
      });
      setMessages((m) => [...m, { role: "assistant", text: reply }]);
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
          {projectId ? "scoped to project" : "no project context"}
        </span>
      </header>

      <div ref={scrollRef} style={scrollStyle}>
        {messages.map((m, i) => (
          <Bubble key={i} role={m.role} text={m.text} />
        ))}
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
          placeholder="Ask the agent…   (Cmd/Ctrl+Enter to send)"
          rows={3}
          style={inputStyle}
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

function Bubble({ role, text }: { role: Role; text: string }) {
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
    </div>
  );
}

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
