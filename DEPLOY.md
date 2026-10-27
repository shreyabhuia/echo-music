# Deploying Echo (GitHub + Render + Vercel)

Echo's backend is a **native C server** that stores songs in `data/state.json` and audio in `media/`.
Vercel cannot run it (serverless, no persistent disk), so the architecture is:

```
Browser ──> Vercel (static frontend, optional) ──API calls──> Render (C server + persistent disk)
```

Render alone can serve the whole app (it also serves `static/`). Vercel is optional.

## 1. Push to GitHub
```bash
git init && git add . && git commit -m "Echo"
git branch -M main
git remote add origin https://github.com/<you>/echo.git
git push -u origin main
```
`data/state.json` and `media/user_*` are git-ignored, so your personal songs never go into the repo.

## 2. Render (backend, with persistence)
1. Render Dashboard > **New > Blueprint** > select the repo (it reads `render.yaml`).
2. It creates a Docker web service plus a **5 GB persistent disk mounted at `/var/data`**.
   Persistent disks are only available on **paid** instance types (the Starter plan in `render.yaml`).
   On the free plan files are wiped on every restart/redeploy/spin-down.
3. After deploy, open `https://<your-service>.onrender.com`. The full app already works here.
4. Upload songs, close the tab, reopen: they are still there (state + audio live on the disk).

## 3. Vercel (optional separate frontend)
1. In Render, set env var **`CORS_ORIGIN`** = your Vercel URL (e.g. `https://echo-music.vercel.app`). No trailing path.
2. Vercel > **Add New Project** > import the same repo. Leave the preset as "Other"; `vercel.json` sets the output directory to `static`.
3. Add env var **`ECHO_API_URL`** = `https://<your-service>.onrender.com` and deploy.
4. Order matters: if you only know the Vercel URL after the first deploy, set `CORS_ORIGIN` on Render afterwards and redeploy.

## Environment variables (C server)
| Var | Meaning | Default |
|---|---|---|
| `PORT` | listen port (Render sets it) | 8000 |
| `HOST` | bind address; `0.0.0.0` for cloud | 127.0.0.1 |
| `CORS_ORIGIN` | allowed frontend origin | off |
| `STORAGE_DIR` | disk mount used by `docker-entrypoint.sh` | /var/data |

## Security note
There is no login. Anyone with the URL can upload, delete and edit the shared library. Share the link carefully, or add authentication before making it public.
