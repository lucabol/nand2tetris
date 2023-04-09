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

/** SYMBOL TABLE **/
#define EXP 12
#define LOADFACTOR 60
#define MAXLEN (1 << EXP) * LOADFACTOR / 100

typedef struct {
  Span symbol;
  Span type;
  Span kind;
  int16_t num;
} Entry;

typedef struct {
  Entry entries[1<<EXP];
  int32_t len;

  // Indexes for kinds of variables
  int16_t staticCount;
  int16_t fieldCount;

  int16_t varCount;
  int16_t argCount;
} SymbolTable;

char* STAdd(SymbolTable* st, Span symbol, Span type, Span kind, Size num) {
  assert(st);
  assert(SpanValid(symbol));

  if(MAXLEN < st->len) return "Symbol table too small to contain all symbols in the program."; // OOM

  uint64_t h = HashString(symbol.ptr, symbol.len);
  for(int32_t i = h;;) {
    i = HashLookup(h, EXP, i);
    Entry* e = &st->entries[i];
    // Insert it either if it already exist (overwrite) or if the cell is empty (0)
    if(SpanEqual(symbol, e->symbol) || e->symbol.ptr == 0) {
      st->len += e->symbol.ptr == 0;
      // Last wins, likely wrong. More likely should err on duplicated symbol.
      *e = (Entry) { symbol, type, kind, num };
      return NULL;
    }
  }
}
Entry* STGet(SymbolTable* st, Span symbol) {
  assert(st);
  assert(SpanValid(symbol));

  uint64_t h = HashString(symbol.ptr, symbol.len);
  for(int32_t i = h;;) {
    i = HashLookup(h, EXP, i);
    Entry* e = &st->entries[i];

    if(e->symbol.ptr == 0) return NULL;

    if(SpanEqual(symbol, e->symbol)) {
      return e;
    }
  }
}

void STInit(SymbolTable* st) {
  *st = (SymbolTable) {0};
}

int16_t STCount(SymbolTable* st, Span kind, bool increase) {
  #define KK(_k) if(SpanEqual(kind, S(#_k))) return increase ? st-> _k ## Count++ : st-> _k ## Count
  KK(static);KK(field);KK(var);KK(arg);
  return 0;
}

#define Save(_s) Span _s = tok.value
#define SaveSymb Save(symb)
#define SaveType Save(type)
#define SaveKind Save(kind)
#define SaveArg Span kind = S("arg")
#define SaveVar Span kind = S("var")
#define StAdd(_st) SM; char* err = STAdd(&_st, symb, type, kind, STCount(&_st, kind, true)); if(err) return err; EM

static SymbolTable StClass;
static SymbolTable StSubroutine;

Entry* STLookup(Span symbol) {
  Entry* ret   = STGet(&StClass, symbol);
  if(!ret) ret = STGet(&StSubroutine, symbol); 
  return ret;
}

