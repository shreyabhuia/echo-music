# Echo JSON HTTP API

Base URL: `http://localhost:8000` (C process must be running).

### GET operations

| Route | Response |
|---|---|
| `/api/health` | Confirms native C backend is responding |
| `/api/state` | `songs`, `playlists`, `queue`, `recent`, `favorites`, `now_playing`, `active_playlist`, `can_undo`, `stats` |
| `/api/search?q=City` | Matching song objects; ID uses hash table; exact titles use binary search; partial match scans the library |
| `/api/binary-search?q=Golden%20Hour` | Exact title using binary search on a merge-sorted index |
| `/api/sort?by=title` | All songs alphabetically by merge sort |
| `/api/structures` | Algorithm names used by C backend |
| `/media/<filename>` | Uploaded or included audio bytes (supports `Range`) |

### POST operations

JSON Content-Type: `application/json`. URL pattern: `/api/action/<command>`.

| Command | JSON body |
|---|---|
| `add_song` | `{ "title":"Song", "artist":"Artist", "album":"", "genre":"", "duration":0, "audio_url":"" }` |
| `delete_song` | `{ "song_id":7 }` |
| `create_playlist` | `{ "name":"Road trip" }` |
| `rename_playlist` | `{ "playlist_id":1, "name":"Driving" }` |
| `delete_playlist` | `{ "playlist_id":1 }` |
| `activate_playlist` | `{ "playlist_id":1 }` |
| `add_to_playlist` | `{ "playlist_id":1, "song_id":2 }` |
| `add_many_to_playlist` | `{ "playlist_id":1, "song_ids":[2,3,4] }` — validates all selected library IDs and duplicates before adding the batch; one Undo removes the entire batch |
| `remove_from_playlist` | `{ "playlist_id":1, "song_id":2 }` |
| `shuffle` | `{ "playlist_id":1 }` |
| `undo` | `{}` |
| `toggle_favorite` | `{ "song_id":2 }` |
| `enqueue` | `{ "song_id":2 }` |
| `clear_queue` | `{}` |
| `play` | `{ "song_id":2 }` |
| `next`, `previous` | `{}` |

Success: `{ "message": "...", "state": {...} }`. Failed validation returns HTTP 400: `{ "error": "..." }`.

`POST /api/upload` accepts `multipart/form-data` with a file field named `file`. It returns HTTP 201 and a relative `audio_url` for passing to `add_song`. Max audio size: 15 MB.

### Example call in browser DevTools

```js
const response = await fetch('/api/binary-search?q=Golden%20Hour');
console.log(await response.json());

const r = await fetch('/api/action/enqueue', {
  method: 'POST',
  headers: {'Content-Type':'application/json'},
  body: JSON.stringify({song_id: 2})
});
console.log(await r.json());
```
