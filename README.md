# Automation Systems Demo Lab

![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)
![Python](https://img.shields.io/badge/Python-FastAPI-3776AB?style=for-the-badge&logo=python&logoColor=white)
![Vue](https://img.shields.io/badge/Vue%203-Control%20Panel-42B883?style=for-the-badge&logo=vuedotjs&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-vcpkg-064F8C?style=for-the-badge&logo=cmake&logoColor=white)
![Security](https://img.shields.io/badge/Public%20Demo-Sanitized-2EA44F?style=for-the-badge)

## Overview

Automation Systems Demo Lab is a public portfolio repository that demonstrates production-style systems engineering, workflow automation, and local AI-assisted operations. It combines a C++23 native application foundation with a local multi-agent control panel built with FastAPI, Vue, and Ollama-friendly configuration.

The repository is intentionally structured as a sanitized demo: secrets, local runtime state, logs, build output, virtual environments, dependency caches, screenshots, and vendor SDK drops are excluded from version control.

## Business Problem

Operational teams often lose time to repeated manual checks, unclear execution handoffs, and inconsistent follow-through on routine technical tasks. This project demonstrates how those bottlenecks can be converted into repeatable, script-driven workflows with reviewable execution steps, local dashboards, and environment-aware automation.

The same patterns apply to business systems work: reduce administrative overhead, create auditable task flows, enforce repeatable operating standards, and give teams a safer interface for running technical processes without exposing private infrastructure or credentials.

## What This Repository Shows

- Local workflow automation with reviewable execution queues.
- A FastAPI backend that tracks runs, sessions, artifacts, and WebSocket updates.
- A Vue control panel for monitoring multi-step automation workflows.
- C++23 systems code with CMake presets, vcpkg dependency management, and Windows-native rendering scaffolding.
- Public-demo hygiene: dummy environment configuration, ignored runtime state, and no committed secrets.

## Tech Stack

| Area | Tools |
|---|---|
| Native systems | C++23, CMake, Ninja, DirectX 12 scaffolding |
| Dependency management | vcpkg, CMake presets |
| Backend automation | Python, FastAPI, Pydantic, HTTPX, Uvicorn |
| Frontend | Vue 3, Vite, Tailwind CSS |
| Local AI workflow | Ollama-compatible API configuration |
| DevOps hygiene | `.gitignore`, `.env.example`, sanitized public config |

## Repository Layout

```text
apps/                    Native demo application entry points
cmake/                   CMake helper modules
config/                  Public demo configuration and schemas
docs/                    Build and architecture notes
scripts/                 Local setup/bootstrap scripts
src/                     Core systems and renderer scaffolding
tests/                   C++ test harness
tools/agent_control_panel FastAPI + Vue automation dashboard
vendor_sdks/             Placeholder only; real SDK drops are ignored
```

## Security & Sanitization

This repository is prepared for public portfolio use:

- No `.env` files are tracked.
- Generated logs, screenshots, reports, runtime sessions, and build folders are ignored.
- Local machine identifiers were replaced with demo placeholders.
- Local absolute paths were removed from documentation and prompt examples.
- Vendor SDK binaries and licensed drops are excluded; only `vendor_sdks/README.md` is tracked.

Use `.env.example` as the only committed configuration template.

## How to Run

### Prerequisites

- Windows 10/11
- Visual Studio 2022 Build Tools with C++ workload
- Git
- CMake 3.28+
- Ninja
- Python 3.11+
- Node.js 20+
- Optional: Ollama for local model-backed workflow automation

### 1. Configure Environment

```powershell
Copy-Item .env.example .env
```

Edit `.env` locally if your Ollama URL, model names, or ports differ.

### 2. Configure Native Build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\setup_demo_workstation.ps1 -BootstrapVcpkg
```

Or configure manually with CMake:

```powershell
cmake --preset demo-workstation-debug
cmake --build --preset demo-workstation-debug
```

### 3. Run Agent Control Panel Backend

```powershell
cd .\tools\agent_control_panel\backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn app.main:app --reload --host 127.0.0.1 --port 8008
```

### 4. Run Agent Control Panel Frontend

```powershell
cd .\tools\agent_control_panel\frontend
npm install
npm run dev
```

Open `http://127.0.0.1:5173`.

## Portfolio Value

This project highlights the kind of engineering used to turn operational friction into controlled automation: structured configuration, local-first execution, reviewable task flow, repeatable build steps, and clean public documentation. It is suitable as a demonstration of business systems automation, DevOps readiness, and practical software delivery discipline.
