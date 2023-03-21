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

#define HASH_IMPL
#include "ulib/Hash.h"

/* SYMBOL TABLE */
#define EXP 12
#define LOADFACTOR 60
#define MAXLEN (1 << EXP) * LOADFACTOR / 100

// GCC bug with the syntax below ...
#pragma GCC diagnostic ignored "-Wmissing-braces"
#define HashInit { {0}, 0, 16 }

typedef struct {
  Span symbol;
  int16_t location;
} Entry;

typedef struct {
  Entry entries[1<<EXP];
  int32_t len;
  int16_t varindex;
} SymbolTable;

// Returns false if too many items in the hashtable
bool STAdd(SymbolTable* st, Span symbol, int16_t location) {
  assert(st);
  assert(SpanValid(symbol));

  if(MAXLEN < st->len) return false; // OOM

  uint64_t h = HashString(symbol.ptr, symbol.len);
  for(int32_t i = h;;) {
    i = HashLookup(h, EXP, i);
    Entry* e = &st->entries[i];
    // Insert it either if it already exist (overwrite) or if the cell is empty (0)
    if(SpanEqual(symbol, e->symbol) || e->symbol.ptr == 0) {
      st->len += e->symbol.ptr == 0;
      // Last wins, likely wrong. More likely should err on duplicated symbol.
      *e = (Entry) { symbol, location };
      return true;
    }
  }
}

// Returns -1 if symbol not present
int16_t STGet(SymbolTable* st, Span symbol) {
  assert(st);
  assert(SpanValid(symbol));

  uint64_t h = HashString(symbol.ptr, symbol.len);
  for(int32_t i = h;;) {
    i = HashLookup(h, EXP, i);
    Entry* e = &st->entries[i];

    if(e->symbol.ptr == 0) return -1;

    if(SpanEqual(symbol, e->symbol)) {
      return e->location;
    }
  }
}

SymbolTable* STInit() {
  static SymbolTable st = HashInit;


  return &st;
}
/* PARSE */

#define INSTR \
  X(push) \
  X(pop) \
  X(add) \
  X(sub)

typedef enum {
#define X(_n) _n,
  INSTR
#undef X
  Empty,
  Error
} TokenType;

// Not bothering with a union inside a struct as this is all going to be inlined anyhow with no loss of perf/mem
typedef struct {
  TokenType type;
  Span arg1;
  Span arg2;
} Token;

// As '/' is not a valid char in an instruction ...
Span removeLineComment(Span line) {
  return SpanCut(line, '/').head;
}

TokenType SpanToTokenType(Span s) {
#define X(_n) if(SpanEqual(s,S(#_n))) return _n;
  INSTR
#undef X
  return Error;
}
// *WARNING* The return value is valid until the next call to parseLine.
Token parseLine(Span line) {
  SpanValid(line);

  Span nocom = removeLineComment(line);
  Span s     = SpanTrim(nocom);
  Size len   = s.len;

  // Empty line
  if(len == 0) return (Token) {Empty, SPAN0, SPAN0};

  SpanPair sp   = SpanCut(s, ' ');
  Span cmd      = sp.head;
  SpanPair args = SpanCut(sp.tail, ' ');
  Span arg1     = SpanTrim(args.head);
  Span arg2     = SpanTrim(args.tail);
  TokenType tt  = SpanToTokenType(cmd);

  return (Token) {tt, arg1, arg2};
}

/* CODE */

SymbolTable* firstPass(SymbolTable* st, Span s) {
  (void)st;
  int16_t line = 0;

  while(true) {
    SpanPair sp = SpanCut(s, '\n');
    if(sp.head.len == 0) break;
    s = sp.tail;

    Token token = parseLine(sp.head);
  }

  return st;
}

#define Handle(_n) char* _n##f(SymbolTable* st, Token t, Buffer* bufout)

#define WriteSpan(_k) if(BufferCopy(_k,bufout).error) return "Writing buffer too small"
#define WriteStr(_s) WriteSpan(S(_s))
#define WriteStrNL(_s) WriteStr((_s));WriteStr("\n")
#define WriteA(_i) WriteStr("@");WriteSpan((_i));WriteStr("\n")

SpanResult fixedMap(Span segment) {
  #define SEGS \
    X(local, LCL) \
    X(this, THIS) \
    X(that, THAT) \
    X(pointer, THIS) \
    X(argument, ARG) \
    X(temp, TEMP)

  #define X(_i,_j) if(SpanEqual(segment, S(#_i))) return SPANRESULT(S(#_j));
  SEGS 
  #undef X
  return SPANERR("Not one of fixed mappings.");
}
// Hack instead of passing the filename to all functions
// At least there is just one .vm file compiled at the time
static char* VmFileName = NULL; 

