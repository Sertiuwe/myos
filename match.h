#ifndef MATCH_H
#define MATCH_H

#include "types.h"

/* Строки */
int  m_strlen(const char *s);
int  m_streq(const char *a, const char *b);
char *m_strcpy(char *dst, const char *src);
char *m_strcat(char *dst, const char *src);

/* Поиск и сопоставление */
int  m_startswith(const char *s, const char *prefix);
int  m_endswith(const char *s, const char *suffix);
int  m_contains(const char *s, const char *needle);
int  m_match(const char *s, const char *pattern);   /* wildcards: * и ? */

/* Классификация символов */
int  m_isdigit(char c);
int  m_isalpha(char c);
int  m_isspace(char c);
int  m_tolower(int c);
int  m_toupper(int c);

/* Конвертация */
int  m_atoi(const char *s);
int  m_parse_hex(const char *s, uint32_t *out);

/* Разбор слов */
int  m_count_words(const char *s);
char *m_next_word(char **pp);   /* возвращает NULL на конце */

#endif
