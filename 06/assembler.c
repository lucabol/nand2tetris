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

  #define H(_i) { bool __r = STAdd(&st, S("R" #_i),_i); assert(__r); }
  H(0);H(1);H(2);H(3);H(4);H(5);H(6);H(7);H(8);H(9);H(10);H(11);H(12);H(13);H(14);H(15);

  #define K(_n,_s) {bool __r = STAdd(&st, S(#_n),_s); assert(__r); }
  K(SP,0);K(LCL,1);K(ARG,2);K(THIS,3);K(THAT,4);K(SCREEN,16384);K(KBD,24576);

  return &st;
}
/* PARSE */

typedef enum { AInstr, CInstr, Label, Comment, Empty,Error } TokenType;

// Not bothering with a union inside a struct as this is all going to be inlined anyhow with no loss of perf/mem
typedef struct {
  TokenType type;
  Span value;
  Span dest;
  Span comp;
  Span jump;
} Token;

// As '/' is not a valid char in an instruction ...
Span removeLineComment(Span line) {
  return SpanCut(line, '/').head;
}

// *WARNING* The return value is valid until the next call to parseLine.
Token parseLine(Span line) {
  SpanValid(line);

  Span nocom = removeLineComment(line);
  Span s     = SpanTrim(nocom);
  Size len = s.len;

  // Empty line
  if(len == 0) return (Token) {Empty, s, SPAN0, SPAN0, SPAN0};

  // Comment
  if(len > 2 && (s.ptr[0] == '/' && s.ptr[1] == '/')) return (Token) {Comment, SpanSub(s, 2, len), SPAN0, SPAN0, SPAN0};

  // Label
  if(s.ptr[0] == '(' && s.ptr[len - 1] == ')') {
    return (Token) {Label, SpanSub(s, 1, len - 1), SPAN0, SPAN0, SPAN0};
  }

  // A Instruction
  if(s.ptr[0] == '@') return (Token) {AInstr, SpanSub(s, 1, len), SPAN0, SPAN0, SPAN0};

  // C Instruction
  Size eqpos   = -1;
  Size semipos = -1;

  // Look for separator characters
  for(Size i = 0; i < len; i++) {
    Byte c = s.ptr[i];

    if(c == '=') eqpos   = i;
    if(c == ';') semipos = i;
  }

  #define TSpanSub(_s,_st,_en) SpanTrim(SpanSub((_s),(_st),(_en)))

  Span zSpan = S("");
  if(eqpos != -1 && semipos != -1) return (Token) {CInstr, zSpan, TSpanSub(s,0,eqpos), TSpanSub(s,eqpos+1, semipos), TSpanSub(s, semipos+1,len)};
  if(eqpos != -1 && semipos == -1) return (Token) {CInstr, zSpan, TSpanSub(s,0,eqpos), TSpanSub(s,eqpos+1, len)    , zSpan};
  if(eqpos == -1 && semipos != -1) return (Token) {CInstr, zSpan, zSpan              , TSpanSub(s,0,semipos)       , TSpanSub(s,semipos+1,len)};

  return (Token) {CInstr, zSpan, zSpan, s, zSpan};
}

/* CODE */

// This table is known at compile time. Implemented as an X macro expanded to a series of if statements.
#define COMP \
  X(0  ,101010) \
  X(1  ,111111) \
  X(-1 ,111010) \
  X(D  ,001100) \
  X(A  ,110000) \
  X(!D ,001101) \
  X(!A ,110001) \
  X(-D ,001111) \
  X(-A ,110011) \
  X(D+1,011111) \
  X(A+1,110111) \
  X(D-1,001110) \
  X(A-1,110010) \
  X(D+A,000010) \
  X(D-A,010011) \
  X(A-D,000111) \
  X(D&A,000000) \
  X(D|A,010101) \
  X(M  ,110000) \
  X(!M ,110001) \
  X(-M ,110011) \
  X(M+1,110111) \
  X(M-1,110010) \
  X(D+M,000010) \
  X(D-M,010011) \
  X(M-D,000111) \
  X(D&M,000000) \
  X(D|M,010101)

#define DEST \
  X(   ,000) \
  X(M  ,001) \
  X(D  ,010) \
  X(DM ,011) \
  X(MD ,011) \
  X(A  ,100) \
  X(AM ,101) \
  X(MA ,101) \
  X(AD ,110) \
  X(DA ,110) \
  X(ADM,111)

