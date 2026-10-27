#include "music.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* Echo DSA engine, deliberately written in C instead of Python.
   No third-party database or JSON dependencies. State stored in data/state.json. */
static Song *songs, *songs_tail, *bucket[HASH_BUCKETS];
static Playlist *playlists,*playlists_tail;
static QueueNode *qfront,*qrear;
static int recent[RECENT_MAX], recent_count;
static Undo *undo_top;
static int song_count,playlist_count,next_song_id,next_playlist_id,active_playlist,now_playing;
static Playlist *current_playlist;
static TrackNode *current_node;

static void copy(char *dst,size_t size,const char *s){if(!size)return;snprintf(dst,size,"%s",s?s:"");}
static void trim(char *s){size_t n=strlen(s);while(n && isspace((unsigned char)s[n-1]))s[--n]=0;char *x=s;while(isspace((unsigned char)*x))x++;if(x!=s)memmove(s,x,strlen(x)+1);}
static unsigned hash_id(int id){return (unsigned)id % HASH_BUCKETS;}
static Song *song_by_id(int id){if(id<=0)return NULL;for(Song *s=bucket[hash_id(id)];s;s=s->hash_next)if(s->id==id)return s;return NULL;}
static Playlist *playlist_by_id(int id){for(Playlist *p=playlists;p;p=p->next)if(p->id==id)return p;return NULL;}
static void hash_insert(Song *s){unsigned h=hash_id(s->id);s->hash_next=bucket[h];bucket[h]=s;}
static void hash_remove(Song *s){unsigned h=hash_id(s->id);Song **cur=&bucket[h];while(*cur){if(*cur==s){*cur=s->hash_next;return;}cur=&(*cur)->hash_next;}}
static Song *create_song(int id,const char *title,const char *artist,const char *album,const char *genre,int duration,const char *url,int plays,int fav){
 if(song_count>=MAX_SONGS)return NULL;
 Song *s=(Song*)calloc(1,sizeof *s);if(!s)return NULL;
 s->id=id;s->duration=duration;s->play_count=plays;s->favorite=fav;
 copy(s->title,sizeof s->title,title);copy(s->artist,sizeof s->artist,artist);
 copy(s->album,sizeof s->album,album);copy(s->genre,sizeof s->genre,genre);
 copy(s->audio_url,sizeof s->audio_url,url);
 if(songs_tail)songs_tail->next=s;else songs=s;songs_tail=s;hash_insert(s);song_count++;
 if(next_song_id<=id)next_song_id=id+1;return s;
}
static Playlist *create_playlist(int id,const char *name,const char *created){
 if(playlist_count>=MAX_PLAYLISTS)return NULL;
 Playlist *p=(Playlist*)calloc(1,sizeof *p);if(!p)return NULL;
 p->id=id;copy(p->name,sizeof p->name,name);copy(p->created_at,sizeof p->created_at,created);
 if(playlists_tail)playlists_tail->next=p;else playlists=p;playlists_tail=p;playlist_count++;
 if(next_playlist_id<=id)next_playlist_id=id+1;return p;
}
static TrackNode *track_find(Playlist *p,int sid){for(TrackNode *n=p?p->head:NULL;n;n=n->next)if(n->song_id==sid)return n;return NULL;}
static int track_insert(Playlist *p,int sid,int position){
 if(!p||track_find(p,sid)||!song_by_id(sid))return 0;
 TrackNode *n=(TrackNode*)calloc(1,sizeof *n);if(!n)return 0;n->song_id=sid;
 if(position<=0||!p->head){n->next=p->head;if(p->head)p->head->prev=n;else p->tail=n;p->head=n;}
 else if(position>=p->length){n->prev=p->tail;p->tail->next=n;p->tail=n;}
 else {TrackNode *at=p->head;for(int i=0;i<position;i++)at=at->next;
  n->prev=at->prev;n->next=at;at->prev->next=n;at->prev=n;}
 p->length++;return 1;
}
static void sync_current(void){current_playlist=playlist_by_id(active_playlist);current_node=track_find(current_playlist,now_playing);}
static int track_remove(Playlist *p,int sid){
 TrackNode *n=track_find(p,sid);if(!n)return 0;
 if(n->prev)n->prev->next=n->next;else p->head=n->next;
 if(n->next)n->next->prev=n->prev;else p->tail=n->prev;
 free(n);p->length--;return 1;
}
static void track_clear(Playlist *p){TrackNode *n=p->head;while(n){TrackNode *next=n->next;free(n);n=next;}p->head=p->tail=NULL;p->length=0;}
static int enqueue(int sid){QueueNode *n=(QueueNode*)calloc(1,sizeof *n);if(!n)return 0;n->song_id=sid;if(qrear)qrear->next=n;else qfront=n;qrear=n;return 1;}
static int dequeue(void){if(!qfront)return 0;QueueNode *n=qfront;int sid=n->song_id;qfront=n->next;if(!qfront)qrear=NULL;free(n);return sid;}
static void queue_clear(void){while(qfront)dequeue();}
/* Recently Played is newest-first in the API, unique by song ID, and capped at 15.
   Internally keep oldest-to-newest so the newest item is always at the end. */
