# Echo — Project Demonstration & Viva Guide

## One-minute introduction

"Echo is a music management application with an HTML, CSS and JavaScript frontend and an actual C backend. JavaScript communicates with the C server using HTTP requests in JSON format. The backend implements doubly linked lists for playlists, a FIFO queue for Play Next, a bounded, unique recency array for the last 15 recently played songs, a stack for Undo, a hash table for song lookup, merge sort for sorting, binary search for exact-title lookup, and Fisher-Yates for shuffle. User data is persisted to a local JSON file."

## A practical live demo

1. Open `http://localhost:8000` after launching the C executable. The library starts empty; add or upload two audio tracks before demonstrating playback.
2. Select All Songs and use **Song A–Z**, **Artist A–Z**, and **Album A–Z** to demonstrate the three merge-sort comparators.
3. Search for a title or collaborator name you entered. For an exact-title demonstration, open `/api/binary-search?q=YOUR%20TITLE` in another tab.
4. Create a playlist, then add songs. State that its nodes have `prev` and `next` pointers in the C backend.
5. Play a track from the playlist and select Next/Previous. Show how moving along a linked list avoids shifting array elements.
6. Add two tracks using the ≡ queue button; choose Next twice and verify the FIFO order.
7. Remove a song from the playlist; press Undo to demonstrate the LIFO stack.
8. Shuffle the playlist; press Undo to restore the order.
9. Play the same song more than once and verify it moves to the front of Recently Played without duplication; the list never exceeds 15 unique songs.
10. Restart the server and show that playlists and songs are still saved in `data/state.json`.

## High-yield viva answers

**Q: Why C for the backend?**  C exposes pointers and memory management, making the implementation of linked lists, queues, and stacks clear and demonstrable.

**Q: How does frontend communicate with C?**  JavaScript `fetch()` sends HTTP JSON to a local socket server written in C. The C server parses commands and returns JSON.

**Q: Why a doubly linked list?**  Each playlist node has both `prev` and `next` pointers. Given the current node, forward/backward navigation is O(1); locating a node by arbitrary ID is O(n) in this implementation.

**Q: Why a queue for Play Next?**  First In, First Out (FIFO). The song enqueued earliest plays before later requests. Enqueue and dequeue are O(1) due to front and rear pointers.

**Q: How does Recently Played work?** The backend removes a song’s existing entry before moving it to the newest position. It keeps at most 15 unique song IDs. Searching and shifting cost O(R), where R ≤ 15, so this is bounded constant work for the chosen fixed cap.

**Q: What is a stack used for?**  Undo records reversible playlist changes. Undo always reverses the newest recorded change, following LIFO.

**Q: How does the hash table work?**  A song ID maps to an index by `ID % 257`. Collisions are handled by separate chaining. Average lookup is O(1); worst case is O(n).

**Q: What is merge sort?**  A divide-and-conquer sort that divides the songs, recursively sorts halves, then merges them. Time O(n log n); extra space O(n).

**Q: Where is binary search?**  For exact song-title lookup, the songs are sorted alphabetically, then the C `binary_title()` function halves the search interval each step. Binary search on an already sorted index is O(log n); preparing the index via merge sort costs O(n log n).

**Q: How does shuffle work?**  Fisher–Yates iterates from the last position to first and swaps each position with a random earlier position. It runs in O(n) time.

**Q: How is music actually played?**  The frontend HTML `<audio>` element plays audio URLs. The C server stores metadata, maintains DSA state and streams the audio files.

**Q: Is JSON a database?**  Here it is local file persistence. It is not a relational database. The actual data structures live in C memory; `data/state.json` saves a snapshot to survive restarts.

**Q: Can GitHub Pages run this whole project?**  No, because GitHub Pages only hosts static files. The C backend must run on a local computer or appropriate server.

## Team credits

- Shreya Bhuia — Roll I013 — SAP ID 70412500089
- Jessica Morris — Roll I047 — SAP ID 70412500031
- Anushka Pawar — Roll I051 — SAP ID 70412500026


## Bulk playlist additions and validation

The `add_many_to_playlist` action accepts a playlist ID and an array of song IDs. Before mutating the playlist, the C backend verifies that the playlist exists, every selected song exists in the library, none is already in the playlist, and the request does not repeat an ID. It then allocates the required nodes and a snapshot of the selected IDs before linking the nodes to the playlist. The batch is represented by one undo-stack record, so a single Undo reverses the whole batch.

Let `m` be the number of selected songs, `k` the existing playlist length, and `n` the number of library songs. Validation uses hash-table lookups (average O(1), worst O(n)), a linked-list membership check (O(k) per selected song), and pairwise duplicate detection among the submitted IDs (O(m²)). Therefore the average-time bound is O(mk + m² + m), with a conservative worst-case bound O(mn + mk + m²). Temporary IDs and node pointers use O(m) auxiliary space; the new playlist nodes also add O(m) persistent space. The frontend sends one request for the batch instead of one request per song.

## Interactive DSA sandbox

The About page contains a teaching-only visualizer for three abstract structures: append/remove at the tail of a doubly linked list, enqueue/dequeue on a FIFO queue, and push/pop on a LIFO stack. It is intentionally isolated from saved library and playlist data. It demonstrates pointer/ordering behavior, but it is not a debugger inspecting the C process's actual heap memory.

## Backend robustness notes

- Batch playlist additions validate all IDs and duplicates before changing playlist links.
- Allocation failures during bulk node preparation free the temporary allocations and leave the playlist unchanged.
- Rebuilding a playlist order allocates and validates replacement nodes before replacing the current list.
- Deleting a song removes it from the queue in place rather than allocating replacement queue nodes.
- Sorting now reports an out-of-memory result rather than dereferencing a missing sort buffer.
- Oversized media basenames are rejected instead of being truncated into a different path.
