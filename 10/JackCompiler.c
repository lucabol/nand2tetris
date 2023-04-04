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

/** TOKENIZER **/

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
#define WriteXmlSpan(_tag,_value) WriteStr("<"); WriteStr(_tag); WriteStr(">"); \
  WriteSpan(_value); WriteStr("</"); WriteStr(_tag); WriteStrNL(">")

char* EmitTokenizerXml(Span rest, Buffer* bufout) {

    WriteStrNL("<tokens>");

    while(true) {
      TokenResult sResult = nextToken(rest);
      if(sResult.error) return sResult.error;

      if(sResult.token.type == Error) return "Error token type shouldn't be generated";

      if(sResult.token.type == Eof) break;

      char* type  = tokenNames[sResult.token.type];
      Span value = xmlNormalize(sResult.token.value);

      WriteXmlSpan(type, value);

      rest = sResult.rest;
    }

    WriteStrNL("</tokens>");
    return NULL;
}

/** END TOKENIZER **/

/** PARSER **/

// Make threadlocal if multithreaded
static Token tok;
static Span rest;
static char* filePath;

#define SM do {
#define EM } while(0)

#define NextToken SM \
  TokenResult tr = nextToken(rest); \
  if(tr.error) return tr.error; \
  tok = tr.token; rest = tr.rest; EM

#define ConsumeTokenIf(__tt, __value) SM \
  if(IsToken(__tt, __value)) WriteToken; else tokenerr; \
  NextToken; EM
#define ConsumeToken SM WriteToken; NextToken; EM

#define ConsumeType SM if(IsType) ConsumeToken; else tokenerr; EM
#define ConsumeIdentifier SM ConsumeTokenIf(identifier, ""); EM
#define ConsumeKeyword(_kw) SM ConsumeTokenIf(keyword, _kw); EM
#define ConsumeSymbol(_sy) SM ConsumeTokenIf(symbol, _sy); EM

#define WriteToken SM WriteXmlSpan(tokenNames[tok.type], xmlNormalize(tok.value)); EM

#undef IsKeyword
#define IsToken(_tt, _v) isToken(tok, (_tt), (_v))
#define IsSymbol(_v) isToken(tok, symbol, (_v))
#define IsKeyword(_v) isToken(tok, keyword, (_v))
#define IsType  IsToken(keyword, "int") || IsToken(keyword, "char") || IsToken(keyword, "boolean") || IsToken(identifier,"")

#define DECLARE(_rule) char* compile ## _rule(Buffer* bufout) 

#define STARTRULE(_rule) DECLARE(_rule) { char* __funcName = #_rule; WriteStrNL("<" #_rule ">");
#define ENDRULE SM WriteStr("</"); WriteStr(__funcName); WriteStrNL(">"); return NULL; EM; }

#define Invoke(_rule) SM char* error = compile ## _rule(bufout); if(error) return error; EM 

char* cerror(char* startMessage, Span s1, Span s2) {
  static Byte buf[1024];
  Buffer bufo = BufferInit(buf, sizeof(buf));
  Buffer* bufout = &bufo;
  WriteStr(filePath); WriteStr(" : ");
  WriteStr(startMessage); WriteStr(" : "); WriteSpan(s1); WriteStr(" ");WriteSpan(s2); WriteStrNL("");
  bufo.data.ptr[bufo.index] = 0;
  return (char*)buf;
}

#define tokenerr SM return cerror(__funcName, SpanFromString(tokenNames[tok.type]), tok.value); EM

char* cerrorS(char* startMessage, char* s1, char* s2) {
  return cerror(startMessage, SpanFromString(s1), SpanFromString(s2));
}

bool isToken(Token tok, TokenType tt, char* value) {

  if(tok.type != tt)
    return false;

  Span svalue = SpanFromString(value);
  if(svalue.len != 0 && !SpanEqual(svalue, tok.value))
    return false;

  return true;
}

STARTRULE(classVarDec)
  ConsumeToken;
  ConsumeType;
  ConsumeIdentifier;

  while(!IsSymbol(";")) {
      ConsumeSymbol(",");
      ConsumeIdentifier;
  }
  ConsumeToken;
ENDRULE

STARTRULE(parameterList)
  while(!IsSymbol(")")) {

    ConsumeType;
    ConsumeIdentifier;

    if(IsSymbol(",")) {
      ConsumeToken;
    }
  }
ENDRULE

STARTRULE(varDec)
  ConsumeKeyword("var");
  ConsumeType;
  ConsumeIdentifier;

  while(!IsSymbol(";")) {
    ConsumeSymbol(",");
    ConsumeIdentifier;
  }
  ConsumeToken;
ENDRULE

DECLARE(expression);
DECLARE(expressionList);

STARTRULE(term)
  if(IsToken(integerConstant,"") || IsToken(stringConstant, "")) {
    ConsumeToken;
  } else if(IsKeyword("true") || IsKeyword("false") || IsKeyword("null") || IsKeyword("this")) {
    ConsumeToken;
  } else if(IsToken(identifier,"")) { // can be a varName, array, subroutine call or method call (with '.')
    ConsumeToken;
    if(IsSymbol("[")) {
      ConsumeToken;
      Invoke(expression);
      ConsumeSymbol("]");
    } else if(IsSymbol("(")) {
      ConsumeToken;
      Invoke(expressionList);
      ConsumeSymbol(")");
    } else if(IsSymbol(".")) {
      ConsumeToken;
      ConsumeIdentifier;
      ConsumeSymbol("(");
      Invoke(expressionList);
      ConsumeSymbol(")");
    }
  } else if(IsSymbol("(")) {
    ConsumeToken;
    Invoke(expression);
    ConsumeSymbol(")");
  } else if(IsSymbol("-") || IsSymbol("~")){
    ConsumeToken;
    Invoke(term);
  } else tokenerr;
