/* Copyright (c) 2010 Dave Reisner
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <ctype.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <readline/history.h>
#include <readline/readline.h>

#include "posty.h"

char *strtrim(char *str) {
  char *pch = str;

  if (str == NULL || *str == '\0')
    return str;

  while (isspace(*pch)) pch++;

  if (pch != str)
    memmove(str, pch, (strlen(pch) + 1));

  if (*str == '\0')
    return str;

  pch = (str + strlen(str) - 1);

  while (isspace(*pch))
    pch--;

  *++pch = '\0';

  return str;
}

char *strlower(char *str) {
  char *pch = str;

  while (*pch != '\0') {
    *pch = tolower(*pch);
    pch++;
  }

  return str;
}

void stack_reset() {
  if (stackptr == &opstack[0])
    return;

  if (verbose >= 2) { /* Dump individual items on the stack */
    printf(":: Stack Dump :: ");
    while (stackptr != &opstack[0])
      printf("%.*f ", precision, *--stackptr);
    putchar('\n');
  } else /* Just reset the pointer */
    stackptr = &opstack[0];
}

/** parse operations */
int parse_constant(const char *constant) {
  if (strcmp(constant, "pi") == 0)
    *stackptr++ = M_PI;
  else if (strcmp(constant, "e") == 0)
    *stackptr++ = M_E;

  return 0;
}

int parse_operand(const char *token, double *operand) {
  char *endPtr;

  *operand = strtod(token, &endPtr);
  if (*endPtr != '\0') {
    fprintf(stderr, "!! Bad input: %s\n", token);
    return 1;
  } else if (DOUBLE_EQ(*operand, HUGE_VAL)) {
    fprintf(stderr, "!! Input overflow.\n");
    return 1;
  }

  return 0;
}

int parse_operator(const char operator) {
  double op1, op2;

  op2 = *--stackptr;
  op1 = *--stackptr;

  if (verbose >= 2)
    printf(":: %.*f ", precision, op1);

  switch (operator) {
    case '+': op1 += op2; break;
    case '-': op1 -= op2; break;
    case '*': op1 *= op2; break;
    case '^': op1 = pow(op1, op2); break;
    case '/': if (DOUBLE_EQ(op2, 0)) {
                fprintf(stderr, "!! Divide by zero\n");
                return 1;
              }
              op1 /= op2; 
              break;
    case '%': if (DOUBLE_EQ(op2, 0) || (int)op2 == 0) {
                fprintf(stderr, "!! Divide by zero\n");
                return 1;
              }
              op1 = (int)op1 % (int)op2;
              break;
  }

  if (verbose >= 2)
    printf("%c %.*f = %.*f\n", operator, precision, op2, precision, op1);

  if (DOUBLE_EQ(op1, HUGE_VAL)) {
    fprintf(stderr, "!! Result overflow\n");
    return 1;
  }

  *stackptr++ = op1;

  return 0;
}

int parse_precision(const char *p) {
  char *endPtr;
  int pre;

  pre = (int)strtol(p, &endPtr, 10);
  if (*endPtr != '\0') {
    fprintf(stderr, "!! Bad precision specified\n");
    return 1;
  } else {
    precision = pre;

    if (precision < 0) /* clamp negative numbers to 0 */
      precision ^= precision;

    if (verbose >= 1)
      printf(":: Precision set to %d decimal places.\n", precision);
  }

  return 0;
}

int parse_mfunc(char *mfunc) {
  double op1;
  double res;

  op1 = *--stackptr;

  if (strcmp(mfunc, "sin") == 0)
    res = sin(op1);
  else if (strcmp(mfunc, "cos") == 0)
    res = cos(op1);
  else if (strcmp(mfunc, "tan") == 0)
    res = tan(op1);
  else if (strcmp(mfunc, "asin") == 0)
    res = asin(op1);
  else if (strcmp(mfunc, "acos") == 0)
    res = acos(op1);
  else if (strcmp(mfunc, "atan") == 0)
    res = atan(op1);
  else if (strcmp(mfunc, "sqrt") == 0)
    res = sqrt(op1);
  else if (strcmp(mfunc, "ln") == 0)
    res = log(op1);

  if (verbose >= 2)
    printf("%s(%.*f) = %.*f\n", mfunc, precision, op1, precision, res);

  *stackptr++ = res;

  return 0;
}

int parse_expression(char *expr) {
  if (strlen(strtrim(expr)) == 0)
    return BREAK;

  char *token;
  static const char *operators = "+/*-%^";
  static const char *mfuncs = "|ln|sqrt|sin|cos|tan|asin|acos|atan|";
  static const char *constants = "|e|pi|";
  double operand;
  char *ptr;

  while ((token = strsep(&expr, " \n"))) {
    if (strlen(token) == 0) continue;

    if (*token == ':') /* precision specified */
      parse_precision(++token);
    else if (strchr(operators, *token) && strlen(token) == 1) { /* operator */
      if (stackptr - opstack < 2) {
        fprintf(stderr, "!! Malformed expression -- too few operands.\n");
        return CONTINUE;
      }

      if (parse_operator(*token) > 0) {
        return CONTINUE;
      }
    } else if ((ptr = strstr(mfuncs, strlower(token))) != NULL &&
              *(ptr - 1) == '|' && *(ptr + strlen(token)) == '|') {
      /* validated trig function */
      if (stackptr - opstack < 1) {
        fprintf(stderr, "!! Malformed expression -- too few operands.\n");
        return CONTINUE;
      }
      parse_mfunc(token);
    } else if ((ptr = strstr(constants, strlower(token))) != NULL &&
              *(ptr - 1) == '|' && *(ptr + strlen(token)) == '|') {
      /* validated constant */
      parse_constant(token);
    } else { /* it's an operand, or it's bad input */
      if (parse_operand(token, &operand) > 0) /* parsing failed on bad input */
        return CONTINUE;

      if (stackptr == &opstack[STACK_SIZE]) { /* stack overflow */
        fprintf(stderr, "!! Stack overflow. Expression too large.\n");
        return CONTINUE;
      }

      *stackptr++ = operand;
    }
  }

  if (stackptr - opstack > 1)
    fprintf(stderr, "!! Malformed expression -- too many operands.\n");
  else if (stackptr - opstack == 1) {
    if (verbose >= 1)
      printf(" = %.*f\n", precision, *--stackptr);
    else
      printf("%.*f\n", precision, *--stackptr);
  }

  return CONTINUE;
}

int main(int argc, char *argv[]) {
  char *buf, line[BUFSIZ + 1];

  if (argc > 1 && strcmp(argv[1], "-v") == 0) {
    fprintf(stderr, "::Stack dumps enabled::\n");
    verbose = 2;
  }

  stackptr = &opstack[0]; /* initialize stack */

  /* If stdin has data, hit it and quit it */
  if (!isatty(fileno(stdin))) {
    fgets(line, BUFSIZ, stdin);
    parse_expression(line);
    freopen(ctermid(NULL), "r", stdin);
    return 0;
  }

  /* going interactive. fire up readline */
  using_history();

  while ((buf = readline("> ")) != NULL) {
    add_history(buf);
    strcpy(line, buf);
    parse_expression(line);
    stack_reset();
    free(buf);
  }
  putchar('\n');

  clear_history();

  return 0;
}