char* SetAddr(Span segment, Span idx, Buffer* bufout) {
  SpanResult sr = fixedMap(segment);

  if(!sr.error) { // Found through simple mapping
    WriteA(sr.data);
    WriteStrNL("D=M");
    WriteA(idx);
    WriteStrNL("A=D+A");
    return NULL;
  } else if(SpanEqual(segment, S("constant"))) {
    WriteA(S("TEMP"));
    WriteStrNL("D=M");
    WriteA(idx);
    WriteStrNL("A=D+A");
    return NULL;
  } else if(SpanEqual(segment, S("static"))) {
    WriteStr(VmFileName);
    WriteStr(".");
    WriteSpan(idx);
    return NULL;
  } else {
    return "Not a known segment type.";
  }
}
Handle(push) {

  char* err = SetAddr(t.arg1, t.arg2, bufout);
  if(err) return err;
  WriteStr("D=M");

  WriteStr("@SP");
  WriteStr("A=M");
  WriteStr("M=D");

  WriteStr("@SP");
  WriteStr("M=M+1");
  
  return NULL;

}

Handle(pop) {

  char* err = SetAddr(t.arg1, t.arg2, bufout);
  if(err) return err;

  // TODO: think of how you can remove the tempoarary var
  WriteStr("@TEMP");
  WriteStr("M=A");

  WriteStr("@SP");
  WriteStr("M=M-1");
  WriteStr("D=M");


  WriteStr("@TEMP");
  WriteStr("A=M");
  WriteStr("M=D");

  return NULL;
}

#define popd \
  WriteStr("@SP"); \
  WriteStr("M=M-1"); \
  WriteStr("A=M"); \
  WriteStr("D=M");

#define popa \
  WriteStr("@SP"); \
  WriteStr("M=M-1"); \
  WriteStr("A=M");

#define pushd \
  WriteStr("@SP"); \
  WriteStr("A=M"); \
  WriteStr("M=D"); \
  WriteStr("@SP"); \
  WriteStr("M=M+1");

Handle(add) {

  popd
  popa

  WriteStr("D=D+M");

  pushd

  return NULL;
}

Handle(sub) {
  popd
  popa

  WriteStr("D=D-M");

  pushd

  return NULL;
}
#undef Write
#undef Handle

char* tokenToOps(SymbolTable* st, Token t, Buffer* bufout) {
  switch(t.type) {
#define X(_n) case _n: return _n##f(st, t, bufout);
    INSTR
#undef X
    case Empty: return NULL;
    case Error:
      return "An error occurred.";
  }
  return "Shouldn't get here as every branch returns.";
}
SpanResult secondPass(SymbolTable* st, Span s, Buffer* bufout) {
  (void) st;
  while(true) {
    SpanPair sp = SpanCut(s, '\n');
    Span line = sp.head;

    if(line.len == 0) break;
    s = sp.tail;


#define Write(_k) if(BufferCopy(_k,bufout).error) return SPANERR("Writing buffer too small")

    Write(S("// "));
    Write(line);
    Write(S("\n"));

    Token token = parseLine(line);
    char* error = tokenToOps(st, token, bufout);
    if(error) {
      return SPANERR(error); 
    }
    
    return SPANRESULT(BufferToSpan(bufout));
      
#undef Write
}

  return (SpanResult){BufferToSpan(bufout), 0};
}

void test(void);

int themain(int argc, char** argv) {
  #ifdef TEST
    test();
    return 0;
  #endif

  if(argc != 2) {
    fprintf(stderr, "Usage: %s <asm_file>\n", argv[0]);
    return -1;
  }

  VmFileName = argv[1];

  #define MAXFILESIZE 1<<20
  static Byte filein [MAXFILESIZE];
  static Byte fileout[MAXFILESIZE];

  Buffer bufin  = BufferInit(filein , MAXFILESIZE);
  Buffer bufout = BufferInit(fileout, MAXFILESIZE);

  // Load asm file
  SpanResult sr = OsSlurp(VmFileName, MAXFILESIZE, &bufin);
  if(sr.error) {
    fprintf(stderr, "Error reading file %s.\n%s\n", VmFileName, sr.error);
    return -1;
  }

  // Load the symbol table
  SymbolTable* st = STInit();
  st = firstPass(st, sr.data);

  // Produce binary code
  SpanResult sResult      = secondPass(st, sr.data, &bufout);
  if(sResult.error) {
    fprintf(stderr, "ERROR: %s\n", sResult.error);
    return -1;
  }
  Span s = sResult.data;

  // Save into output file
  Byte newName[1024];
  Buffer nbuf = BufferInit(newName, 1024);
  BufferCopy(SpanFromString(VmFileName), &nbuf);
  BufferCopy(S(".hack"), &nbuf);
  BufferPushByte(&nbuf, 0);

  char* writeError = OsFlash((char*)BufferToSpan(&nbuf).ptr, s);
  if(writeError) {
    fprintf(stderr, "%s\n", writeError);
    return -1;
  }
  return 0;
}

void SpanPuts(Span s) {
  fwrite(s.ptr,1,s.len,stdout);
  puts("\n");
  fflush(stdout);
}
void test() {
  #define TPARSE(_tt,_a1,_a2) { \
    Token t = parseLine(S(#_tt " " #_a1 " " #_a2)); \
    assert(t.type == _tt); \
    assert(SpanEqual(S(#_a1), t.arg1)); \
    assert(SpanEqual(S(#_a2), t.arg2)); \
    }

  TPARSE(push , const, 3);
  TPARSE(pop , const, 3);

  Byte b[1 << 10];
  Buffer buf = BufferInit(b, 1 << 10);
  SpanResult sr = secondPass(NULL, S("push const 3"), &buf);
  (void)secondPass;
}

