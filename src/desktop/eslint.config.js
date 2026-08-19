// SPDX-License-Identifier: Apache-2.0
//
// Flat config (ESLint 9). `npm run lint` was declared in package.json
// from the start but had no config file to read, so it exited 2 on every
// invocation and nothing was ever linted. This is that config.
//
// Scope is deliberately narrow: correctness rules that catch real bugs,
// not a style regime. Formatting belongs to the editor and to review;
// a lint rule that argues about whitespace only trains people to run
// --fix without reading it.

import js from "@eslint/js";
import tseslint from "typescript-eslint";
import reactHooks from "eslint-plugin-react-hooks";
import globals from "globals";

export default tseslint.config(
  {
    // Build output and dependencies are not ours to lint.
    ignores: ["dist/**", "node_modules/**", "src-tauri/**", "*.config.js"],
  },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    files: ["src/**/*.{ts,tsx}"],
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: "module",
      globals: { ...globals.browser },
    },
    plugins: { "react-hooks": reactHooks },
    rules: {
      ...reactHooks.configs.recommended.rules,

      // An unused variable is usually a half-finished edit. The
      // underscore prefix is the escape hatch for a deliberately unused
      // binding (a destructured field you are discarding, a required
      // callback parameter you do not need).
      "@typescript-eslint/no-unused-vars": [
        "error",
        {
          argsIgnorePattern: "^_",
          varsIgnorePattern: "^_",
          caughtErrorsIgnorePattern: "^_",
        },
      ],

      // `any` defeats the point of the typed bridge to the engine, but
      // it is occasionally the honest type at a boundary — warn so it
      // shows up in review rather than blocking a legitimate use.
      "@typescript-eslint/no-explicit-any": "warn",

      // Off, not warn. Every hit in this tree is a `\-` or `\[` inside a
      // character class, where the escape is redundant to the engine but
      // says "literal, not a range" to the reader. Rewriting working
      // regexes to satisfy a style rule is a bad trade.
      "no-useless-escape": "off",

      // These catch real defects rather than taste.
      eqeqeq: ["error", "always", { null: "ignore" }],
      "no-implicit-coercion": "error",
      // No allow-list. The tree already carries deliberate
      // `eslint-disable-next-line no-console` comments at the few places
      // console output is intended; an allow-list would render those
      // dead and let new stray logging in unremarked.
      "no-console": "warn",
    },
  },
);
