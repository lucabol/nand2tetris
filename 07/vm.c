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
  X(sub) \
  X(eq) \
  X(gt) \
  X(lt) \
  X(neg) \
  X(not) \
  X(and) \
  X(or) 

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
  (void)line;

  while(true) {
    SpanPair sp = SpanCut(s, '\n');
    if(sp.head.len == 0) break;
    s = sp.tail;

    Token token = parseLine(sp.head);
    (void) token;
  }

  return st;
}

#define Handle(_n) char* _n##f(SymbolTable* st, Token t, Buffer* bufout)

#define WriteSpan(_k) if(BufferCopy(_k,bufout).error) return "Writing buffer too small"
#define WriteStr(_s) WriteSpan(SpanFromString(_s))
#define WriteStrNL(_s) WriteStr((_s));WriteStr("\n")
#define WriteA(_i) WriteStr("@");WriteSpan((_i));WriteStr("\n")
#define WriteLabel(_i) WriteStr("(");WriteSpan((_i));WriteStr(")\n")

SpanResult fixedMap(Span segment) {
  #define SEGS \
    X(local, LCL) \
    X(this, THIS) \
    X(that, THAT) \
    X(argument, ARG) 

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
  } else if(SpanEqual(segment, S("constant"))) {
    WriteA(idx);
    WriteStrNL("D=A");
    WriteA(S("5"));
    WriteStrNL("M=D");
  } else if(SpanEqual(segment, S("static"))) {
    WriteStr("@");
    WriteStr(VmFileName);
    WriteStr(".");
    WriteSpan(idx);
    WriteStr("\n");
  } else if(SpanEqual(segment, S("temp"))) {
    WriteA(S("5")); // Bug?? TEMP not defined?
    WriteStrNL("D=A");
    WriteA(idx);
    WriteStrNL("A=D+A");
  } else if(SpanEqual(segment, S("pointer"))) {
    WriteA(S("THIS"));
    WriteStrNL("D=A");
    WriteA(idx);
    WriteStrNL("A=D+A");
  } else {
    return "Not a known segment type.";
  }
  return NULL;
}

Handle(push) {
  (void) st;
  (void) t;

  char* err = SetAddr(t.arg1, t.arg2, bufout);
  if(err) return err;
  WriteStrNL("D=M");

  WriteStrNL("@SP");
  WriteStrNL("A=M");
  WriteStrNL("M=D");

  WriteStrNL("@SP");
  WriteStrNL("M=M+1");
  
  return NULL;

}

#define popd \
  WriteStrNL("@SP"); \
  WriteStrNL("M=M-1"); \
  WriteStrNL("A=M"); \
  WriteStrNL("D=M");

#define popa \
  WriteStrNL("@SP"); \
  WriteStrNL("M=M-1"); \
  WriteStrNL("A=M"); \

#define pushd \
  WriteStrNL("@SP"); \
  WriteStrNL("A=M"); \
  WriteStrNL("M=D"); \
  WriteStrNL("@SP"); \
  WriteStrNL("M=M+1");

Handle(pop) {
  (void)st;
  (void)t;

  // Store calculated address in D
  char* err = SetAddr(t.arg1, t.arg2, bufout);
  if(err) return err;
  WriteStrNL("D=A");

  // Store D in TEMP
  WriteStrNL("@5");
  WriteStrNL("M=D");

  // Pop D
  popd

  // Get calculated address in A without touching D
  WriteStrNL("@5");
  WriteStrNL("A=M");

  // Finally store D in the calculated address
  WriteStrNL("M=D");

  return NULL;
}

static inline char* arith(char* arith, Buffer* bufout) {
  popd
  popa

  WriteStrNL(arith);

  pushd

  return NULL;
}

Handle(add) { (void)st; (void)t; return arith("D=M+D", bufout);}
Handle(sub) { (void)st; (void)t; return arith("D=M-D", bufout);}
Handle(and) { (void)st; (void)t; return arith("D=M&D", bufout);}
Handle(or)  { (void)st; (void)t; return arith("D=M|D", bufout);}

