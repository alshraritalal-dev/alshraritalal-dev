# Local Agent Control Panel

This module provides a local-first control surface for multi-agent workflow automation. It is designed for demo and portfolio use: runtime sessions, screenshots, generated artifacts, virtual environments, and logs are intentionally ignored by Git.

## Features

- FastAPI backend with WebSocket status streaming.
- Vue 3 + Vite frontend for task orchestration and review.
- Local Ollama integration through a configurable `OLLAMA_URL`.
- Step-by-step approval mode for file operations and shell commands.
- Runtime workspace, reports, and artifacts stored outside version control.

## Directory Structure

```text
tools/agent_control_panel/
|-- backend/
|   |-- app/
|   |-- requirements.txt
|-- frontend/
|   |-- index.html
|   |-- package.json
|   |-- src/
|-- runtime/        # generated locally, ignored by Git
```

## Configuration

Copy the root `.env.example` file to `.env` if you want to override defaults.

```powershell
Copy-Item ..\..\.env.example ..\..\.env
```

Default local services:

- API: `http://127.0.0.1:8008`
- Frontend: `http://127.0.0.1:5173`
- Ollama: `http://127.0.0.1:11434`

## Run Locally

### Backend

```powershell
cd .\tools\agent_control_panel\backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
uvicorn app.main:app --reload --host 127.0.0.1 --port 8008
```

### Frontend

```powershell
cd .\tools\agent_control_panel\frontend
npm install
npm run dev
```

Open `http://127.0.0.1:5173`.

## Security Notes

- Do not commit `.env`, runtime sessions, generated reports, screenshots, or local model output.
- Keep this service bound to localhost unless you add authentication and network hardening.
- Replace sample model names and ports through environment variables when adapting the demo.