#define JUMP \
  X(   ,000) \
  X(JGT,001) \
  X(JEQ,010) \
  X(JGE,011) \
  X(JLT,100) \
  X(JNE,101) \
  X(JLE,110) \
  X(JMP,111)

#define X(n,b) if(SpanEqual(S(#n),c)) return S(#b); else

Span compToBinary(Span c) { COMP return S(""); }
Span destToBinary(Span c) { DEST return S(""); }
Span jumpToBinary(Span c) { JUMP return S(""); }

// This doesn't allocate but the returned Span is overriddedn at each call
Span decToBinary(int16_t n)
{
  static Byte p[16];

  int c, d, t;

  t = 0;


  for (c = 15 ; c >= 0 ; c--)
  {
    d = n >> c;

    if (d & 1)
      *(p+t) = 1 + '0';
    else
      *(p+t) = 0 + '0';

    t++;
  }

  Span res = SPAN(p,16);
  return  res;
}

char* SpanToStr1K(Span s) {
  static char tmp[1024];

  for(Size i = 0; i < s.len; i++)
    tmp[i] = s.ptr[i];

  tmp[s.len] = 0;
  return tmp;
}

Span tokenToBinary(SymbolTable* st, Token t) {
  if(t.type == AInstr) {
    int16_t value;

    if(isdigit(t.value.ptr[0])) {
      value = atoi(SpanToStr1K(t.value));
    } else {
      value = STGet(st, t.value); 
      if(value < 0) {
        // If it's not in the symbol table, it is a variable
        value = st->varindex;
        bool success = STAdd(st, t.value, value);
        assert(success);

        st->varindex += 1;
      }
    }

    return decToBinary(value);
  }
  if(t.type == CInstr) {
    Span dest = destToBinary(t.dest);
    Span comp = compToBinary(t.comp);
    Span jump = jumpToBinary(t.jump);
    Byte a    = SpanContains(t.comp, 'M') ? '1' : '0';

    static Byte tmp[16];

    memcpy(&tmp[0],"111",3);
    tmp[3] = a;
    memcpy(&tmp[4],comp.ptr,6);
    memcpy(&tmp[10],dest.ptr,3);
    memcpy(&tmp[13],jump.ptr,3);
    return SPAN(tmp, 16);
  }
  // TODO: LABELS
  // Comments and empty lines are eaten
  return S("");
}


SymbolTable* firstPass(SymbolTable* st, Span s) {
  
  int16_t line = 0;

  while(true) {
    SpanPair sp = SpanCut(s, '\n');
    if(sp.head.len == 0) break;
    s = sp.tail;

    Token token = parseLine(sp.head);

    if(token.type == Label) {
        STAdd(st, token.value, line);
    } 

    if(token.type == AInstr || token.type == CInstr)
      line++;
  }

  return st;
}

