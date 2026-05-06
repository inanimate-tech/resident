# Resident agent plugin

Stub Claude Code plugin for the Resident ESP32 sandbox device platform.

Status: **v0.1.0** — scaffolding only. Skills will land here as they are
written.

## Install (via marketplace)

```
/plugin marketplace add inanimate-tech/agent-plugins
/plugin install resident@inanimate
```

The `inanimate` marketplace tracks `main` of this repo via `git-subdir`, so
each install pulls the current contents of `tools/agent-plugin/`.

## Local development

From the root of the resident repo:

```
claude --plugin-dir ./tools/agent-plugin
```

That loads this plugin into the Claude Code session without going through
the marketplace.

## Layout

```
tools/agent-plugin/
├── .claude-plugin/
│   └── plugin.json    # Claude Code plugin manifest
├── skills/            # Portable skills (one subdir per skill)
└── README.md
```

Cross-tool manifests (e.g. `.codex-plugin/plugin.json`) and shared assets
(`scripts/`, `references/`, `assets/`, `.mcp.json`) can be added at this
level when needed.

## See also

- Marketplace: [`inanimate-tech/agent-plugins`](https://github.com/inanimate-tech/agent-plugins)
