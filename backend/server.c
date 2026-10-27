/* Echo local HTTP/JSON server implemented fully in C (no Flask/Node/Python backend).
   Routes mirror the existing frontend. Run from project root after compilation.
   Localhost-only by design: no authentication, no public deployment. */
#define _CRT_SECURE_NO_WARNINGS
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
#pragma comment(lib,"ws2_32.lib")
typedef SOCKET sock_t;
#define sock_close closesocket
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
typedef int sock_t;
#define sock_close close
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include "music.h"

#define MAX_BODY (17u*1024u*1024u)
#define MAX_HEADER 32768u
static unsigned upload_counter;
/* Optional CORS: set CORS_ORIGIN (an origin like https://echo.vercel.app, or the wildcard) when the frontend is hosted on a different origin. */
static char cors_origin[300];
static void cors_headers(char *out,size_t cap){
 if(!cors_origin[0]){if(cap)out[0]=0;return;}
 snprintf(out,cap,"Access-Control-Allow-Origin: %s\r\nVary: Origin\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Range\r\nAccess-Control-Expose-Headers: Content-Range, Accept-Ranges, Content-Length\r\nAccess-Control-Max-Age: 86400\r\n",cors_origin);
}

static int sendall(sock_t fd,const char *data,size_t n){while(n){int step=(int)(n>65536?65536:n);int sent=send(fd,data,step,0);if(sent<=0)return 0;data+=sent;n-=(size_t)sent;}return 1;}
static const char *reason(int status){switch(status){case 200:return "OK";case 201:return "Created";case 204:return "No Content";case 206:return "Partial Content";case 400:return "Bad Request";case 403:return "Forbidden";case 404:return "Not Found";case 405:return "Method Not Allowed";case 413:return "Payload Too Large";default:return "Internal Server Error";}}
static void response(sock_t fd,int code,const char *type,const char *body,size_t length,const char *extra){
 char cors[600];cors_headers(cors,sizeof cors);
 char head[1800];int n=snprintf(head,sizeof head,"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\nCache-Control: no-store\r\n%s%s\r\n",code,reason(code),type,length,cors,extra?extra:"");
 if(n>0&&(size_t)n<sizeof head && sendall(fd,head,(size_t)n) && body && length)sendall(fd,body,length);
}
static void response_json(sock_t fd,int code,const char *json){response(fd,code,"application/json; charset=utf-8",json,strlen(json),NULL);}
static void response_error(sock_t fd,int code,const char *err){JOut o;jout_init(&o);jout_add(&o,"{\"error\":");jout_str(&o,err);jout_add(&o,"}");response_json(fd,code,o.p?o.p:"{\"error\":\"Server error\"}");jout_free(&o);}
static int eq_ci(char a,char b){return tolower((unsigned char)a)==tolower((unsigned char)b);}
static int starts_ci(const char *s,const char *pref){while(*pref){if(!*s||!eq_ci(*s,*pref))return 0;s++;pref++;}return 1;}
static void header_value(const char *headers,const char *key,char *dest,size_t max){
 dest[0]=0;size_t kl=strlen(key);const char *start=strstr(headers,"\r\n");if(!start)return;start+=2;
 while(*start){const char *end=strstr(start,"\r\n");if(!end||end==start)break;
  if((size_t)(end-start)>kl && start[kl]==':' && starts_ci(start,key)){const char *val=start+kl+1;
   while(val<end&&(*val==' '||*val=='\t'))val++;
   size_t n=(size_t)(end-val);if(n>=max)n=max-1;memcpy(dest,val,n);dest[n]=0;return;}
  start=end+2;
 }
}
static const char *mime(const char *name){
 const char *dot=strrchr(name,'.');if(!dot)return "application/octet-stream";
 if(!strcmp(dot,".html"))return "text/html; charset=utf-8";
 if(!strcmp(dot,".css"))return "text/css; charset=utf-8";
 if(!strcmp(dot,".js"))return "application/javascript; charset=utf-8";
 if(!strcmp(dot,".svg"))return "image/svg+xml";
 if(!strcmp(dot,".wav"))return "audio/wav";
 if(!strcmp(dot,".mp3"))return "audio/mpeg";
 if(!strcmp(dot,".ogg"))return "audio/ogg";
 if(!strcmp(dot,".m4a"))return "audio/mp4";
 if(!strcmp(dot,".aac"))return "audio/aac";
 if(!strcmp(dot,".flac"))return "audio/flac";
 return "application/octet-stream";
}
static void serve_file(sock_t fd,const char *path,const char *range){
 FILE *f=fopen(path,"rb");if(!f){response_error(fd,404,"File not found");return;}
 if(fseek(f,0,SEEK_END)){fclose(f);response_error(fd,500,"Cannot read file");return;}
 long total=ftell(f);if(total<0){fclose(f);response_error(fd,500,"Cannot read file");return;}
 long start=0,end=total>0?total-1:0;
 int partial=0;if(starts_ci(range,"bytes=")){
  char *stop=NULL;long offset=strtol(range+6,&stop,10);
  if(stop!=range+6 && *stop=='-' && offset>=0 && offset<total){start=offset;partial=1;
   if(isdigit((unsigned char)stop[1])){long last=strtol(stop+1,NULL,10);if(last>=start && last<total)end=last;}
  }
 }
 long length=total?end-start+1:0;
 char extra[256];if(partial)snprintf(extra,sizeof extra,"Accept-Ranges: bytes\r\nContent-Range: bytes %ld-%ld/%ld\r\n",start,end,total);
 else snprintf(extra,sizeof extra,"Accept-Ranges: bytes\r\n");
 char cors[600];cors_headers(cors,sizeof cors);
 char head[1800];int n=snprintf(head,sizeof head,"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %ld\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n%s%s\r\n",partial?206:200,partial?"Partial Content":"OK",mime(path),length,cors,extra);
 if(n<=0||(size_t)n>=sizeof head||!sendall(fd,head,(size_t)n)){fclose(f);return;}
 fseek(f,start,SEEK_SET);char chunk[32768];while(length>0){size_t k=fread(chunk,1,(size_t)(length<(long)sizeof chunk?length:(long)sizeof chunk),f);if(!k||!sendall(fd,chunk,k))break;length-=(long)k;}
 fclose(f);
}
static int is_basename(const char *s){if(!*s)return 0;for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='.'&&*s!='-'&&*s!='_')return 0;return 1;}
static int hexdig(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static void url_decode(const char *s,char *out,size_t cap){size_t n=0;while(*s && n+1<cap){if(*s=='+' ){out[n++]=' ';s++;}
  else if(*s=='%'&&hexdig(s[1])>=0&&hexdig(s[2])>=0){out[n++]=(char)(hexdig(s[1])*16+hexdig(s[2]));s+=3;}
  else out[n++]=*s++;}out[n]=0;}
static void param(const char *full,const char *name,char *out,size_t cap){out[0]=0;const char *q=strchr(full,'?');if(!q)return;q++;
 size_t n=strlen(name);while(*q){if(!strncmp(q,name,n)&&q[n]=='='){const char *a=q+n+1;const char *b=strchr(a,'&');if(!b)b=a+strlen(a);char tmp[600];size_t sz=(size_t)(b-a);if(sz>=sizeof tmp)sz=sizeof tmp-1;memcpy(tmp,a,sz);tmp[sz]=0;url_decode(tmp,out,cap);return;}
  q=strchr(q,'&');if(!q)return;q++;}
}
static const char *bytes_find(const char *data,size_t n,const char *pattern,size_t pn){if(!pn||pn>n)return NULL;
 for(size_t i=0;i+pn<=n;i++)if(data[i]==pattern[0] && !memcmp(data+i,pattern,pn))return data+i;return NULL;}
static int allow_extension(const char *e){return !strcmp(e,".mp3")||!strcmp(e,".wav")||!strcmp(e,".ogg")||!strcmp(e,".m4a")||!strcmp(e,".aac")||!strcmp(e,".flac");}
static void handle_upload(sock_t fd,const char *body,size_t n,const char *ctype){
 const char *b=strstr(ctype,"boundary=");if(!b){response_error(fd,400,"Missing upload boundary");return;}b+=9;
 char boundary[130];size_t bn=0;if(*b=='"')b++;while(*b && *b!=';'&&*b!='"'&&!isspace((unsigned char)*b)&&bn+1<sizeof boundary)boundary[bn++]=*b++;boundary[bn]=0;
 if(!bn || bn>100){response_error(fd,400,"Invalid upload boundary");return;}
 const char *filepart=bytes_find(body,n,"name=\"file\"",11);
 if(!filepart){response_error(fd,400,"Select an audio file");return;}
 /* The disposition line contains both name="file" and filename="...". */
 const char *hstart=filepart;while(hstart>body+1 && memcmp(hstart-2,"\r\n",2))hstart--;
 const char *headers_end=bytes_find(filepart,n-(size_t)(filepart-body),"\r\n\r\n",4);
 if(!headers_end || headers_end-filepart>1500){response_error(fd,400,"Invalid multipart data");return;}
 const char *fn=bytes_find(hstart,(size_t)(headers_end-hstart),"filename=\"",10);
 if(!fn){response_error(fd,400,"Filename missing");return;}fn+=10;const char *fnend=memchr(fn,'"',(size_t)(headers_end-fn));
 if(!fnend || fnend==fn){response_error(fd,400,"Invalid filename");return;}
 char name[270];size_t sz=(size_t)(fnend-fn);if(sz>=sizeof name)sz=sizeof name-1;memcpy(name,fn,sz);name[sz]=0;
 char *slash=strrchr(name,'/'),*slash2=strrchr(name,'\\');if(slash2 && (!slash||slash2>slash))slash=slash2;char *base=slash?slash+1:name;
 char *dot=strrchr(base,'.');if(!dot){response_error(fd,400,"Audio file extension is required");return;}
 for(char *p=dot;*p;p++)*p=(char)tolower((unsigned char)*p);
 if(!allow_extension(dot)){response_error(fd,400,"Use an MP3, WAV, OGG, M4A, AAC or FLAC file");return;}
 const char *payload=headers_end+4;size_t remaining=n-(size_t)(payload-body);
 char mark[140];snprintf(mark,sizeof mark,"\r\n--%s",boundary);
 const char *end=bytes_find(payload,remaining,mark,strlen(mark));if(!end){response_error(fd,400,"Upload was incomplete");return;}
 size_t length=(size_t)(end-payload);
 if(length<=128 || length>=15u*1024u*1024u){response_error(fd,400,"Audio must be between 128 bytes and 15 MB");return;}
 if(!strcmp(dot,".wav") && (length<12 || memcmp(payload,"RIFF",4)||memcmp(payload+8,"WAVE",4))){response_error(fd,400,"Invalid WAV file");return;}
 if(!strcmp(dot,".mp3") && memcmp(payload,"ID3",3) && (unsigned char)payload[0]!=255){response_error(fd,400,"Invalid MP3 file");return;}
 char path[320],url[250];unsigned nonce=(unsigned)time(NULL)^((unsigned)rand()<<1)^++upload_counter;
 snprintf(url,sizeof url,"/media/user_%08x%s",nonce,dot);snprintf(path,sizeof path,".%s",url);
 FILE *f=fopen(path+2,"wb"); /* Convert ./media/... to media/... for Windows and Unix. */
 if(!f){response_error(fd,500,"Could not store audio; check the media folder");return;}
 if(fwrite(payload,1,length,f)!=length){fclose(f);remove(path+2);response_error(fd,500,"Could not write audio file");return;}
 fclose(f);JOut o;jout_init(&o);jout_add(&o,"{\"audio_url\":");jout_str(&o,url);jout_add(&o,",\"original_name\":");jout_str(&o,base);jout_add(&o,"}");response_json(fd,201,o.p);jout_free(&o);
}
static void handle(sock_t fd){
 char *req=(char*)malloc(MAX_BODY+MAX_HEADER+1);if(!req){response_error(fd,500,"Out of memory");return;}
 size_t cap=MAX_BODY+MAX_HEADER,have=0,headerlen=0;
 while(have<MAX_HEADER){int count=recv(fd,req+have,4096,0);if(count<=0)break;have+=(size_t)count;req[have]=0;
  char *end=strstr(req,"\r\n\r\n");if(end){headerlen=(size_t)(end-req)+4;break;}
 }
 if(!headerlen){response_error(fd,400,"Missing request headers");free(req);return;}
 char method[12]={0},path[2048]={0};if(sscanf(req,"%11s %2047s",method,path)!=2){response_error(fd,400,"Invalid request");free(req);return;}
 char type[350],ranger[200],lengthstr[45];header_value(req,"Content-Type",type,sizeof type);header_value(req,"Range",ranger,sizeof ranger);
 header_value(req,"Content-Length",lengthstr,sizeof lengthstr);
 char *endlen=NULL;unsigned long bodylen=*lengthstr?strtoul(lengthstr,&endlen,10):0;
 if(bodylen>MAX_BODY){response_error(fd,413,"Request is too large");free(req);return;}
 if(!strcmp(method,"POST")){
  if(!*lengthstr || !endlen || *endlen){response_error(fd,400,"Content-Length required");free(req);return;}
  while(have-headerlen<bodylen && have<cap){size_t left=bodylen-(have-headerlen);int count=recv(fd,req+have,(int)(left>65536?65536:left),0);if(count<=0)break;have+=(size_t)count;}
  if(have-headerlen<bodylen){response_error(fd,400,"Incomplete request body");free(req);return;}
  const char *body=req+headerlen;
  if(!strcmp(path,"/api/upload")){
   if(!starts_ci(type,"multipart/form-data;")){response_error(fd,400,"Expected multipart form-data");free(req);return;}
   handle_upload(fd,body,(size_t)bodylen,type);
  }else if(!strncmp(path,"/api/action/",12)){
   if(!starts_ci(type,"application/json")){response_error(fd,400,"Expected JSON");free(req);return;}
   if(bodylen>16384){response_error(fd,413,"JSON payload too large");free(req);return;}
   req[headerlen+bodylen]=0;
   char message[250];int ok=music_action(path+12,body,message,sizeof message);
   if(!ok)response_error(fd,400,message);
   else{JOut o;jout_init(&o);jout_add(&o,"{\"message\":");jout_str(&o,message);jout_add(&o,",\"state\":");music_state(&o);jout_add(&o,"}");response_json(fd,200,o.p);jout_free(&o);}
  }else response_error(fd,404,"Unknown API route");
 }else if(!strcmp(method,"GET")){
  if(!strcmp(path,"/api/state")){JOut o;jout_init(&o);music_state(&o);response_json(fd,200,o.p);jout_free(&o);}
  else if(!strcmp(path,"/api/health")){response_json(fd,200,"{\"ok\":true,\"service\":\"Echo C DSA Music Player\",\"backend\":\"C\"}");}
  else if(!strncmp(path,"/api/search",11) && (path[11]=='?'||!path[11])){char q[140];param(path,"q",q,sizeof q);JOut o;jout_init(&o);music_search(q,&o);response_json(fd,200,o.p);jout_free(&o);}
  else if(!strncmp(path,"/api/sort",9) && (path[9]=='?'||!path[9])){char by[40];param(path,"by",by,sizeof by);JOut o;jout_init(&o);music_sorted(by,&o);response_json(fd,200,o.p);jout_free(&o);}
  else if(!strncmp(path,"/api/binary-search",18) && (path[18]=='?'||!path[18])){char q[140];param(path,"q",q,sizeof q);JOut o;jout_init(&o);music_binary(q,&o);response_json(fd,200,o.p);jout_free(&o);}
  else if(!strcmp(path,"/api/structures"))response_json(fd,200,"{\"backend\":\"C\",\"structures\":[\"Doubly linked list\",\"FIFO linked queue\",\"Unique recent-history list (max 15)\",\"LIFO undo stack\",\"Hash table\",\"Merge sort\",\"Binary search\",\"Fisher-Yates shuffle\"]}");
  else{
   char actual[280];const char *filename=NULL;
   if(!strcmp(path,"/"))filename="index.html";
   else if(!strcmp(path,"/index.html"))filename="index.html";
   else if(!strcmp(path,"/styles.css"))filename="styles.css";
   else if(!strcmp(path,"/app.js"))filename="app.js";
   else if(!strcmp(path,"/favicon.svg"))filename="favicon.svg";
   else if(!strcmp(path,"/config.js"))filename="config.js";
   if(filename){snprintf(actual,sizeof actual,"static/%s",filename);serve_file(fd,actual,ranger);}
   else if(!strncmp(path,"/media/",7) && is_basename(path+7) && strlen(path+7)<sizeof actual-7){snprintf(actual,sizeof actual,"media/%s",path+7);serve_file(fd,actual,ranger);}
   else response_error(fd,404,"Not found");
  }
 }else if(!strcmp(method,"OPTIONS")){response(fd,204,"text/plain",NULL,0,NULL);
 }else response_error(fd,405,"Only GET, POST and OPTIONS are supported");
 free(req);
}
int main(void){
#ifdef _WIN32
 WSADATA wsa;if(WSAStartup(MAKEWORD(2,2),&wsa)){fprintf(stderr,"Winsock initialization failed\n");return 1;}
 _mkdir("data");_mkdir("media");
#else
 mkdir("data",0755);mkdir("media",0755);
#endif
 {const char *c=getenv("CORS_ORIGIN");if(c){size_t n=strlen(c);while(n&&(c[n-1]=='/'||isspace((unsigned char)c[n-1])))n--;if(n>=sizeof cors_origin)n=sizeof cors_origin-1;memcpy(cors_origin,c,n);cors_origin[n]=0;}}
 srand((unsigned)time(NULL));music_init();
 sock_t sock=socket(AF_INET,SOCK_STREAM,0);if(sock==INVALID_SOCKET){perror("socket");return 1;}
 int yes=1;setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof yes);
 int port=8000;const char *env=getenv("PORT");if(env){int x=atoi(env);if(x>1024&&x<65536)port=x;}
 struct sockaddr_in addr;memset(&addr,0,sizeof addr);addr.sin_family=AF_INET;{const char *host=getenv("HOST"); /* default: localhost only; cloud hosts set HOST=0.0.0.0 */
  if(host&&*host){struct in_addr ia;if(inet_pton(AF_INET,host,&ia)==1)addr.sin_addr=ia;else{fprintf(stderr,"Invalid HOST %s\n",host);return 1;}}
  else addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);}addr.sin_port=htons((unsigned short)port);
 if(bind(sock,(struct sockaddr*)&addr,sizeof addr)==SOCKET_ERROR || listen(sock,15)==SOCKET_ERROR){fprintf(stderr,"Cannot listen on port %d (in use?)\n",port);sock_close(sock);return 1;}
 printf("Echo C backend running at http://localhost:%d\n",port);
 printf("Frontend: HTML + CSS + JavaScript | Backend: ISO C + sockets\n");
 printf("Real data structures: DLL, linked FIFO queue, unique recent-history list (max 15), undo stack, hash table, merge sort, binary search.\n");
 printf("Data persisted to data/state.json. Press Ctrl+C to stop.\n");fflush(stdout);
 while(1){sock_t client=accept(sock,NULL,NULL);if(client==INVALID_SOCKET)continue;
#ifdef _WIN32
 DWORD timeout=15000;setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout,sizeof timeout);
#else
 struct timeval timeout={15,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout);
#endif
 handle(client);sock_close(client);
 }
 music_shutdown();sock_close(sock);
#ifdef _WIN32
 WSACleanup();
#endif
 return 0;
}
