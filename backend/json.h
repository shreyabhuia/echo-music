#ifndef ECHO_JSON_H
#define ECHO_JSON_H
#include <stddef.h>
typedef enum { J_OBJECT, J_ARRAY, J_STRING, J_PRIMITIVE } JType;
typedef struct { JType type; int start, end, count, parent; } JToken;
typedef struct { char *p; size_t len, cap; } JOut;
int jparse(const char *json, JToken *tokens, int capacity);
int jskip(const JToken *tok, int n, int index);
int jget(const char *s,const JToken *t,int n,int object_index,const char *key);
void jread(const char *s,JToken t,char *out,size_t outsize);
int jint(const char *s,JToken t,int defval);
void jout_init(JOut *o);
void jout_free(JOut *o);
void jout_add(JOut *o,const char *fmt,...);
void jout_str(JOut *o,const char *value);
#endif