static inline char* unary(char* arith, Buffer* bufout) {
  popd

  WriteStrNL(arith);

  pushd

  return NULL;
}
Handle(neg) { (void)st; (void)t; return unary("D=-D", bufout);} 
Handle(not) { (void)st; (void)t; return unary("D=!D", bufout);} 

// *CAREFUL* not thread safe
Span nextLabel(int i) {
  static int x = 0;
  static char ar[1000];

  char* label = &ar[i * 50];
  sprintf(label, "LABEL%d", x);
  x += 1;
  return SpanFromString(label);
}

static inline char* comparison(char* dJump, Buffer* bufout) {

  popd
  popa
  WriteStrNL("A=M");

  Span l1 = nextLabel(0);
  Span l2 = nextLabel(1);
  Span l3 = nextLabel(2);

  WriteStrNL("D=D-A");
  WriteA(l1);
  WriteStrNL(dJump);
  WriteA(l2);
  WriteStrNL("0;JMP");

  WriteLabel(l1);
  WriteStrNL("D=-1");
  WriteA(l3);
  WriteStrNL("0;JMP");


  WriteLabel(l2);
  WriteStrNL("D=0");
  WriteA(l3);
  WriteStrNL("0;JMP");

  WriteLabel(l3);
  pushd

  return NULL;
}
Handle(eq) { (void)st; (void)t; return comparison("D;JEQ", bufout); }
Handle(lt) { (void)st; (void)t; return comparison("D;JGT", bufout); }
Handle(gt) { (void)st; (void)t; return comparison("D;JLT", bufout); }

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
#define Write(_k) if(BufferCopy(_k,bufout).error) return SPANERR("Writing buffer too small")

  while(true) {
    SpanPair sp = SpanCut(s, '\n');
    Span line = sp.head;

    if(line.len == 0) break;
    s = sp.tail;

    Write(S("\n// "));
    Write(line);
    Write(S("\n"));

    Token token = parseLine(line);
    char* error = tokenToOps(st, token, bufout);
    if(error) {
      return SPANERR(error); 
    }
  }

  Write(S("(End)"));
  Write(S("\n"));
  Write(S("@End"));
  Write(S("\n"));
  Write(S("0;JMP"));
  Write(S("\n"));

#undef Write
  return (SpanResult){BufferToSpan(bufout), 0};
}

void test(void);

inline static char *basename(char *path)
{
    char *s = strrchr(path, '/');
    if (!s)
        return path;
    else
        return s + 1;
}
int themain(int argc, char** argv) {
  #ifdef TEST
    test();
    return 0;
  #endif

  if(argc != 2) {
    fprintf(stderr, "Usage: %s <asm_file>\n", argv[0]);
    return -1;
  }

  VmFileName = basename(argv[1]);

  #define MAXFILESIZE 1<<20
  static Byte filein [MAXFILESIZE];
  static Byte fileout[MAXFILESIZE];

  Buffer bufin  = BufferInit(filein , MAXFILESIZE);
  Buffer bufout = BufferInit(fileout, MAXFILESIZE);

  // Load asm file
  SpanResult sr = OsSlurp(argv[1], MAXFILESIZE, &bufin);
  if(sr.error) {
    fprintf(stderr, "Error reading file %s.\n%s\n", argv[1], sr.error);
    return -1;
  }

  // Load the symbol table
  SymbolTable* st = STInit();
  st = firstPass(st, sr.data);

  // Produce binary code
  SpanResult sResult = secondPass(st, sr.data, &bufout);
  if(sResult.error) {
    fprintf(stderr, "ERROR: %s\n", sResult.error);
    return -1;
  }
  Span s = sResult.data;

  // Save into output file
  Byte newName[1024];
  Buffer nbuf = BufferInit(newName, 1024);
  Span oldName = SpanFromString(argv[1]);
  Span baseName = SpanCut(oldName, '.').head;

  BufferCopy(baseName, &nbuf);
  BufferCopy(S(".asm"), &nbuf);
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
  (void)sr;
  (void)secondPass;
}