static void recent_add(int sid){
 int found=-1;
 for(int i=0;i<recent_count;i++)if(recent[i]==sid){found=i;break;}
 if(found>=0){for(int i=found;i<recent_count-1;i++)recent[i]=recent[i+1];recent_count--;}
 if(recent_count>=RECENT_MAX){for(int i=0;i<recent_count-1;i++)recent[i]=recent[i+1];recent_count--;}
 recent[recent_count++]=sid;
}
static void recent_drop(int sid){
 int write=0;for(int i=0;i<recent_count;i++)if(recent[i]!=sid)recent[write++]=recent[i];
 recent_count=write;
}
static void undo_clear(void){while(undo_top){Undo *n=undo_top->next;free(undo_top->order);free(undo_top);undo_top=n;}}
static void undo_push(int type,int pid,int sid,int position,int *order,int count){
 Undo *u=(Undo*)calloc(1,sizeof *u);if(!u){free(order);return;}
 u->type=type;u->playlist_id=pid;u->song_id=sid;u->position=position;u->order=order;u->order_count=count;u->next=undo_top;undo_top=u;
 /* Keep a reasonable stack bound for long-running demonstrations. */
 int c=0;for(Undo *x=undo_top;x;x=x->next)if(++c==100){Undo *tail=x->next;x->next=NULL;while(tail){Undo *n=tail->next;free(tail->order);free(tail);tail=n;}break;}
}
static int *track_order(Playlist *p){int *a=(int*)calloc((size_t)(p->length?p->length:1),sizeof(int));if(!a)return NULL;int i=0;for(TrackNode *n=p->head;n;n=n->next)a[i++]=n->song_id;return a;}
static int track_rebuild(Playlist *p,const int *order,int count){
 if(!p||count<0)return 0;
 TrackNode **nodes=(TrackNode**)calloc((size_t)(count?count:1),sizeof *nodes);if(!nodes)return 0;
 for(int i=0;i<count;i++){
  if(!song_by_id(order[i])){for(int j=0;j<i;j++)free(nodes[j]);free(nodes);return 0;}
  for(int j=0;j<i;j++)if(order[j]==order[i]){for(int k=0;k<i;k++)free(nodes[k]);free(nodes);return 0;}
  nodes[i]=(TrackNode*)calloc(1,sizeof **nodes);
  if(!nodes[i]){for(int j=0;j<i;j++)free(nodes[j]);free(nodes);return 0;}
  nodes[i]->song_id=order[i];
 }
 track_clear(p);
 for(int i=0;i<count;i++){nodes[i]->prev=i?nodes[i-1]:NULL;nodes[i]->next=i+1<count?nodes[i+1]:NULL;}
 p->head=count?nodes[0]:NULL;p->tail=count?nodes[count-1]:NULL;p->length=count;free(nodes);return 1;
}
static void play_song(int sid){Song *s=song_by_id(sid);if(!s)return;now_playing=sid;s->play_count++;recent_add(sid);sync_current();}
static void timestamp(char *buf,size_t size){time_t t=time(NULL);struct tm *u=gmtime(&t);if(!u){copy(buf,size,"2026-01-01T00:00:00Z");return;}strftime(buf,size,"%Y-%m-%dT%H:%M:%SZ",u);}
static int ccmp(const char *a,const char *b){while(*a&&*b){int d=tolower((unsigned char)*a)-tolower((unsigned char)*b);if(d)return d;a++;b++;}return tolower((unsigned char)*a)-tolower((unsigned char)*b);}
static int has_sub(const char *str,const char *needle){if(!*needle)return 1;for(;*str;str++){const char *a=str,*b=needle;while(*a&&*b&&tolower((unsigned char)*a)==tolower((unsigned char)*b)){a++;b++;}if(!*b)return 1;}return 0;}
static int cmp_title(Song *a,Song *b){int x=ccmp(a->title,b->title);return x?x:a->id-b->id;}
static int cmp_artist(Song *a,Song *b){int x=ccmp(a->artist,b->artist);if(!x)x=ccmp(a->title,b->title);return x?x:a->id-b->id;}
static int cmp_album(Song *a,Song *b){int x=ccmp(a->album,b->album);if(!x)x=ccmp(a->title,b->title);return x?x:a->id-b->id;}
typedef int (*SongCmp)(Song *,Song *);
static void msort(Song **a,Song **tmp,int lo,int hi,SongCmp cmp){
 if(hi-lo<2)return;int mid=(hi+lo)/2;msort(a,tmp,lo,mid,cmp);msort(a,tmp,mid,hi,cmp);
 int i=lo,j=mid,k=lo;while(i<mid&&j<hi)tmp[k++]=(cmp(a[i],a[j])<=0)?a[i++]:a[j++];
 while(i<mid)tmp[k++]=a[i++];while(j<hi)tmp[k++]=a[j++];for(i=lo;i<hi;i++)a[i]=tmp[i];
}
static Song **sorted_songs(SongCmp cmp,int *n){
 *n=song_count;Song **a=(Song**)calloc((size_t)(song_count+1),sizeof(Song*));if(!a)return NULL;
 int i=0;for(Song *s=songs;s;s=s->next)a[i++]=s;
 Song **tmp=(Song**)malloc((size_t)(i+1)*sizeof(Song*));
 if(!tmp){free(a);return NULL;}
 msort(a,tmp,0,i,cmp);free(tmp);return a;
}
/* Sorted song index enables O(log n) exact-title binary search. */
static Song *binary_title(const char *q){int n=0;Song **a=sorted_songs(cmp_title,&n);if(!a)return NULL;
 int lo=0,hi=n-1;Song *result=NULL;
 while(lo<=hi){int mid=lo+(hi-lo)/2;int cmp=ccmp(a[mid]->title,q);if(!cmp){result=a[mid];break;}if(cmp<0)lo=mid+1;else hi=mid-1;}
 free(a);return result;
}
static void jout_song(JOut *o,Song *s){
 jout_add(o,"{\"id\":%d,\"title\":",s->id);jout_str(o,s->title);
 jout_add(o,",\"artist\":");jout_str(o,s->artist);
 jout_add(o,",\"album\":");jout_str(o,s->album);
 jout_add(o,",\"genre\":");jout_str(o,s->genre);
 jout_add(o,",\"duration\":%d,\"audio_url\":",s->duration);jout_str(o,s->audio_url);
 jout_add(o,",\"play_count\":%d,\"favorite\":%s}",s->play_count,s->favorite?"true":"false");
}
void music_state(JOut *o){
 jout_add(o,"{\"songs\":[");int i=0;
 for(Song *s=songs;s;s=s->next){if(i++)jout_add(o,",");jout_song(o,s);}jout_add(o,"],\"playlists\":[");i=0;
 for(Playlist *p=playlists;p;p=p->next){if(i++)jout_add(o,",");jout_add(o,"{\"id\":%d,\"name\":",p->id);jout_str(o,p->name);
  jout_add(o,",\"created_at\":");jout_str(o,p->created_at);jout_add(o,",\"songs\":[");
  int j=0;for(TrackNode *n=p->head;n;n=n->next){if(j++)jout_add(o,",");jout_add(o,"%d",n->song_id);}jout_add(o,"]}");
 }
 jout_add(o,"],\"queue\":[");i=0;for(QueueNode *n=qfront;n;n=n->next){if(i++)jout_add(o,",");jout_add(o,"%d",n->song_id);}
 jout_add(o,"],\"recent\":[");for(i=0;i<recent_count;i++){if(i)jout_add(o,",");jout_add(o,"%d",recent[recent_count-1-i]);}
 jout_add(o,"],\"favorites\":[");i=0;int fav=0;
 for(Song *s=songs;s;s=s->next){if(s->favorite){if(i++)jout_add(o,",");jout_add(o,"%d",s->id);fav++;}}
 jout_add(o,"],\"now_playing\":");if(now_playing)jout_add(o,"%d",now_playing);else jout_add(o,"null");
 jout_add(o,",\"active_playlist\":");if(active_playlist)jout_add(o,"%d",active_playlist);else jout_add(o,"null");
 jout_add(o,",\"can_undo\":%s,\"stats\":{\"songs\":%d,\"playlists\":%d,\"favorites\":%d}}",undo_top?"true":"false",song_count,playlist_count,fav);
}
static void save_state(void){JOut o;jout_init(&o);music_state(&o);if(!o.p)return;
 FILE *f=fopen("data/state.json.tmp","wb");if(f){if(fwrite(o.p,1,o.len,f)==o.len){fclose(f);
#ifdef _WIN32
 remove("data/state.json");
#endif
 if(rename("data/state.json.tmp","data/state.json"))remove("data/state.json.tmp");
 }else{fclose(f);remove("data/state.json.tmp");}}
 jout_free(&o);
}
static int token_int(const char *json,JToken *t,int n,int obj,const char *key,int def){int k=jget(json,t,n,obj,key);return k>=0?jint(json,t[k],def):def;}
static void token_str(const char *json,JToken *t,int n,int obj,const char *key,char *out,size_t cap){int k=jget(json,t,n,obj,key);if(k>=0)jread(json,t[k],out,cap);else if(cap)out[0]=0;}
static int load_state(void){FILE *f=fopen("data/state.json","rb");if(!f)return 0;
 if(fseek(f,0,SEEK_END)){fclose(f);return 0;}long len=ftell(f);rewind(f);
 if(len<=0||len>4*1024*1024){fclose(f);return 0;}
 char *s=(char*)malloc((size_t)len+1);if(!s){fclose(f);return 0;}
 if(fread(s,1,(size_t)len,f)!=(size_t)len){free(s);fclose(f);return 0;}fclose(f);s[len]=0;
 JToken *t=(JToken*)calloc(100000,sizeof *t);if(!t){free(s);return 0;}
 int n=jparse(s,t,100000);if(n<=0 || t[0].type!=J_OBJECT){free(s);free(t);return 0;}
 int list=jget(s,t,n,0,"songs");
 if(list<0 || t[list].type!=J_ARRAY){free(s);free(t);return 0;}
 for(int i=list+1;i<n && t[i].parent==list;i=jskip(t,n,i)){
  char title[160],artist[160],album[160],genre[90],url[700];
  token_str(s,t,n,i,"title",title,sizeof title);token_str(s,t,n,i,"artist",artist,sizeof artist);
  token_str(s,t,n,i,"album",album,sizeof album);token_str(s,t,n,i,"genre",genre,sizeof genre);
  token_str(s,t,n,i,"audio_url",url,sizeof url);
  int k=jget(s,t,n,i,"favorite");int fav=k>=0&&s[t[k].start]=='t';
  create_song(token_int(s,t,n,i,"id",0),title,artist,album,genre,
   token_int(s,t,n,i,"duration",0),url,token_int(s,t,n,i,"play_count",0),fav);
 }
 list=jget(s,t,n,0,"playlists");if(list>=0&&t[list].type==J_ARRAY){
  for(int i=list+1;i<n&&t[i].parent==list;i=jskip(t,n,i)){
   char name[120],date[45];token_str(s,t,n,i,"name",name,sizeof name);token_str(s,t,n,i,"created_at",date,sizeof date);
   Playlist *p=create_playlist(token_int(s,t,n,i,"id",0),name,date);int x=jget(s,t,n,i,"songs");
   if(p&&x>=0&&t[x].type==J_ARRAY)for(int j=x+1;j<n&&t[j].parent==x;j=jskip(t,n,j))track_insert(p,jint(s,t[j],0),p->length);
  }
 }
 list=jget(s,t,n,0,"queue");if(list>=0&&t[list].type==J_ARRAY)for(int i=list+1;i<n&&t[i].parent==list;i=jskip(t,n,i)){int id=jint(s,t[i],0);if(song_by_id(id))enqueue(id);}
 list=jget(s,t,n,0,"recent");if(list>=0&&t[list].type==J_ARRAY){
  int items[RECENT_MAX],c=0;for(int i=list+1;i<n&&t[i].parent==list;i=jskip(t,n,i)){if(c<RECENT_MAX)items[c++]=jint(s,t[i],0);}
  /* Persisted JSON is newest-first; rebuild oldest-first and de-duplicate on load. */
  for(int i=c-1;i>=0;i--)if(song_by_id(items[i]))recent_add(items[i]);
 }
 now_playing=token_int(s,t,n,0,"now_playing",0);active_playlist=token_int(s,t,n,0,"active_playlist",0);
 if(!song_by_id(now_playing))now_playing=0;
 if(!playlist_by_id(active_playlist))active_playlist=playlists?playlists->id:0;
 sync_current();free(t);free(s);return 1;
}
void music_init(void){
 /* Start with a genuinely empty library. Songs and playlists are added by the user. */
 next_song_id=next_playlist_id=1;
 if(!load_state())save_state();
}
void music_shutdown(void){
 save_state();undo_clear();queue_clear();
 while(playlists){Playlist *p=playlists->next;track_clear(playlists);free(playlists);playlists=p;}
 while(songs){Song *s=songs->next;free(songs);songs=s;}
}
static void getstr(const char *json,JToken *t,int n,const char *key,char *to,size_t cap){token_str(json,t,n,0,key,to,cap);trim(to);}
static int getint(const char *json,JToken *t,int n,const char *key){return token_int(json,t,n,0,key,0);}
static void error(char *msg,size_t cap,const char *err){copy(msg,cap,err);}
int music_action(const char *command,const char *body,char *message,size_t messagesize){
 JToken tok[5000];int n=jparse(body?body:"{}",tok,5000);
 if(n<=0||tok[0].type!=J_OBJECT){error(message,messagesize,"Invalid JSON payload");return 0;}
 int sid=getint(body,tok,n,"song_id"),pid=getint(body,tok,n,"playlist_id");
 Song *s;Playlist *p;
 if(!strcmp(command,"add_song")){
  char title[160],artist[160],album[160],genre[90],url[700];
  getstr(body,tok,n,"title",title,sizeof title);getstr(body,tok,n,"artist",artist,sizeof artist);
  getstr(body,tok,n,"album",album,sizeof album);getstr(body,tok,n,"genre",genre,sizeof genre);
  getstr(body,tok,n,"audio_url",url,sizeof url);int duration=getint(body,tok,n,"duration");
  if(!*title||!*artist){error(message,messagesize,"Song title and artist are required");return 0;}
  if(song_count>=MAX_SONGS){error(message,messagesize,"Song library is full");return 0;}
  if(duration<0||duration>86400){error(message,messagesize,"Invalid duration");return 0;}
  if(*url&&strncmp(url,"https://",8)&&strncmp(url,"/media/",7)){error(message,messagesize,"Audio URL must begin with https:// or /media/");return 0;}
  if(!create_song(next_song_id,title,artist,album,genre,duration,url,0,0)){error(message,messagesize,"Out of memory");return 0;}
  copy(message,messagesize,"Song added to library");
 }else if(!strcmp(command,"delete_song")){
  s=song_by_id(sid);if(!s){error(message,messagesize,"Song not found");return 0;}
  for(p=playlists;p;p=p->next)track_remove(p,sid);
  QueueNode **qp=&qfront;qrear=NULL;while(*qp){QueueNode *node=*qp;if(node->song_id==sid){*qp=node->next;free(node);continue;}qrear=node;qp=&node->next;}if(qrear)qrear->next=NULL;
  recent_drop(sid);undo_clear();if(now_playing==sid)now_playing=0;
  hash_remove(s);Song **pp=&songs;while(*pp && *pp!=s)pp=&(*pp)->next;
  if(*pp){*pp=s->next;if(songs_tail==s)songs_tail=NULL;if(!songs_tail)for(Song *z=songs;z;z=z->next)songs_tail=z;}
  free(s);song_count--;copy(message,messagesize,"Song removed from library");
 }else if(!strcmp(command,"create_playlist")){
  char name[120],date[45];getstr(body,tok,n,"name",name,sizeof name);
  if(!*name){error(message,messagesize,"Enter a playlist name");return 0;}
  timestamp(date,sizeof date);p=create_playlist(next_playlist_id,name,date);
  if(!p){error(message,messagesize,"Could not create playlist");return 0;}
  active_playlist=p->id;copy(message,messagesize,"Playlist created");
 }else if(!strcmp(command,"rename_playlist")){
  char name[120];getstr(body,tok,n,"name",name,sizeof name);
  if(!(p=playlist_by_id(pid))){error(message,messagesize,"Playlist not found");return 0;}
  if(!*name){error(message,messagesize,"Enter a playlist name");return 0;}
  copy(p->name,sizeof p->name,name);copy(message,messagesize,"Playlist renamed");
 }else if(!strcmp(command,"delete_playlist")){
  p=playlist_by_id(pid);if(!p){error(message,messagesize,"Playlist not found");return 0;}
  Playlist **pp=&playlists;while(*pp!=p)pp=&(*pp)->next;*pp=p->next;
  if(playlists_tail==p){playlists_tail=NULL;for(Playlist *z=playlists;z;z=z->next)playlists_tail=z;}
  track_clear(p);free(p);playlist_count--;undo_clear();
  if(active_playlist==pid)active_playlist=playlists?playlists->id:0;
  copy(message,messagesize,"Playlist deleted");
 }else if(!strcmp(command,"activate_playlist")){
  if(!playlist_by_id(pid)){error(message,messagesize,"Playlist not found");return 0;}
  active_playlist=pid;copy(message,messagesize,"Playlist opened");
 }else if(!strcmp(command,"add_to_playlist")){
  p=playlist_by_id(pid);if(!p){error(message,messagesize,"Playlist not found");return 0;}
  if(!song_by_id(sid)){error(message,messagesize,"Song not found");return 0;}
  if(!track_insert(p,sid,p->length)){error(message,messagesize,"Song is already in this playlist");return 0;}
  undo_push(1,pid,sid,0,NULL,0);copy(message,messagesize,"Added to playlist");
 }else if(!strcmp(command,"add_many_to_playlist")){
  p=playlist_by_id(pid);if(!p){error(message,messagesize,"Playlist not found");return 0;}
  int arr=jget(body,tok,n,0,"song_ids");
  if(arr<0||tok[arr].type!=J_ARRAY){error(message,messagesize,"song_ids must be an array");return 0;}
  int ids[2048],count=0;
  for(int i=arr+1;i<n&&tok[i].parent==arr;i=jskip(tok,n,i)){
   int id=jint(body,tok[i],0);
   if(id<=0||!song_by_id(id)){error(message,messagesize,"One or more selected songs are not in the Music Library");return 0;}
   if(track_find(p,id)){error(message,messagesize,"One or more selected songs are already in this playlist");return 0;}
   for(int j=0;j<count;j++)if(ids[j]==id){error(message,messagesize,"Duplicate song IDs are not allowed");return 0;}
   if(count>=2048){error(message,messagesize,"Too many selected songs");return 0;}
   ids[count++]=id;
  }
  if(!count){error(message,messagesize,"Select at least one song");return 0;}
  TrackNode **nodes=(TrackNode**)calloc((size_t)count,sizeof *nodes);
  int *undo_ids=(int*)malloc((size_t)count*sizeof *undo_ids);
  if(!nodes||!undo_ids){free(nodes);free(undo_ids);error(message,messagesize,"Out of memory");return 0;}
  memcpy(undo_ids,ids,(size_t)count*sizeof *undo_ids);
  for(int i=0;i<count;i++){nodes[i]=(TrackNode*)calloc(1,sizeof **nodes);if(!nodes[i]){for(int j=0;j<i;j++)free(nodes[j]);free(nodes);free(undo_ids);error(message,messagesize,"Out of memory");return 0;}nodes[i]->song_id=ids[i];}
  for(int i=0;i<count;i++){
   TrackNode *node=nodes[i];node->prev=p->tail;
   if(p->tail)p->tail->next=node;else p->head=node;
   p->tail=node;p->length++;
  }
  free(nodes);undo_push(4,pid,0,0,undo_ids,count);snprintf(message,messagesize,"Added %d song%s to playlist",count,count==1?"":"s");
}else if(!strcmp(command,"remove_from_playlist")){
  p=playlist_by_id(pid);if(!p){error(message,messagesize,"Playlist not found");return 0;}
  int pos=0;TrackNode *node=p->head;while(node&&node->song_id!=sid){pos++;node=node->next;}
  if(!node){error(message,messagesize,"Song is not in playlist");return 0;}
  track_remove(p,sid);undo_push(2,pid,sid,pos,NULL,0);
  copy(message,messagesize,"Removed from playlist (Undo available)");
 }else if(!strcmp(command,"shuffle")){
  p=playlist_by_id(pid);if(!p){error(message,messagesize,"Playlist not found");return 0;}
  if(p->length<2){error(message,messagesize,"Add at least two songs to shuffle");return 0;}
  int *order=track_order(p);int *randomized=track_order(p);
  if(!order||!randomized){free(order);free(randomized);error(message,messagesize,"Out of memory");return 0;}
  /* Fisher-Yates unbiased shuffle; keep linked list nodes as the true playlist. */
  for(int i=p->length-1;i>0;i--){int j=rand()%(i+1);int tmp=randomized[i];randomized[i]=randomized[j];randomized[j]=tmp;}
  if(!track_rebuild(p,randomized,p->length)){free(order);free(randomized);error(message,messagesize,"Could not rebuild playlist order");return 0;}free(randomized);
  undo_push(3,pid,0,0,order,p->length);copy(message,messagesize,"Playlist shuffled with Fisher-Yates");
 }else if(!strcmp(command,"undo")){
  if(!undo_top){error(message,messagesize,"Nothing to undo");return 0;}
  Undo *u=undo_top;p=playlist_by_id(u->playlist_id);
  if(!p){error(message,messagesize,"Original playlist no longer exists");return 0;}
  undo_top=u->next;
  if(u->type==1)track_remove(p,u->song_id);
  else if(u->type==2)track_insert(p,u->song_id,u->position);
  else if(u->type==4){for(int i=0;i<u->order_count;i++)track_remove(p,u->order[i]);}
  else if(u->type==3){
   int *merged=(int*)malloc((size_t)(p->length+1)*sizeof(int));if(merged){int nmerged=0;
    for(int i=0;i<u->order_count;i++)if(track_find(p,u->order[i]))merged[nmerged++]=u->order[i];
    for(TrackNode *x=p->head;x;x=x->next){int seen=0;for(int i=0;i<nmerged;i++)if(merged[i]==x->song_id)seen=1;if(!seen)merged[nmerged++]=x->song_id;}
    track_rebuild(p,merged,nmerged);free(merged);
   }
  }
  free(u->order);free(u);copy(message,messagesize,"Last playlist change undone");
 }else if(!strcmp(command,"toggle_favorite")){
  s=song_by_id(sid);if(!s){error(message,messagesize,"Song not found");return 0;}
  s->favorite=!s->favorite;copy(message,messagesize,s->favorite?"Added to favorites":"Removed from favorites");
 }else if(!strcmp(command,"enqueue")){
  if(!song_by_id(sid)){error(message,messagesize,"Song not found");return 0;}
  if(!enqueue(sid)){error(message,messagesize,"Out of memory");return 0;}copy(message,messagesize,"Song added to Play Next queue");
 }else if(!strcmp(command,"clear_queue")){
  queue_clear();copy(message,messagesize,"Play Next queue cleared");
 }else if(!strcmp(command,"play")){
  if(!song_by_id(sid)){error(message,messagesize,"Song not found");return 0;}
  play_song(sid);copy(message,messagesize,"Now playing");
 }else if(!strcmp(command,"next") || !strcmp(command,"previous")){
  int chosen=0;p=playlist_by_id(active_playlist);
  /* Prev/next is O(1) from cached DLL node. Fall back to ends when outside playlist. */
  if(p!=current_playlist)sync_current();
  TrackNode *node=current_node;
  if(!strcmp(command,"next")){
   chosen=dequeue();
   if(!chosen)chosen=node?(node->next?node->next->song_id:0):(p&&p->head?p->head->song_id:0);
  }else chosen=node?(node->prev?node->prev->song_id:0):(p&&p->tail?p->tail->song_id:0);
  if(chosen){play_song(chosen);copy(message,messagesize,!strcmp(command,"next")?"Playing next track":"Playing previous track");}
  else{now_playing=0;copy(message,messagesize,"No more songs in this direction");}
 }else{error(message,messagesize,"Unknown action");return 0;}
 sync_current();save_state();return 1;
}
void music_search(const char *text,JOut *out){
 jout_add(out,"{\"results\":[");int i=0;
 /* Direct numeric IDs use the hash table: O(1) average. Exact titles use binary search. */
 char *end=NULL;long id=strtol(text,&end,10);
 Song *direct=(*text && end && !*end && id>0 && id<2147483647L)?song_by_id((int)id):NULL;
 Song *exact=(*text && !direct)?binary_title(text):NULL;
 for(Song *s=songs;s;s=s->next){if(!*text || s==direct || s==exact || has_sub(s->title,text) || has_sub(s->artist,text) || has_sub(s->album,text) || has_sub(s->genre,text)){
  if(i++)jout_add(out,",");jout_song(out,s);
 }}jout_add(out,"]}");
}
void music_binary(const char *text,JOut *out){Song *s=binary_title(text);jout_add(out,"{\"algorithm\":\"binary search\",\"results\":[");if(s)jout_song(out,s);jout_add(out,"]}");}
void music_sorted(const char *sort,JOut *out){SongCmp cmp=cmp_title;const char *label="song title";if(sort&&!strcmp(sort,"artist")){cmp=cmp_artist;label="artist";}else if(sort&&!strcmp(sort,"album")){cmp=cmp_album;label="album";}int n=0;Song **a=sorted_songs(cmp,&n);
 if(!a){jout_add(out,"{\"algorithm\":\"merge sort\",\"results\":[],\"error\":\"Out of memory\"}");return;}
 jout_add(out,"{\"algorithm\":\"merge sort\",\"sort_by\":");jout_str(out,label);jout_add(out,",\"results\":[");for(int i=0;i<n;i++){if(i)jout_add(out,",");jout_song(out,a[i]);}free(a);jout_add(out,"]}");
}
