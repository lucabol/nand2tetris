#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define SPAN_IMPL
#include "ulib/Span.h"

#define BUFFER_IMPL
#include "ulib/Buffer.h"

#define OS_STDC_IMPL
#include "ulib/OsStdc.h"

#define TOKENS \
  X(keyword) \
  X(symbol) \
  X(stringConstant) \
  X(integerConstant) \
  X(identifier) \
  X(Eof)

#define X(n) n,
typedef enum { TOKENS } TokenType;
#undef X

#define X(n) #n,
static char* tokenNames[] = { TOKENS };
#undef X

typedef struct {
  TokenType type;
  Span value;
} Token;

typedef struct {
  Token token;
  Span rest;
  char* error;
} TokenResult;


TokenResult nextToken(Span data, Buffer* bufout) {
  return (TokenResult){0};
}

int themain(int argc, char** argv) {
  if(argc == 1) {
    fprintf(stderr, "Usage: %s <jack_files>\n", argv[0]);
    return -1;
  }


  #define MAXFILESIZE 1<<20
  static Byte fileout[MAXFILESIZE];
  Buffer bufout = BufferInit(fileout, MAXFILESIZE);


  // Processes all files
  for(int i = 1; i < argc; i++) {

    static Byte filein [MAXFILESIZE];
    Buffer bufin  = BufferInit(filein , MAXFILESIZE);

    // Load asm file
    SpanResult sr = OsSlurp(argv[i], MAXFILESIZE, &bufin);
    if(sr.error) {
      fprintf(stderr, "Error reading file %s.\n%s\n", argv[i], sr.error);
      return -1;
    }

    Span rest = sr.data;

    while(true) {
      TokenResult sResult = nextToken(rest, &bufout);
      if(sResult.error) {
        fprintf(stderr, "ERROR: %s\n", sResult.error);
        return -1;
      }
      if(sResult.token.type == Eof) break;
      rest = sResult.rest;
    }
  }

  // Save into output file
  Span s = BufferToSpan(&bufout);

  Byte newName[1024];
  Buffer nbuf = BufferInit(newName, 1024);
  Span oldName = SpanFromString(argv[1]);
  Span baseName = SpanRCut(oldName, '/').head; // just for linux

  BufferCopy(baseName, &nbuf);
  BufferCopy(S("/out.asm"), &nbuf);
  BufferPushByte(&nbuf, 0);

  char* writeError = OsFlash((char*)BufferToSpan(&nbuf).ptr, s);
  if(writeError) {
    fprintf(stderr, "%s\n", writeError);
    return -1;
  }
  return 0;
}
