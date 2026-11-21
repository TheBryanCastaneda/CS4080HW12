#include <string.h>

#include "common.h"
#include "scanner.h"

typedef struct {
  const char* start;
  const char* current;
  int line;
} Scanner;

static Scanner scanner;

void initScanner(const char* source) {
  scanner.start = source;
  scanner.current = source;
  scanner.line = 1;
}

static bool isAtEnd(void) {
  return *scanner.current == '\0';
}

static char advance(void) {
  scanner.current++;
  return scanner.current[-1];
}

static char peek(void) {
  return *scanner.current;
}

static char peekNext(void) {
  if (isAtEnd()) {
    return '\0';
  }

  return scanner.current[1];
}

static bool match(char expected) {
  if (isAtEnd() || *scanner.current != expected) {
    return false;
  }

  scanner.current++;
  return true;
}

static bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

static bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      c == '_';
}

static Token makeToken(TokenType type) {
  Token token;
  token.type = type;
  token.start = scanner.start;
  token.length = (int)(scanner.current - scanner.start);
  token.line = scanner.line;
  return token;
}

static Token errorToken(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = scanner.line;
  return token;
}

static void skipWhitespace(void) {
  for (;;) {
    switch (peek()) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;

      case '\n':
        scanner.line++;
        advance();
        break;

      case '/':
        if (peekNext() == '/') {
          while (peek() != '\n' && !isAtEnd()) {
            advance();
          }
        } else {
          return;
        }
        break;

      default:
        return;
    }
  }
}

static Token identifier(void) {
  static const struct {
    const char* text;
    TokenType type;
  } keywords[] = {
    {"and", TOKEN_AND},
    {"case", TOKEN_CASE},
    {"class", TOKEN_CLASS},
    {"const", TOKEN_CONST},
    {"continue", TOKEN_CONTINUE},
    {"default", TOKEN_DEFAULT},
    {"else", TOKEN_ELSE},
    {"false", TOKEN_FALSE},
    {"for", TOKEN_FOR},
    {"fun", TOKEN_FUN},
    {"if", TOKEN_IF},
    {"nil", TOKEN_NIL},
    {"or", TOKEN_OR},
    {"print", TOKEN_PRINT},
    {"return", TOKEN_RETURN},
    {"super", TOKEN_SUPER},
    {"switch", TOKEN_SWITCH},
    {"this", TOKEN_THIS},
    {"true", TOKEN_TRUE},
    {"var", TOKEN_VAR},
    {"while", TOKEN_WHILE}
  };

  while (isAlpha(peek()) || isDigit(peek())) {
    advance();
  }

  Token token = makeToken(TOKEN_IDENTIFIER);

  for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++) {
    size_t length = strlen(keywords[i].text);

    if ((size_t)token.length == length &&
        memcmp(token.start, keywords[i].text, length) == 0) {
      token.type = keywords[i].type;
      break;
    }
  }

  return token;
}

static Token number(void) {
  while (isDigit(peek())) {
    advance();
  }

  if (peek() == '.' && isDigit(peekNext())) {
    advance();

    while (isDigit(peek())) {
      advance();
    }
  }

  return makeToken(TOKEN_NUMBER);
}

static Token string(void) {
  while (peek() != '"' && !isAtEnd()) {
    if (peek() == '\n') {
      scanner.line++;
    }

    advance();
  }

  if (isAtEnd()) {
    return errorToken("Unterminated string.");
  }

  advance();
  return makeToken(TOKEN_STRING);
}

Token scanToken(void) {
  skipWhitespace();
  scanner.start = scanner.current;

  if (isAtEnd()) {
    return makeToken(TOKEN_EOF);
  }

  char c = advance();

  if (isAlpha(c)) {
    return identifier();
  }

  if (isDigit(c)) {
    return number();
  }

  switch (c) {
    case '(':
      return makeToken(TOKEN_LEFT_PAREN);
    case ')':
      return makeToken(TOKEN_RIGHT_PAREN);
    case '{':
      return makeToken(TOKEN_LEFT_BRACE);
    case '}':
      return makeToken(TOKEN_RIGHT_BRACE);
    case ',':
      return makeToken(TOKEN_COMMA);
    case '.':
      return makeToken(TOKEN_DOT);
    case '-':
      return makeToken(TOKEN_MINUS);
    case '+':
      return makeToken(TOKEN_PLUS);
    case ';':
      return makeToken(TOKEN_SEMICOLON);
    case ':':
      return makeToken(TOKEN_COLON);
    case '/':
      return makeToken(TOKEN_SLASH);
    case '*':
      return makeToken(TOKEN_STAR);
    case '!':
      return makeToken(match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
      return makeToken(match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
      return makeToken(match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
      return makeToken(match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"':
      return string();
  }

  return errorToken("Unexpected character.");
}
