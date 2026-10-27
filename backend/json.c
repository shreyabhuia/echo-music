#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* Small dependency-free JSON tokenizer (parent/child tree with unescaped string access).
   Each object stores direct key/value tokens in sequential order. */
static int pos,used,limit; static const char *src; static JToken *ts;
static void ws(void){while(src[pos] && (unsigned char)src[pos]<=32)pos++;}
static int val(int par){
 ws(); if(used>=limit)return -1;
 int idx=used++, ch=(unsigned char)src[pos];
 ts[idx].start=pos;ts[idx].parent=par;ts[idx].count=0;
 if(ch=='{'||ch=='['){
  ts[idx].type= ch=='{'?J_OBJECT:J_ARRAY; pos++;ws();
  char close=ch=='{'?'}':']';
  while(src[pos] && src[pos]!=close){
   if(src[pos]==',' || src[pos]==':'){pos++;continue;}
   if(val(idx)<0)return -1;ts[idx].count++;ws();
  }
  if(src[pos]!=close)return -1;pos++;ts[idx].end=pos;return idx;
 }
 if(ch=='"'){
  ts[idx].type=J_STRING;pos++;
  while(src[pos]){if(src[pos]=='\\'){if(!src[++pos])return -1;pos++;continue;}if(src[pos]=='"')break;pos++;}
  if(src[pos]!='"')return -1;pos++;ts[idx].end=pos;return idx;
 }
 if(ch=='-'||isdigit(ch)||ch=='t'||ch=='f'||ch=='n'){
  ts[idx].type=J_PRIMITIVE;
  while(src[pos] && !strchr(" \t\r\n,]}:",src[pos]))pos++;
  ts[idx].end=pos;return idx;
 }
 return -1;
}
int jparse(const char *s,JToken *tokens,int cap){
 if(!s||cap<=0)return -1;src=s;ts=tokens;used=pos=0;limit=cap;
 if(val(-1)<0)return -1;ws();return src[pos]? -1:used;
}
int jskip(const JToken *t,int n,int i){if(i<0||i>=n)return n;int end=t[i].end;i++;while(i<n && t[i].start<end)i++;return i;}
int jget(const char *s,const JToken *t,int n,int obj,const char *key){
 (void)s;if(obj<0 || obj>=n || t[obj].type!=J_OBJECT)return -1;
 int i=obj+1;while(i<n && t[i].parent==obj){
  if(t[i].type!=J_STRING)return -1;int len=t[i].end-t[i].start-2;
  int next=i+1;
  if(next<n && (int)strlen(key)==len && strncmp(s+t[i].start+1,key,(size_t)len)==0)return next;
  i=jskip(t,n,next);
 }
 return -1;
}
static int from_hex(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return 0;}
void jread(const char *s,JToken t,char *out,size_t max){
 if(!max)return;size_t k=0;int a=t.start+(t.type==J_STRING),b=t.end-(t.type==J_STRING);
 for(int i=a;i<b && k+1<max;i++){
  if(s[i]=='\\' && i+1<b){
   i++;switch(s[i]){
    case 'n':out[k++]='\n';break;case 'r':out[k++]='\r';break;case 't':out[k++]='\t';break;
    case 'u':if(i+4<b){int cp=(from_hex(s[i+1])<<12)|(from_hex(s[i+2])<<8)|(from_hex(s[i+3])<<4)|from_hex(s[i+4]);i+=4;out[k++]=(cp>=32&&cp<128)?(char)cp:'?';}break;
    default:out[k++]=s[i];
   }
  }else out[k++]=s[i];
 }
 out[k]='\0';
}
int jint(const char *s,JToken t,int def){char tmp[40];jread(s,t,tmp,sizeof tmp);if(!tmp[0]||!strcmp(tmp,"null"))return def;return (int)strtol(tmp,NULL,10);}
void jout_init(JOut *o){o->cap=4096;o->p=(char*)malloc(o->cap);o->len=0;if(o->p)o->p[0]=0;}
void jout_free(JOut *o){free(o->p);o->p=NULL;o->len=o->cap=0;}
static void reserve(JOut *o,size_t n){if(!o->p)return;if(o->len+n+1>o->cap){size_t m=o->cap;while(o->len+n+1>m)m*=2;char *p=(char*)realloc(o->p,m);if(!p){free(o->p);o->p=NULL;o->len=o->cap=0;}else{o->p=p;o->cap=m;}}}
void jout_add(JOut *o,const char *fmt,...){if(!o->p)return;va_list a,b;va_start(a,fmt);va_copy(b,a);int n=vsnprintf(NULL,0,fmt,b);va_end(b);if(n<0){va_end(a);return;}reserve(o,(size_t)n);if(o->p){vsnprintf(o->p+o->len,o->cap-o->len,fmt,a);o->len+=(size_t)n;}va_end(a);}
void jout_str(JOut *o,const char *s){
 jout_add(o,"\"");if(!s)s="";
 for(const unsigned char *p=(const unsigned char*)s;*p;p++){
  switch(*p){case '"':jout_add(o,"\\\"");break;case '\\':jout_add(o,"\\\\");break;case '\n':jout_add(o,"\\n");break;case '\r':jout_add(o,"\\r");break;case '\t':jout_add(o,"\\t");break;
  default: if(*p<32)jout_add(o,"\\u%04x",*p);else jout_add(o,"%c",*p);}
 }
 jout_add(o,"\"");
}