SpanResult secondPass(SymbolTable* st, Span s, Buffer* bufout) {

  while(true) {
    SpanPair sp = SpanCut(s, '\n');
    if(sp.head.len == 0) break;
    s = sp.tail;

    Token token = parseLine(sp.head);
    Span binary = tokenToBinary(st, token);
    if(binary.len == 0) continue; // jumps over empty lines and comments
    
    SpanResult sres = BufferCopy(binary, bufout);
    if(sres.error) {
      return SPANERR("Buffer too small.");
    }

    if(!TryBufferPushByte(bufout, '\n')) {
      return SPANERR("Buffer too small.");
    }
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
  SpanResult sResult      = secondPass(st, sr.data, &bufout);
  if(sResult.error) {
    fprintf(stderr, "ERROR: %s\n", sResult.error);
    return -1;
  }
  Span s = sResult.data;

  // Save into output file
  Byte newName[1024];
  Buffer nbuf = BufferInit(newName, 1024);
  BufferCopy(SpanFromString(argv[1]), &nbuf);
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

  SpanPuts(compToBinary(S("0")));
  SpanPuts(compToBinary(S("1")));
  SpanPuts(destToBinary(S("M")));
  SpanPuts(destToBinary(S("")));
  SpanPuts(jumpToBinary(S("")));
  SpanPuts(jumpToBinary(S("JGT")));

  #define TAI(__t) { \
    Token r = parseLine(S("@" #__t)); \
    assert(SpanEqual(S(#__t), r.value)); \
    assert(r.type == AInstr); \
  }
  TAI(123);
  TAI(bob);

  #define TAC(__t) { \
    Token r = parseLine(S("//" #__t)); \
    assert(SpanEqual(S(#__t ), r.value)); \
    assert(r.type == Comment); \
  }
  TAC(123);
  TAC(bob);

  #define TAL(__t) { \
    Token r = parseLine(S("(" #__t ")")); \
    assert(SpanEqual(S(#__t ), r.value)); \
    assert(r.type == Label); \
  }
  TAL(123);
  TAL(bob);

  #define TAE(__t) { \
    Token r = parseLine(S(#__t)); \
    assert(SpanEqual(S(""), r.value)); \
    assert(r.type == Empty); \
  }
  TAL();
  TAL(   );

  #define TCI1(_dest, _comp, _jmp) { \
    char tstr[] = #_dest " = " #_comp " ;  " #_jmp; \
    printf("Testing: %s\n", tstr); \
    Token r = parseLine(S(tstr)); \
    assert(SpanEqual(S(#_dest), r.dest)); \
    assert(SpanEqual(S(#_comp), r.comp)); \
    assert(SpanEqual(S(#_jmp), r.jump)); \
    assert(r.type == CInstr); \
  }
  TCI1(M,+1,JNZ);
  TCI1(ADM,D&A,JLE);

  #define TCI4(_dest, _comp) { \
    char tstr[] = #_dest " = " #_comp ; \
    printf("Testing: %s\n", tstr); \
    Token r = parseLine(S(tstr)); \
    assert(SpanEqual(S(#_dest), r.dest)); \
    assert(SpanEqual(S(#_comp), r.comp)); \
    assert(SpanEqual(S(""), r.jump)); \
    assert(r.type == CInstr); \
  }
  TCI4(M,+1);
  TCI4(ADM,D&A);

  #define TCI2(_comp, _jmp) { \
    char tstr[] = #_comp " ;  " #_jmp; \
    printf("Testing: %s\n", tstr); \
    Token r = parseLine(S(tstr)); \
    assert(SpanEqual(S(""), r.dest)); \
    assert(SpanEqual(S(#_comp), r.comp)); \
    assert(SpanEqual(S(#_jmp), r.jump)); \
    assert(r.type == CInstr); \
  }
  TCI2(+1,JNZ);
  TCI2(D&A,JLE);

  #define TCI3(_comp) { \
    char tstr[] = #_comp  ; \
    printf("Testing: %s\n", tstr); \
    Token r = parseLine(S(tstr)); \
    assert(SpanEqual(S(""), r.dest)); \
    assert(SpanEqual(S(#_comp), r.comp)); \
    assert(SpanEqual(S(""), r.jump)); \
    assert(r.type == CInstr); \
  }
  TCI3(+1);
  TCI3(D&A);

  char* str1 = SpanToStr1K(S("Bob"));
  assert(strcmp("Bob", str1) == 0);

  #define TDB(_i,_b) assert(SpanEqual(decToBinary((_i)), S(#_b)));
  TDB(0,0000000000000000);
  TDB(1,0000000000000001);
  TDB(3,0000000000000011);
  TDB(1024,0000010000000000);

  SymbolTable* tst = STInit();

  #define TTOK(_inst,_bin) { \
    Token tok = parseLine(S(#_inst)); \
    Span  bin = tokenToBinary(tst, tok); \
    assert(SpanEqual(bin,S(#_bin))); \
  }
  // The Add program line by line
  TTOK(@2   ,0000000000000010);
  TTOK(D=A  ,1110110000010000);
  TTOK(@3   ,0000000000000011);
  TTOK(D=D+A,1110000010010000);
  TTOK(@0   ,0000000000000000);
  TTOK(M=D  ,1110001100001000);

  SymbolTable st = HashInit;

  assert(STAdd(&st, S("var"), 0));
  assert(STAdd(&st, S("var"), 1));
  assert(STAdd(&st, S("bob"), 2));
  assert(st.len == 2);

  assert(STGet(&st, S("var")) == 1);
  assert(STGet(&st, S("bob")) == 2);
  assert(STGet(&st, S("nonexistent")) == -1);

  // Test string that collide
  assert(STAdd(&st, S("altarage"), 0));
  assert(STAdd(&st, S("zinke"), 1));
  assert(STGet(&st, S("altarage")) == 0);
  assert(STGet(&st, S("zinke")) == 1);
  assert(st.len == 4);

  assert(STAdd(&st, S("declinate"), 0));
  assert(STAdd(&st, S("macallums"), 1));
  assert(STGet(&st, S("declinate")) == 0);
  assert(STGet(&st, S("macallums")) == 1);
  assert(st.len == 6);

  SymbolTable* stab = STInit();
  assert(stab->len == 23);
}

