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
  X(Error) \
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


#define ETOK ((char)-1)

#define FIRSTSTATE                                \
  Span rest = data;                                \
  char ch = ETOK;                                \
  Byte* startPtr = data.ptr;                       \
  Byte* endPtr   = 0;                             \
  Byte* curPtr   = 0; \
  Byte* nextPtr  = 0; \
  STATE(FirstState)


#define ADVANCE                                     \
  if(rest.len == 0) {                               \
    ch = ETOK;                                     \
    curPtr = rest.ptr; \
    rest.ptr++;                                     \
    nextPtr = rest.ptr; \
  } else {                                          \
    ch = (char)rest.ptr[0];                         \
    curPtr = rest.ptr; \
    rest = SpanTail(rest, rest.len - 1);                \
    nextPtr = rest.ptr; \
  }                                                 \
  switch(ch)

#define STATE(name)                               \
  name:                                           \
  ADVANCE

#define RETTOKEN(tokenType, ptr, len) return (TokenResult) { (Token) {tokenType, SPAN(ptr, len)}, rest, NULL }

#define CASENUMBER case '0':case '1':case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9'
#define CASESYMBOL case '{':case '}':case '(':case ')':case '[':case ']':case '.':case ',':case ';': \
  case '+':case '-':case '*':case '&':case '|':case '<':case '>':case '=': case '~'
#define CASESPACES case ' ': case '\t': case '\n': case '\r'

#define KEYWORDS Y(class) Y(constructor) Y(function) Y(method) Y(field) Y(static) Y(var) \
  Y(int) Y(char) Y(boolean) Y(void) Y(true) Y(false) Y(null) Y(this) Y(let) \
  Y(do) Y(if) Y(else) Y(while) Y(return)

#define Y(k) || SpanEqual(S(#k),s)
#define IsKeyword (0 KEYWORDS) 

TokenResult nextToken(Span data) {
  FIRSTSTATE {
    case ETOK:
      RETTOKEN(Eof, NULL, 0);
    case '"':
      startPtr = nextPtr;
      goto stringConstant;
    CASENUMBER: 
      startPtr = curPtr;
      goto integerConstant;
    case '/': // special case for start of comment symbol
      startPtr = curPtr;
      goto maybeComment;
    CASESYMBOL:
      startPtr = curPtr;
      RETTOKEN(symbol, startPtr, 1);
    CASESPACES:
      goto FirstState;
    default:
      startPtr = curPtr;
      goto identifierOrKeyword;
  }
  STATE(maybeComment) {
    case '/':
      goto lineComment;
    case '*':
      goto starComment;
    default:
      RETTOKEN(symbol, startPtr, 1);
  }
  STATE(lineComment) {
    case ETOK:
      RETTOKEN(Eof, NULL, 0);
    case '\n':
      goto FirstState;
    default:
      goto lineComment;
  }
  STATE(starComment) {
    case ETOK:
      RETTOKEN(Eof, NULL, 0);
    case '*':
      if(rest.len > 0 && *nextPtr == '/') {
        rest.ptr++;rest.len--;
        goto FirstState;
      }
    default:
      goto starComment;
  }
  STATE(identifierOrKeyword) {
    case ETOK: CASESPACES: CASESYMBOL:
      endPtr = curPtr - 1;
      Span s = SPAN(startPtr, endPtr - startPtr + 1);

      // Eat back one char because otherwise we might miss a symbol
      rest.ptr--;rest.len++;
      // TODO: IsKeyword should be parametize by s. Goofy now.
      if(IsKeyword) RETTOKEN(keyword, s.ptr, s.len);
      else RETTOKEN(identifier, s.ptr, s.len);
    default:
      goto identifierOrKeyword;
  }
  STATE(stringConstant) {
    case '"':
      endPtr = curPtr - 1;
      RETTOKEN(stringConstant, startPtr, endPtr - startPtr + 1);
    case '\n': case ETOK:
      endPtr = curPtr - 1;
      RETTOKEN(Error, startPtr, endPtr - startPtr + 1);
    default:
      goto stringConstant;
  }
  STATE(integerConstant) {
    CASENUMBER:
      goto integerConstant;
    default:
      endPtr = curPtr;
      // Eat back one char because otherwise we might miss a symbol
      rest.ptr--;rest.len++;
      RETTOKEN(integerConstant, startPtr, endPtr - startPtr);
  }
}

Span xmlNormalize(Span s) {
  switch(s.ptr[0]) {
    case '<': return S("&lt;");
    case '>': return S("&gt;");
    case '"': return S("&quot;");
    case '&': return S("&amp;");
    default: return s;
  }
}

#define WriteSpan(_k) if(BufferCopy(_k,bufout).error) return "Writing buffer too small"
#define WriteStr(_s) WriteSpan(SpanFromString(_s))
#define WriteStrNL(_s) WriteStr((_s));WriteStr("\n")
#define WriteXml(_tag,_value) WriteStr("<"); WriteStr(_tag); WriteStr(">"); \
  WriteStr(_value); WriteStr("</"); WriteStr(_tag); WriteStrNL(">")

char* EmitTokenizerXml(Span rest, Buffer* bufout) {

    WriteStrNL("<tokens>");

    while(true) {
      TokenResult sResult = nextToken(rest);
      if(sResult.error) return sResult.error;

      if(sResult.token.type == Error) return "Error token type shouldn't be generated";

      if(sResult.token.type == Eof) break;

      char* type  = tokenNames[sResult.token.type];
      char* value = (char*)SpanTo1KTempString(xmlNormalize(sResult.token.value));

      WriteXml(type, value);

      rest = sResult.rest;
    }

    WriteStrNL("</tokens>");
    return NULL;
}

int themain(int argc, char** argv) {
  if(argc == 1) {
    fprintf(stderr, "Usage: %s <jack_files>\n", argv[0]);
    return -1;
  }

  // Processes all files
  for(int i = 1; i < argc; i++) {

    // Initialize input and output buffers
    #define MAXFILESIZE 1<<20

    static Byte fileout[MAXFILESIZE];
    Buffer bufout = BufferInit(fileout, MAXFILESIZE);

    static Byte filein [MAXFILESIZE];
    Buffer bufin  = BufferInit(filein , MAXFILESIZE);

    char* filePath = argv[i];

    // Load input file
    SpanResult sr = OsSlurp(filePath, MAXFILESIZE, &bufin);
    if(sr.error) {
      fprintf(stderr, "Error reading file %s.\n%s\n", filePath, sr.error);
      return -1;
    }

    #ifdef TOKENIZER
    char* error = EmitTokenizerXml(sr.data, &bufout);
    if(error) {
      fprintf(stderr, "Error: %s\n", error);
      return -1;
    }
    #else
    #error Implement parser
    #endif

    // Produce output file
    Span s = BufferToSpan(&bufout);

    Byte newName[1024];
    Buffer nbuf = BufferInit(newName, sizeof(newName));
    Span oldName = SpanFromString(filePath);
    Span withoutExt = SpanRCut(oldName, '.').head; // just for linux

    BufferCopy(withoutExt, &nbuf);
    BufferCopy(S("T.vm"), &nbuf);
    BufferPushByte(&nbuf, 0); // make it a proper 0 terminated string

    char* writeError = OsFlash((char*)BufferToSpan(&nbuf).ptr, s);
    if(writeError) {
      fprintf(stderr, "%s\n", writeError);
      return -1;
    }
  }

  return 0;
}
