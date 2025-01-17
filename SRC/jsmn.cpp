/*
�2012�2015 � Serge Zaitsev � zaitsev DOT serge AT gmail DOT com

Released under the MIT license.  For further information, please visit this page

http://zserge.com/jsmn.html


*/

#include <stdlib.h>
#include "jsmn.h"

/**
 * Allocates a fresh unused token from the token pull.
 */
static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser,  jsmntok_t *tokens, size_t num_tokens, int len) {
	jsmntok_t *tok;
	if (parser->toknext >= num_tokens) return NULL;
	tok = &tokens[parser->toknext++];
	tok->start = tok->end = -1;
	tok->size = 0;
#ifdef JSMN_PARENT_LINKS
	tok->parent = -1;
#endif
	return tok;
}

/**
 * Fills token type and boundaries.
 */
static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type,  int start, int end) {
	token->type = type;
	token->start = start;
	token->end = end;
	token->size = 0;
}

/**
 * Fills next available token with JSON primitive.
 */
static jsmnerr_t jsmn_parse_primitive(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *token;
	int start;

	start = parser->pos;
	int nest = 0;
	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		switch (js[parser->pos]) {
			case '[': // we allow [ ] in jsonpath access tokens
				++nest;
				continue;
			case ']':
				--nest;
				if (nest < 0) goto found; // was actual close of an array
				continue;
			case ':':
			case '\t' : case '\r' : case '\n' : case ' ' : // marks end of thingys
			case ','  :  case '}' :
				goto found;
		}
		if (js[parser->pos] < 32 || js[parser->pos] >= 127) { // only ascii blank and on, no UTF markers
			parser->pos = start;
			return JSMN_ERROR_INVAL;
		}
	}

found:
	if (tokens == NULL) {
		parser->pos--;
		return (jsmnerr_t) 0;
	}
	token = jsmn_alloc_token(parser, tokens, num_tokens,len);
	if (token == NULL) {
		parser->pos = start;
		return JSMN_ERROR_NOMEM;
	}
	jsmn_fill_token(token, JSMN_PRIMITIVE, start, parser->pos);
#ifdef JSMN_PARENT_LINKS
	token->parent = parser->toksuper;
#endif
	parser->pos--;
	return (jsmnerr_t) 0;
}

/**
 * Filsl next token with JSON string.
 */
static jsmnerr_t jsmn_parse_string(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, size_t num_tokens) {
	jsmntok_t *token;

	int start = parser->pos;

	parser->pos++;

	/* Skip starting quote */
	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		char c = js[parser->pos];

		/* Quote: end of string */
		if (c == '\"') {
			if (tokens == NULL)  return (jsmnerr_t) 0;

			token = jsmn_alloc_token(parser, tokens, num_tokens,len);
			if (token == NULL) {
				parser->pos = start;
				return JSMN_ERROR_NOMEM;
			}
			jsmn_fill_token(token, JSMN_STRING, start+1, parser->pos);
#ifdef JSMN_PARENT_LINKS
			token->parent = parser->toksuper;
#endif
			return (jsmnerr_t) 0;
		}

		/* Backslash: Quoted symbol expected */
		if (c == '\\') {
			parser->pos++;
			switch (js[parser->pos]) {
					/* Allows escaped symbol \uXXXX */
				case 'u':
				{
					parser->pos++;
					for (int i = 0; i < 4 && js[parser->pos] != '\0'; i++) {
						/* If it isn't a hex character we have an error */
						if (!((js[parser->pos] >= 48 && js[parser->pos] <= 57) || /* 0-9 */
							(js[parser->pos] >= 65 && js[parser->pos] <= 70) || /* A-F */
							(js[parser->pos] >= 97 && js[parser->pos] <= 102))) { /* a-f */
							parser->pos = start;
							return JSMN_ERROR_INVAL;
						}
						parser->pos++;
					}
					parser->pos--;
					break;
				}
                /* Allowed escaped symbols */
                case '\"': case '/': case '\\': case 'b':
                case 'f': case 'r': case 'n': case 't':
                    break;
                    /* Unexpected symbol */
				default:
					parser->pos = start;
					return JSMN_ERROR_INVAL;
			}
		}
	}
	parser->pos = start;
	return JSMN_ERROR_PART;
}

/**
 * Parse JSON string and fill tokens.
 */
jsmnerr_t jsmn_parse(jsmn_parser *parser, const char *js, size_t len, jsmntok_t *tokens, unsigned int num_tokens) { // if tokens == null, just count them
	jsmnerr_t r;
	int i;
	jsmntok_t *token;
	int count = 0;

	for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
		char c;
		jsmntype_t type;
		c = js[parser->pos];
		switch (c) {
			case '{': case '[':
				count++;
				if (tokens == NULL) break;

				token = jsmn_alloc_token(parser, tokens, num_tokens,len);
				if (token == NULL) return JSMN_ERROR_NOMEM;
				if (parser->toksuper != -1) {
					tokens[parser->toksuper].size++;
#ifdef JSMN_PARENT_LINKS
					token->parent = parser->toksuper;
#endif
				}
				token->type = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
				token->start = parser->pos;
				parser->toksuper = parser->toknext - 1;
				break;
			case '}': case ']':
				if (tokens == NULL) break;

				type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
#ifdef JSMN_PARENT_LINKS
				if (parser->toknext < 1) return JSMN_ERROR_INVAL;
				token = &tokens[parser->toknext - 1];
				for (;;) {
					if (token->start != -1 && token->end == -1) {
						if (token->type != type)  return JSMN_ERROR_INVAL;
						token->end = parser->pos + 1;
						parser->toksuper = token->parent;
						break;
					}
					if (token->parent == -1)  break;
					token = &tokens[token->parent];
				}
#else
				for (i = parser->toknext - 1; i >= 0; i--) {
					token = &tokens[i];
					if (token->start != -1 && token->end == -1) {
						if (token->type != type)  return JSMN_ERROR_INVAL;
						parser->toksuper = -1;
						token->end = parser->pos + 1;
						break;
					}
				}
				/* Error if unmatched closing bracket */
				if (i == -1) return JSMN_ERROR_INVAL;
				for (; i >= 0; i--) {
					token = &tokens[i];
					if (token->start != -1 && token->end == -1) {
						parser->toksuper = i;
						break;
					}
				}
#endif
				break;
			case '\t' : case '\r' : case '\n' : case ':' : case ',': case ' ':  // ignore all these
				break;
			case '"':
				r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
				if (r < 0) 
					return r;
				count++;
				if (parser->toksuper != -1 && tokens != NULL) tokens[parser->toksuper].size++;
				break;
			default:
				r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
				if (r < 0) return r;
				count++;
				if (parser->toksuper != -1 && tokens != NULL) tokens[parser->toksuper].size++;
				break;

		}
	}

	for (i = parser->toknext - 1; i >= 0; i--) {
		/* Unmatched opened object or array */
		if (tokens[i].start != -1 && tokens[i].end == -1)  return JSMN_ERROR_PART;
	}

	return (jsmnerr_t) count;
}

/**
 * Creates a new parser based over a given  buffer with an array of tokens 
 * available.
 */
void jsmn_init(jsmn_parser *parser) {
	parser->pos = 0;
	parser->toknext = 0;
	parser->toksuper = -1;
}