ENDRULE

STARTRULE(expression)
  Invoke(term);
  while(IsSymbol("+") || IsSymbol("-") || IsSymbol("*") || IsSymbol("/") || IsSymbol("&") ||
                 IsSymbol("|") || IsSymbol("<") || IsSymbol(">") || IsSymbol("=")) {
      ConsumeToken;
      Invoke(term);
    }
ENDRULE

STARTRULE(expressionList)
  while(!IsSymbol(")")) {
    Invoke(expression);
    if(IsSymbol(","))
      ConsumeToken;
  }
ENDRULE

STARTRULE(letStatement)
  ConsumeKeyword("let");
  ConsumeIdentifier;

  if(IsSymbol("[")) {
    ConsumeToken;
    Invoke(expression);
    ConsumeSymbol("]");
  }

  ConsumeSymbol("=");
  Invoke(expression);
  ConsumeSymbol(";");
ENDRULE

DECLARE(statements);

STARTRULE(ifStatement)
  ConsumeKeyword("if");
  ConsumeSymbol("(");
  Invoke(expression);
  ConsumeSymbol(")");

  ConsumeSymbol("{");
  Invoke(statements);
  ConsumeSymbol("}");

  if(IsKeyword("else")) {
     ConsumeToken;
     ConsumeSymbol("{");
     Invoke(statements);
     ConsumeSymbol("}");
  }
ENDRULE

STARTRULE(whileStatement)
  ConsumeKeyword("while");

  ConsumeSymbol("(");
  Invoke(expression);
  ConsumeSymbol(")");

  ConsumeSymbol("{");
  Invoke(statements);
  ConsumeSymbol("}");
ENDRULE

STARTRULE(doStatement)
  ConsumeKeyword("do");
  ConsumeIdentifier;
  if(IsSymbol("[")) {
    ConsumeToken;
    Invoke(expression);
    ConsumeSymbol("]");
  } else if(IsSymbol("(")) {
    ConsumeToken;
    Invoke(expressionList);
    ConsumeSymbol(")");
  } else if(IsSymbol(".")) {
    ConsumeToken;
    ConsumeIdentifier;
    ConsumeSymbol("(");
    Invoke(expressionList);
    ConsumeSymbol(")");
  }
  ConsumeSymbol(";");
ENDRULE

STARTRULE(returnStatement)
  ConsumeKeyword("return");
  if(!IsSymbol(";"))
    Invoke(expression);
  ConsumeSymbol(";");
ENDRULE

STARTRULE(statements)
  while(true) {
    if(IsKeyword("let")) Invoke(letStatement);
    else if(IsKeyword("if")) Invoke(ifStatement);
    else if(IsKeyword("while")) Invoke(whileStatement);
    else if(IsKeyword("do")) Invoke(doStatement);
    else if(IsKeyword("return")) Invoke(returnStatement);
    else break;
  } 
ENDRULE

STARTRULE(subroutineBody)
  ConsumeSymbol("{");
  while(IsKeyword("var"))
      Invoke(varDec);
  Invoke(statements);
  ConsumeSymbol("}");
ENDRULE

STARTRULE(subroutineDec)
  ConsumeToken;
  
  if(IsType || IsToken(keyword, "void")) {
    ConsumeToken;
  } else
      tokenerr;

  ConsumeIdentifier;
  ConsumeSymbol("(");
  Invoke(parameterList);
  ConsumeSymbol(")");
  Invoke(subroutineBody);
ENDRULE


STARTRULE(class)
  NextToken; // Just one class x file, trivial to extend to multiple ones

  ConsumeKeyword("class");
  ConsumeIdentifier;
  ConsumeSymbol("{");

  while(!IsSymbol("}")) {

    if(IsKeyword("static") || IsKeyword("field"))
      Invoke(classVarDec);
    else if(IsKeyword("constructor") || IsKeyword("function") || IsKeyword("method"))
      Invoke(subroutineDec);
    else
      tokenerr;
  }

  ConsumeToken;
ENDRULE

/** END PARSER **/

/** MAIN LOOP **/
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

    filePath = argv[i];

    // Load input file
    SpanResult sr = OsSlurp(filePath, MAXFILESIZE, &bufin);
    if(sr.error) {
      fprintf(stderr, "Error reading file %s.\n%s\n", filePath, sr.error);
      return -1;
    }

    #ifdef TOKENIZER
    char* error = EmitTokenizerXml(sr.data, &bufout);
    #else
    rest = sr.data;
    char* error = compileclass(&bufout);
    #endif

    if(error) {
      fprintf(stderr, "Error: %s\n", error);
      //return -1;
    }

    // Produce output file
    Span s = BufferToSpan(&bufout);

    Byte newName[1024];
    Buffer nbuf = BufferInit(newName, sizeof(newName));
    Span oldName = SpanFromString(filePath);
    Span withoutExt = SpanRCut(oldName, '.').head; // just for linux

    BufferCopy(withoutExt, &nbuf);

    #ifdef TOKENIZER
    BufferCopy(S("T.vm"), &nbuf);
    #else
    BufferCopy(S(".vm"), &nbuf);
    #endif

    BufferPushByte(&nbuf, 0); // make it a proper 0 terminated string

    char* writeError = OsFlash((char*)BufferToSpan(&nbuf).ptr, s);
    if(writeError) {
      fprintf(stderr, "%s\n", writeError);
      return -1;
    }
  }

  return 0;
}
