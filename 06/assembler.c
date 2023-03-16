#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <assert.h> // for assert
#include <string.h> // for strcmp
#include <stdio.h>
#include <ctype.h>

#define SPAN_IMPL
#include "ulib/Span.h"

#define BUFFER_IMPL
#include "ulib/Buffer.h"

#define OS_STDC_IMPL
#include "ulib/OsStdc.h"

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


// *WARNING* The return value is valid until the next call to parseLine.
Token parseLine(Span line) {
  SpanValid(line);

  Span s     = SpanTrim(line);
  size_t len = s.len;

  // Empty line
  if(len == 0) return (Token) {Empty, s};

  // Comment
  if(len > 2 && (s.ptr[0] == '/' && s.ptr[1] == '/')) return (Token) {Comment, SpanSub(s, 2, len)};

  // Label
  if(s.ptr[0] == '(' && s.ptr[len - 1] == ')') {
    return (Token) {Label, SpanSub(s, 1, len - 1)};
  }

  // A Instruction
  if(s.ptr[0] == '@') return (Token) {AInstr, SpanSub(s, 1, len)};

  // C Instruction
  size_t eqpos   = -1;
  size_t semipos = -1;
  Byte*  ptr     = s.ptr;
  Byte   c       = 0;

  // Look for separator characters
  for(int i = 0; i < len; i++) {
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

Span tokenToBinary(Token t) {
  if(t.type == AInstr) {
    Span s = S("0");
    int16_t value = atoi(SpanToStr1K(t.value));
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

void test(void);

int themain(int argc, char** argv) {
  //#define TEST
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

  SpanResult sr = OsSlurp(argv[1], MAXFILESIZE, &bufin);
  if(sr.error) {
    fprintf(stderr, "Error reading file %s.\n%s\n", argv[1], sr.error);
    return -1;
  }

  Span s      = sr.data;
  Size linenr = 1;

  while(true) {
    SpanPair sp = SpanCut(s, '\n');
    if(sp.head.len == 0) break;
    s = sp.tail;

    Token token = parseLine(sp.head);
    Span binary = tokenToBinary(token);
    if(binary.len == 0) continue; // jumps over empty lines and comments
    
    SpanResult sres = BufferCopy(binary, &bufout);
    if(sres.error) {
      fprintf(stderr, "Error: %s\n", sres.error);
      return -1;
    }

    if(!TryBufferPushByte(&bufout, '\n')) {
      fprintf(stderr, "Buffer too small for the given file");
      return -1;
    }

    linenr++;
  }

  if(BufferAvail(&bufout) < 20) {
      fprintf(stderr, "Buffer too small for the given file");
      return -1;
  }

  /*
  // Post program an infinite loop
  BufferCopy(decToBinary(linenr), &bufout);
  BufferPushByte(&bufout, '\n');
  BufferCopy(tokenToBinary(parseLine(S("0;JMP"))), &bufout);
  BufferPushByte(&bufout, '\n');

  Span sout = BufferToSpan(&bufout);
  */

  Byte newName[1024];
  Buffer nbuf = BufferInit(newName, 1024);
  BufferCopy(SpanFromString(argv[1]), &nbuf);
  BufferCopy(S(".hack"), &nbuf);
  BufferPushByte(&nbuf, 0);

  char* writeError = OsFlash((char*)BufferToSpan(&nbuf).ptr, BufferToSpan(&bufout));
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

  #define TTOK(_inst,_bin) { \
    Token tok = parseLine(S(#_inst)); \
    Span  bin = tokenToBinary(tok); \
    assert(SpanEqual(bin,S(#_bin))); \
  }
  // The Add program line by line
  TTOK(@2   ,0000000000000010);
  TTOK(D=A  ,1110110000010000);
  TTOK(@3   ,0000000000000011);
  TTOK(D=D+A,1110000010010000);
  TTOK(@0   ,0000000000000000);
  TTOK(M=D  ,1110001100001000);
}

