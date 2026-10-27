# Echo — C-Powered DSA Music Management Application

**DSA Mini Project | MBA Tech (IT) | Semester 3**

Project members: **Shreya Bhuia (I013), Jessica Morris (I047), Anushka Pawar (I051)**

**Technology:** Frontend — HTML5, CSS3, vanilla JavaScript. **Backend — C11** (native sockets + real DSA structures). Data — local JSON file. Requests/responses — HTTP JSON. **No Python backend.**

## Windows: Run the project

1. Extract this ZIP into a folder (keep the folder structure as-is).
2. Install a **GCC C compiler** if you don't have one; MSYS2's MinGW-w64 GCC is one option. Ensure `gcc --version` works in Command Prompt. This app does **not** run using a Python server.
3. Double-click **`start_windows.bat`**. It compiles the C source and starts the local server. You can alternatively run from Command Prompt:

   ```bat
   gcc -std=c11 -O2 -o echo_server.exe backend\server.c backend\music.c backend\json.c -lws2_32
   echo_server.exe
   ```
4. Open **http://localhost:8000** in your browser.
5. Leave the terminal window open. Press Ctrl+C to stop the server.

If you get **port in use**, close the older music player first. If Windows Firewall prompts, permit local/private loopback only; this server binds to **127.0.0.1** and is not intended for exposure to the internet.

## macOS / Linux

From Terminal inside the extracted project folder:

```bash
bash start_mac_linux.sh
```

Alternatively:

```bash
cc -std=c11 -O2 -o echo_server backend/server.c backend/music.c backend/json.c
./echo_server
```

## Features — all powered by the C backend

| Feature | Data structure / algorithm | Where in C source |
|---|---|---|
| Playlist management + next/previous | Doubly linked list | `TrackNode`, `track_insert`, `track_remove`, cached `current_node` in `music.c` |
| Play Next prioritization | FIFO linked queue | `QueueNode`, `enqueue`, `dequeue` |
| Recently played (last 15 unique songs) | Bounded recency array | `recent[15]`, `recent_add` (deduplicates and moves replayed song to newest) |
| Undo playlist editing and shuffle | LIFO stack | `Undo`, `undo_push`, `undo` command |
| Fast search by song ID | Hash table | `bucket[257]`, `hash_id`, `song_by_id` |
| Sort songs alphabetically | Merge sort | `msort`, `music_sorted` |
| Find exact song title | Binary search | `binary_title` on merge-sorted song index |
| Randomize songs | Fisher–Yates shuffle | `shuffle` command |
| Audio playback | HTML `<audio>` | `static/app.js` |
| Upload audio | C multipart parser | `backend/server.c` |
| Persist library, history, playlists | Local JSON data file | `music_state`, `save_state`, `load_state` |

### What is saved?

Song library, audio paths, play counts, favorites, playlists, queue, recent history, and the active/current track are persisted to `data/state.json` as valid JSON. The undo stack is session-only, so it resets when restarting the server. The library and playlist list start empty. Deleting `data/state.json` resets the app to an empty library; add songs manually or upload your own audio files. The bundled WAV files are test fixtures and are not added to the library automatically. Playlist membership is restricted to song IDs that already exist in the music library. Bulk additions reject missing IDs, duplicates, and already-present songs before applying any changes; one Undo reverses the complete bulk-add operation. Open a playlist and use **Add songs** to select one or many unassigned library tracks in one request; the C backend validates the complete selection before changing the playlist. The playlist header shows track count and total duration. The **Shuffle** control randomizes that playlist's order with Fisher–Yates. The About page also includes an interactive teaching sandbox for doubly linked lists, FIFO queues and LIFO stacks; its demo operations do not modify saved music.

### Notes on music

Includes six **original computer-generated audio examples** for local testing, not Spotify streams. Add any personal music you have the right to use through **Upload audio** (up to 15 MB each; MP3, WAV, OGG, M4A, AAC or FLAC, subject to browser codec support). For a metadata-only song, Add Song accepts a direct HTTPS audio URL or an already uploaded `/media/...` URL.

## How frontend and backend connect

1. The browser loads `static/index.html`, `static/styles.css`, and `static/app.js` from the C HTTP server.
2. JavaScript sends `fetch('/api/action/create_playlist', { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({name:'Study'}) })`.
3. The native C HTTP server routes this command to `music_action()`, which creates a linked-list-based playlist and writes the snapshot to `data/state.json`.
4. The server returns JSON with `message` and the updated `state`. JavaScript updates the UI without refreshing.
5. The same C server streams audio (`/media/*.wav`, `/media/*.mp3`, etc.), including HTTP Range requests.

All significant state-changing operations happen in the **C backend**, not through fake frontend-only arrays. JSON is the transport and persistence format, not the DSA implementation language.

See `API.md` for the endpoints and `DSA_VIVA_GUIDE.md` for a demonstration script and explanations.

## Tests

The project includes 19 automated HTTP integration tests using Python **as a test client only**, not as part of the backend. You can run them with:

```bash
python tests/integration_test.py
```

The tests start the real C compiled server in an isolated temporary directory and do not overwrite your project data. All 19 passed during development on Linux with GCC. Windows GCC build is supplied but not tested on a Windows device in this environment.

## Deploying online

See **DEPLOY.md** for GitHub + Render (C backend with persistent disk) + Vercel (optional frontend).

## Important deployment limits (local run)

This is a **local academic demonstration**, not a hosted Spotify-like platform. **GitHub Pages alone cannot run a C server**. You may upload the code to GitHub as a repository, but running the full app requires starting the C program on a computer or deploying a native executable to a server. Localhost binds only to your computer. No accounts, licensed commercial catalogue, streaming rights, external user authentication, or public hosting are included.


### Playback responsiveness and playlists

The player starts the selected audio source optimistically when Play, Next, or Previous is pressed, while the C API confirms and persists the authoritative state in parallel. The audio element is loaded once per source change rather than reloaded twice, and unrelated actions such as favorites and playlist edits do not reset the audio element. This reduces perceived playback latency on localhost.


### Library sorting

The Music Library supports sorting by song title, artist, or album. Each option is handled by the C backend using merge sort and case-insensitive comparison; ties are resolved by title and then song ID. Creating a playlist shows immediate feedback while the local C API creates and persists the playlist.
