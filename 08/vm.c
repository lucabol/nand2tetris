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
  X(or) \
  X(label) \
  X(function) \
  X(call)

typedef enum {
#define X(_n) _n,
  INSTR
#undef X
  gotoe,
  gotoeif,
  returne,
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
  // Unfortunately, goto is a reserved word and identifiers cannot have '-' in them.
  // So I cannot use the X macro trick for these two keywords.
  if(SpanEqual(s, S("goto"))) return gotoe;
  if(SpanEqual(s, S("if-goto"))) return gotoeif;
  if(SpanEqual(s, S("return"))) return returne;

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

#define Handle(_n) char* _n##f(Token t, Buffer* bufout)

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
// Hack instead of passing these parameters to many functions
// TODO: make it threadlocal if multi-thread compilation.
static char* VmFileName = NULL; 
static Span FuncName = {0};

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

Handle(add) { (void)t; return arith("D=M+D", bufout);}
Handle(sub) { (void)t; return arith("D=M-D", bufout);}
Handle(and) { (void)t; return arith("D=M&D", bufout);}
Handle(or)  { (void)t; return arith("D=M|D", bufout);}

static inline char* unary(char* arith, Buffer* bufout) {
  popd

  WriteStrNL(arith);

  pushd

  return NULL;
}
Handle(neg) { (void)t; return unary("D=-D", bufout);} 
Handle(not) { (void)t; return unary("D=!D", bufout);} 

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
Handle(eq) { (void)t; return comparison("D;JEQ", bufout); }
Handle(lt) { (void)t; return comparison("D;JGT", bufout); }
Handle(gt) { (void)t; return comparison("D;JLT", bufout); }

Handle(label) {
  WriteLabel(t.arg1);
  return NULL;
}

Handle(goto) {
  WriteA(t.arg1);
  WriteStr("0;JMP\n");
  return NULL;
}

Handle(gotoif) {
  popd
  WriteA(t.arg1);
  WriteStr("D;JNE\n");
  return NULL;
}

char* GenFLabel(Buffer* bufout, bool indexed) {
  static unsigned i = 0;

  WriteSpan(SpanFromString(VmFileName));
  WriteStr(".");
  WriteSpan(FuncName);

  if(indexed) {
    WriteStr(".");
    WriteSpan(SpanFromUlong(i));
    i += 1;
  }

  return NULL;
}

#define CheckF(...) { char* _err = __VA_ARGS__; if(_err) return _err; }
#define PUSH(_reg) { WriteA(S(#_reg)); WriteStrNL("D=M"); pushd;}
#define PUSHSPAN(_reg) { WriteA(_reg); WriteStrNL("D=M"); pushd;}

Handle(function) {
  FuncName = t.arg1;

  WriteStr("(");
  CheckF(GenFLabel(bufout, false));
  WriteStrNL(")");

  Size args = SpanToUlong(t.arg2);
  assert(args >= 0);
  
  for(int i = 0; i < args; i++) {
    PUSH(0);
  }

  return NULL;
}
Handle(call) {
  // TODO: push ret address ...
  Byte buf[1024];
  Buffer b = BufferInit(buf, sizeof(buf));
  CheckF(GenFLabel(&b,true));
  Span retLabel = BufferToSpan(&b);

  // push retAddress
  PUSHSPAN(retLabel);

  // push caller state
  PUSH(LCL);PUSH(ARG);PUSH(THIS);PUSH(THAT);

  // ARG = SP - 5 - nArgs
  WriteA(S("SP"));
  WriteStrNL("D=M");
  WriteA(S("5"));
  WriteStrNL("D=D-A");
  WriteA(t.arg2);
  WriteStrNL("D=D-A");
  WriteA(S("ARG"));
  WriteStrNL("M=D");

  // LCL = SP
  WriteA(S("SP"));
  WriteStrNL("D=M");
  WriteA(S("LCL"));
  WriteStrNL("M=D");
  
  // goto f
  WriteA(t.arg1);
  WriteStr("0;JMP");

  // Write retAddress label
  WriteLabel(retLabel);
  return NULL;
}

Handle(returne) {
  (void)t;

  // frame = LCL
  WriteA(S("LCL"));
  WriteStrNL("D=M");
  WriteA(S("frame"));
  WriteStr("M=D");

#define TOA(_addr, _minus) \
  WriteA(S("frame")); \
  WriteStr("M=D"); \
  WriteA(S(#_minus)); \
  WriteStrNL("D=D-A"); \
  WriteA(S(#_addr)); \
  WriteStrNL("M=D")

  // *ARG = pop()
  popd
  WriteA(S("ARG"));
  WriteStrNL("M=D");

  // SP = ARG+1
  WriteStrNL("D=D+1");
  WriteA(S("SP"));
  WriteStrNL("M=D");

  TOA(retAddr, 5); // retAddr = *(frame-5)
  TOA(THAT, 1);
  TOA(THIS, 2);
  TOA(ARG, 3);
  TOA(LCL, 4);

  WriteA(S("retAddr"));
  WriteStrNL("0;JMP");

  return NULL;
}
#undef Write
#undef Handle

char* tokenToOps(Token t, Buffer* bufout) {
  switch(t.type) {
#define X(_n) case _n: return _n##f(t, bufout);
    INSTR
#undef X
    case gotoe: return gotof(t, bufout);
    case gotoeif: return gotoiff(t, bufout);
    case returne: return returnef(t, bufout);
    case Empty: return NULL;
    case Error:
      return "An error occurred.";
  }
  return "Shouldn't get here as every branch returns.";
}

SpanResult compile(Span s, Buffer* bufout) {
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
    char* error = tokenToOps(token, bufout);
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

  // Produce binary code
  SpanResult sResult = compile(sr.data, &bufout);
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
  SpanResult sr = compile(S("push const 3"), &buf);
  (void)sr;
}

