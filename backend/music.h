#ifndef ECHO_MUSIC_H
#define ECHO_MUSIC_H
#include "json.h"
#include <stddef.h>
/* Actual C data structures. Playlist nodes are bidirectional, queue uses FIFO linked
   nodes, recent is a bounded unique, recency-ordered array, undo is a LIFO stack, songs use a hash
   table, and song ordering uses merge sort plus binary search. */
#define RECENT_MAX 15
#define HASH_BUCKETS 257
#define MAX_SONGS 2048
#define MAX_PLAYLISTS 120

typedef struct Song {
 int id, duration, play_count, favorite;
 char title[160],artist[160],album[160],genre[90],audio_url[700];
 struct Song *next,*hash_next;
} Song;
typedef struct TrackNode {int song_id;struct TrackNode *prev,*next;} TrackNode;
typedef struct Playlist {
 int id, length;char name[120],created_at[45];
 TrackNode *head,*tail;struct Playlist *next;
} Playlist;
typedef struct QueueNode {int song_id;struct QueueNode *next;} QueueNode;
typedef struct Undo {
 int type,playlist_id,song_id,position,order_count;
 int *order;struct Undo *next;
} Undo;

void music_init(void);
void music_shutdown(void);
void music_state(JOut *out);
void music_search(const char *text,JOut *out);
void music_sorted(const char *sort,JOut *out);
void music_binary(const char *exact,JOut *out);
/* Returns 1 on success and fills message; 0 on bad command/invalid input. */
int music_action(const char *command,const char *body,char *message,size_t messagesize);
#endif