#define CheckStLookup(_n, _v) \
  Entry* _n = STLookup(_v); \
  if(!_n) return cerror("Left side of assignment not in the symbol table", S(#_n), _v);

/** END SYMBOL TABLE **/

/** EMITTER **/

Span kindToSeg(Span k) {
  #define KTS(kind,seg) if(SpanEqual(S(#kind), k)) return S(#seg)
  KTS(static,static);
  KTS(field,this);
  KTS(arg,argument);
  KTS(var,local);
  return k;
}
#define Push(kind, idx) SM WriteStr("push "); WriteSpan(kindToSeg(kind)); WriteStr(" "); \
  WriteSpan(SpanFromUlong(idx)); WriteStrNL(""); EM
#define Pop(kind, idx) SM WriteStr("pop "); WriteSpan(kindToSeg(kind)); WriteStr(" "); \
  WriteSpan(SpanFromUlong(idx)); WriteStrNL(""); EM

#define PushEntry(e) Push(e->kind, e->num)
#define PopEntry(e)  Pop(e->kind, e->num)

#define Arith(s)      SM WriteStrNL(#s); EM
#define Label(n)      SM WriteStr("label "); WriteStr("L"); WriteSpan(SpanFromUlong(n)); WriteStrNL(""); EM
#define Goto(n)       SM WriteStr("goto "); WriteStr("L"); WriteSpan(SpanFromUlong(n)); WriteStrNL("");EM
#define IfGoto(n)     SM WriteStr("if-goto "); WriteStr("L"); WriteSpan(SpanFromUlong(n)); WriteStrNL("");EM

#define Call(s,n)     SM WriteStr("call "); WriteStr(s); WriteStr(" "); WriteSpan(SpanFromUlong(n)); WriteStrNL(""); EM
#define CallC(c, m,n) SM WriteStr("call "); WriteSpan(c); WriteStr("."); WriteSpan(m); \
  WriteStr(" "); WriteSpan(SpanFromUlong(n)); WriteStrNL(""); EM

#define FunctionName(s) SM WriteStr("function "); WriteSpan(baseName); \
  WriteStr("."); WriteSpan(s); WriteStr(" "); EM
#define FunctionParams(n) SM WriteSpan(SpanFromUlong(n)); WriteStrNL(""); EM

#define Return        SM WriteStrNL("return"); EM

/** END EMITTER **/

/** PARSER **/

// Make threadlocal if multithreaded
static Token tok;
static Span rest;
static Span baseName;


#define NextToken SM \
  TokenResult tr = nextToken(rest); \
  if(tr.error) return tr.error; \
  tok = tr.token; rest = tr.rest; EM

#define ConsumeTokenIf(__tt, __value) SM \
  if(IsToken(__tt, __value)) NextToken; else tokenerr; EM
#define ConsumeToken SM NextToken; EM

#define ConsumeType SM if(IsType) ConsumeToken; else tokenerr; EM
#define ConsumeIdentifier SM ConsumeTokenIf(identifier, ""); EM
#define ConsumeKeyword(_kw) SM ConsumeTokenIf(keyword, _kw); EM
#define ConsumeSymbol(_sy) SM ConsumeTokenIf(symbol, _sy); EM

#undef IsKeyword
#define IsToken(_tt, _v) isToken(tok, (_tt), (_v))
#define IsSymbol(_v) isToken(tok, symbol, (_v))
#define IsKeyword(_v) isToken(tok, keyword, (_v))
#define IsType  IsToken(keyword, "int") || IsToken(keyword, "char") || IsToken(keyword, "boolean") || IsToken(identifier,"")

#define DECLARE(_rule) char* compile ## _rule(Buffer* bufout) 

#define STARTRULE(_rule) DECLARE(_rule) { char* __funcName = #_rule; (void)bufout; (void) __funcName;
#define ENDRULE SM return NULL; EM; }

#define Invoke(_rule) SM char* error = compile ## _rule(bufout); if(error) return error; EM 

// TODO: need to refactor. I decided not to pass any state between rules. Later I discover that some state
// is needed. Instead of changing all function/macros signatures to pass the state around, I hacked it with static.
// It works because the state is never used in recursive rules. An AST wouldn't require data as the state
// is stored in the tree.
static int __sArgs = -1;
static int labelCount = 0;
static Span LastFuncType = {0};
static Span LastFuncName = {0};
static Span LastClassName = {0};
static Span LastFuncReturn = {0};

#define InvokeExpressionList \
  Invoke(expressionList); \
  int nArgs = __sArgs; (void)nArgs


char* cerror(char* startMessage, Span s1, Span s2) {
  static Byte buf[1024];
  Buffer bufo = BufferInit(buf, sizeof(buf));
  Buffer* bufout = &bufo;
  WriteSpan(baseName); WriteStr(" : ");
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
  SaveKind; ConsumeToken;
  SaveType; ConsumeType;
  SaveSymb; ConsumeIdentifier;

  StAdd(StClass);

  while(!IsSymbol(";")) {
      ConsumeSymbol(",");

      SaveSymb; ConsumeIdentifier;
      StAdd(StClass);
  }
  ConsumeToken;
ENDRULE

STARTRULE(parameterList)
  while(!IsSymbol(")")) {

    SaveArg;
    SaveType; ConsumeType;
    SaveSymb; ConsumeIdentifier;
  
    StAdd(StSubroutine);

    if(IsSymbol(",")) {
      ConsumeToken;
    }
  }
ENDRULE

STARTRULE(varDec)
  SaveVar; ConsumeKeyword("var");
  SaveType; ConsumeType;
  SaveSymb; ConsumeIdentifier;

  StAdd(StSubroutine);

  while(!IsSymbol(";")) {
    ConsumeSymbol(",");
    SaveSymb; ConsumeIdentifier;
    StAdd(StSubroutine);
  }
  ConsumeToken;
ENDRULE

DECLARE(expression);
DECLARE(expressionList);

Span ConstToStr(Span c) {
  #define CTS(_c, _s) if(SpanEqual(S(_c),c)) return S(_s)
  CTS("null", "push constant 0");
  CTS("false", "push constant 0");
  CTS("true", "push constant 1\nneg");
  CTS("this", "push pointer 0");
  return S("ERROR");
}

STARTRULE(term)
  if(IsToken(integerConstant,"")) {
    Push(S("constant"),SpanToUlong(tok.value));
    ConsumeToken;
  } else if(IsToken(stringConstant,"")) {
    Push(S("constant"), tok.value.len);
    Call("String.new", 1);
    for(Size i = 0; i < tok.value.len; i++) {
      Push(S("constant"), tok.value.ptr[i]);
      Call("String.appendChar", 1);
    }
    ConsumeToken;
  } else if(IsKeyword("true") || IsKeyword("false") || IsKeyword("null") || IsKeyword("this")) {
    WriteSpan(ConstToStr(tok.value)); WriteStrNL("");
    ConsumeToken;
  } else if(IsToken(identifier,"")) { // can be a varName, array, subroutine call or method call (with '.')
    Span startId = tok.value;
    Entry* stFound = STLookup(startId);
    ConsumeIdentifier;

    if(IsSymbol("[")) { // Array
      ConsumeToken;
      Invoke(expression);
      ConsumeSymbol("]");
    } else if(IsSymbol("(")) { // Method call on this
      ConsumeToken;

      Push(S("pointer"),0);
      // Entry* thisp = STLookup(S("this"));
      // if(!thisp) return "No 'this' pointer before method invocation.";
      // PushEntry(thisp);

      InvokeExpressionList;
      ConsumeSymbol(")");
      CallC(LastClassName,startId,nArgs + 1);
    } else if(IsSymbol(".")) { // func or method
      ConsumeToken;
      Span funcOrMethod = tok.value;
      ConsumeIdentifier;
      ConsumeSymbol("(");
      InvokeExpressionList;
      ConsumeSymbol(")");

      if(stFound) { // method call, push the object first
        PushEntry(stFound);
        CallC(stFound->type, funcOrMethod, nArgs + 1);
      } else {
        CallC(startId, funcOrMethod, nArgs);
      }
    } else { // Var
      if(!stFound) cerror("Variable not found", startId, SPAN0);
      PushEntry(stFound);
    }
  } else if(IsSymbol("(")) {
    ConsumeToken;
    Invoke(expression);
    ConsumeSymbol(")");
  } else if(IsSymbol("-") || IsSymbol("~")){
    Span op = tok.value;
    ConsumeToken;
    Invoke(term);
    if(SpanEqual(op, S("-")))
      Arith(neg);
    else
      Arith(not);
  } else tokenerr;
ENDRULE

Span OpToStr(Span c) {
  #define OTS(_c, _s) if(SpanEqual(S(_c),c)) return S(_s)
  OTS("+", "add");
  OTS("-", "sub");
  OTS("*", "call Math.multiply 2");
  OTS("/", "call Math.divide 2");
  OTS("&", "and");
  OTS("|", "or");
  OTS("<", "lt");
  OTS(">", "gt");
  OTS("=", "eq");
  return S("ERROR");
}
STARTRULE(expression)
  Invoke(term);
  while(IsSymbol("+") || IsSymbol("-") || IsSymbol("*") || IsSymbol("/") || IsSymbol("&") ||
                 IsSymbol("|") || IsSymbol("<") || IsSymbol(">") || IsSymbol("=")) {
      Span op = OpToStr(tok.value);
      ConsumeToken;

      Invoke(term);

      WriteSpan(op); WriteStrNL("");
    }
ENDRULE

STARTRULE(expressionList)
  __sArgs = 0;
  while(!IsSymbol(")")) {
    __sArgs++;
    Invoke(expression);
    if(IsSymbol(","))
      ConsumeToken;
  }
ENDRULE

STARTRULE(letStatement)
  ConsumeKeyword("let");

  CheckStLookup(left,tok.value);

  ConsumeIdentifier;

  if(IsSymbol("[")) {
    ConsumeToken;
    Invoke(expression);
    ConsumeSymbol("]");
  }

  ConsumeSymbol("=");
  Invoke(expression);

  PopEntry(left);
  ConsumeSymbol(";");
ENDRULE

DECLARE(statements);

STARTRULE(ifStatement)
  ConsumeKeyword("if");
  ConsumeSymbol("(");
  Invoke(expression);
  ConsumeSymbol(")");

  Arith(not);

  int l1 = labelCount++;
  int l2 = labelCount++;

  IfGoto(l1);

  ConsumeSymbol("{");
  Invoke(statements);
  ConsumeSymbol("}");
  Goto(l2);

  Label(l1);
  if(IsKeyword("else")) {
     ConsumeToken;
     ConsumeSymbol("{");
     Invoke(statements);
     ConsumeSymbol("}");
  }
  Label(l2);
ENDRULE

STARTRULE(whileStatement)
  ConsumeKeyword("while");

  int l1 = labelCount++;
  int l2 = labelCount++;

  Label(l1);
  ConsumeSymbol("(");
  Invoke(expression);
  ConsumeSymbol(")");

  Arith(not);
  IfGoto(l2);

  ConsumeSymbol("{");
  Invoke(statements);
  ConsumeSymbol("}");
  Goto(l1);

  Label(l2);
ENDRULE

STARTRULE(doStatement)
  ConsumeKeyword("do");
  Invoke(expression);
  Pop(S("temp"), 0);
  ConsumeSymbol(";");
ENDRULE

STARTRULE(returnStatement)
  ConsumeKeyword("return");
  if(!IsSymbol(";"))
    Invoke(expression);
  ConsumeSymbol(";");

  // void returning function must return something
  if(SpanEqual(LastFuncReturn, S("void"))) {
    Push(S("constant"),0);
  }
  Return;
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

  while(IsKeyword("var")) {
    Invoke(varDec);
  }

  FunctionName(LastFuncName);
  int16_t vars = STCount(&StSubroutine, S("var"), false);
  FunctionParams(vars);

  if(SpanEqual(LastFuncType, S("method"))) {
    STAdd(&StSubroutine, S("this"), LastClassName, S("arg"), STCount(&StSubroutine,S("arg"), true));
    Push(S("argument"),0);
    Pop(S("pointer"),0);
  } else if(SpanEqual(LastFuncType, S("constructor"))) {
    int16_t fields = STCount(&StClass, S("field"), false);
    Push(S("constant"), fields);
    Call("Memory.alloc",1);
    Pop(S("pointer"),0);
  }

  Invoke(statements);

  // void returning functions might not contain a 'return statement'
  if(SpanEqual(LastFuncReturn, S("void"))) {
    Push(S("constant"),0);
    Return;
  }
  ConsumeSymbol("}");
ENDRULE


STARTRULE(subroutineDec)

  // Reset subroutine symbol table when declaring new subroutine.
  STInit(&StSubroutine);
  
  LastFuncType = tok.value;

  ConsumeToken;
  
  LastFuncReturn = tok.value;

  if(IsType || IsToken(keyword, "void")) {
    ConsumeToken;
  } else
      tokenerr;

  LastFuncName = tok.value;
  ConsumeIdentifier;

  ConsumeSymbol("(");
  Invoke(parameterList);
  ConsumeSymbol(")");
  Invoke(subroutineBody);
ENDRULE


STARTRULE(class)

  // Reset symble tables when starting a new class
  STInit(&StClass);
  STInit(&StSubroutine);

  NextToken; // Just one class x file, trivial to extend to multiple ones

  ConsumeKeyword("class");

  LastClassName = tok.value;

  ConsumeIdentifier;
  ConsumeSymbol("{");

  // This is a translator (one pass). All declarations must come first to fill the symbol table
  // before compiling subroutines.
  while(IsKeyword("static") || IsKeyword("field"))
    Invoke(classVarDec);

  while(!IsSymbol("}")) {

    if(IsKeyword("constructor") || IsKeyword("function") || IsKeyword("method"))
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

    char* filePath = argv[i];

    // Craft output file name
    Byte newName[1024];
    Buffer fileNameBuf = BufferInit(newName, sizeof(newName));
    Span oldName = SpanFromString(filePath);
    Span withoutExt = SpanRCut(oldName, '.').head;

    baseName = SpanRCut(withoutExt, '/').tail;

    BufferCopy(withoutExt, &fileNameBuf);

    #ifdef TOKENIZER
    BufferCopy(S("T.vm"), &fileNameBuf);
    #else
    BufferCopy(S(".vm"), &fileNameBuf);
    #endif

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

    Span s = BufferToSpan(&bufout);

    BufferPushByte(&fileNameBuf, 0); // make it a proper 0 terminated string

    char* writeError = OsFlash((char*)BufferToSpan(&fileNameBuf).ptr, s);
    if(writeError) {
      fprintf(stderr, "%s\n", writeError);
      return -1;
    }
  }

  return 0;
}
