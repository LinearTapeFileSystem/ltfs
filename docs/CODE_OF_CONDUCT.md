# C language coding style guide for LTFS

This document is guide line of wring C language code in the LTFS development.

This guide line is not a rule. You don't need to follow every items in every place. But this document is described for guiding that normal level engineer or higher can understand your code easily.

Again, this is not a rule. You can use any style of coding if you find another way to improve readability for everyone.

## 1. Standard of C

  - Using C11 is recommended
  - GCC extension is recommended
  - C99 is acceptable
  - Do not use C89 or prior

## 2. General guide

  - Max width of line is 120 characters, 2-byte character is counted as 2 characters
  - Use tab indentation with 4 spaces
  - Do not place following white spaces at the end of line
  - Do not place leading spaces at empty line

### 2.1. Consideration about text editors

At this time, VIM and EMACS might be major editors for developing LTFS. It would be an good idea to add magic lines for editor setting in the source code like below.

```C
/* -*- indent-tabs-mode: t; tab-width: 4 -*- */
/* vim: set tabstop=4:softtabstop=4:shiftwidth=4:expandtab! */
#include <stdio.h>
#include <stdlib.h>
...


int func_foo(int a1, double d1)
{
    ....
    return 0;
}
```

The first line is a setting for EMACS, the second line for VIM.

Adding files for specific IDE is not recommended. It often enforces to use a specific IDE for all developers.

## 3. Formatting

### 3.1. Comments in logic

Use standard C style comment line and block. C++ style comment is acceptable only for reasonable comment out of logic.

```C
/* normal one line comment */

/*
 * Use this style for multi-line comment
 */

/* Following 3 libnes are an exsample of logic comment out */
//for (int i = 0; i < MAX_LINE; i++) {
//    ...
//}
```

### 3.2. Brace Placement

Brace placement below is recommended. For function declaration, another format is recommended. See xxx.

```C

if (condition1) {
    ...
} else if (condition2) {
    ...
} else {
    ...
}

for (int i = 0; i < BAR; i++) {
}

```

Single line form can be used for improving readability like below.

```C
if (condition) val1 = false;
```

### 3.3. Parentheses `()`

  - Do place parentheses next to function name
  - Do not place parentheses next to keywords. Put a space between
  - Do not use parentheses in return statement if it is not necessary

Show examples below.

```C
if (condition) {
    ...
}

int ret = func_foo(a, d);

return 1;

return (a > b) ? true : false;
```

### 3.4. `switch` Statement

  - Indent `case` with in a `switch` statement
  - Falling through a case statement into the next case statement is acceptable as long as a comment is included
  - The `default` case should always be present and trigger an error if it should not be reached, yet is reached
  - If you need to create variables put all the code in a block

```C
    switch (...) {
        case VALUE1:
            ...
            break;
        case VALUE2:
            ...
            /* Fall through */
        case VALUE3:
            {
                int v;
                ...
            }
            break;
        default:
            /* Handle error if this is not expected */
            break;
   }
```

### 3.5. Function Declaration

  - Do not place line break between return type and function name as much as possible
  - Place starting brace `{` in the next line
  - Break line after comma `,`
  - Doxygen style comment is recommended if the function can be called from customer as API

```C
/**
 * Summary of func_normal()
 * Developer can be add more details about func_nortmal() here.
 *
 * @param c integer variable for the fucntion
 * @return 0 on success, otherwise a negative value
 */
int func_normal(int c)
{
    /* Logic of the func */
    return 0;
}

int func_which_has_a_very_long_name_and_need_line_break(struct very_long_name_structure *st1,
                                                        struct very_long_name_structure *st2)
{
    /* Logic of the func */
    return 0;
}

struct very_long_name_structure
    *anothr_func_which_has_a_very_long_name_and_need_line_break (struct very_long_name_structure *st1,
                                                                 struct very_long_name_structure *st2)
{
    /* Logic of the func */
    return NULL;
}
```



### 3.6. Pointer variables and function that returns a pointer

  - Place the `*` close to the variable name not type
  - place the `*` close to the function name not type

```C
char *foo; /* GOOD: foo is a pointer of char */
char* foo; /* BAD:  this is acceptable from C perspective but ambiguous when multiple variables are defined in a line */

/* Good Example */
int *func_foo(int a)
{
    ...
}

/* Bad Example: No rationale, just follow the K&R */
int* func_foo(int a)
{
    ...
}
```

### 3.7. Structure, Union, Enumerates and Type Declaration

  - Place starting brace `{` in the next line
  - Do not create new type from a pointer type as much as possible
  - It is not a good idea to redefine the primitive type as another name. Check it is almost defined in the standard header before considering this.
  - Doxygen style comment is recommended if it is used from customer as API

```C
/**
 * Summary of struct foo
 * Developer can be add more details here.
 */
struct foo
{
    int fi; /**< Summary of fi */
    ...
    ...
};

/**
 * Summary of struct foo
 * Developer can be add more details here.
 */
typedef struct foo
{
    int fi; /**< Summary of fi */
    ...
    ...
} st_foo;

/**
 * Summary of st_foo
 * Developer can be add more details here.
 */
typedef struct
{
    int fi; /**< Summary of fi */
    ...
    ...
} st_foo;

typedef struct foo st_foo;     /* GOOD */
typedef struct foo *pst_foo;   /* NOT GOOD: Might not be a good idea because you need to name value that implies it is a pointer. */
typedef int (*func_type)(int); /* ACCEPTABLE: May be function pointer type is only expeption */
typedef char[8] st_barcode;    /* WORST...: Actually it is no meaning. It just say `st_barcode` type is pointer of `char` */

```

## 4. Preprocessor

### 4.1. Preprocessor directives

  - Indent can be used
  - Do not make indent against logic after detectives

```C
#ifdef AAA
    #ifdef BBB
    ...
    ...
    #endif
#endif

int func_foo(int a)
{
    int b;

    #ifdef AAA
    b = a * 2
    #else
    b = a/2
    #endif

    return b;
}
```

### 4.2. Definitions & constants

  - Definitions are preferable than constant declarations for constants
  - Use only capital letters for name

```C
#define MIN_OF_FOO (5)
#define MASK_FOO   (0xFFFF00AAAA)
```

### 4.3. Macros

  - Before using macro, consider to use inline function
  - Don't change syntax via macro substitution. It makes the program unintelligible to all but the perpetrator

```C
#define MACRO_FUNC(a, b) (a + b)

#ifdef MSG_CHECK
#define MSG(level, id, ...)               \
    do {                                  \
        printf(MSG ## id, ##__VA_ARGS__); \
    } while(0)
#else
#define MSG(level, id, ...)                                      \
    do {                                                         \
        if (level <= log_level)                                  \
            msg_internal(true, level, NULL, #id, ##__VA_ARGS__); \
    } while (0)
#endif
```

## 5. Names

Naming might be one of the most important and difficult topic in the programming. Deep consideration and continuous refactoring is recommended if the situation allows.

### 5.1. Function Names

The snake case with small letter is recommended. Starting underscore, "`_`", is acceptable only for internal function.

```C
static int _func_foo(int a)
{
    ...
    retuern 0;
}

int func_foo(int a, int b)
{
   int c;
   ...

   c = _func_foo(a) + b;
   return c;
}
```

### 5.2. Structure Names, Union Names and Enumerator names

The snake case with small letter is recommended for name.

```c
struct foo_structure
{
    int a;
    ...
};

union foo_union
{
    int a:
    ...
};
```

The snake case with capital letter is recommended for entry name of enumerator.

```C
enum foo_enumerator
{
    FOO_AAA = 0,
    FOO_BBB,
    FOO_CCC,
    ...
    FOO_CCC,
}
```

### 5.3. Variable Names

The snake case with small letter is recommended. For local variables for a function or a block, shorter name is better than long descriptive name (Assume function or block is enough short to see everything on one screen).

```C
int variable_foo;
int this_is_really_long_variable_name;
```

#### 5.3.1. Positive logic naming

Positive logic naming is recommended. Negative logic naming might lead misunderstanding easily because you need to use double negative in the code, one negative is in the name others are in the code.

```C
/* Good example */
int *func_foo(int a, bool keep_alloc)
{
    int rc = -1;
    int *ai = calloc(1, sizeof(int));

    if (!ai) {
        return rc;
    }
    else {
        *ai = a;
        rc = *ai + a;
    }

    if (!keep_alloc) free(ai);

    return rc;
}

/* BAD example */
int *func_foo(int a, bool without_free)
{
    int rc = -1;
    int *ai = calloc(1, sizeof(int));

    if (!ai) {
        return rc;
    }
    else {
        *ai = a;
        rc = *ai + a;
    }

    if (!without_free) free(ai);

    return rc;
}
```

## 6. Header

### 6.1. Include guard

Having include guard in every header files are strongly recommended for avoiding multiple include at compile.

Example of `foo_baa.h`
```C
#ifndef FOO_BAA_H
#define

/* Add header contents here */
...

#endif /* FOO_BAA_H */
```

### 6.2. Order of include

Following include order is preferable.

  1. dir/foo.h (corresponded header file against c file)
  2. Empty line
  3. System headers (closed by `<>`)
  4. Empty line
  5. Headers for other libraries which is using in this project
  6. Empty line
  7. Headers in the project

Example of `foo_baa.h`.

```C
#include "foo_baa.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>

#include <valgrind.h>
#include <libxml/tree.h>

#include "src/include/my_proj.h"

int func_foo(void)
{
    ...
}
```

## X. Miscellaneous

### X.1. Preamble

May be preamble at the beginning of file is important for tracking the originality of the code. Because license and copyright clearance must be done bore releasing the product.

In this process,  we need to distinguish between the code written by ourselves and the code written by others. And finally we might need to indicate something (like into NOTICES file) if others are using other licenses.

It could have following information.

  - Copyright information
  - Component name
  - File name
  - Short description of file
  - Authors

Example is a preamble in another project.

```C
/*
**
**  OO_Copyright_BEGIN
**
**
**  Copyright 2010, 2025 The LTFS project. All rights reserved.
**
**  Redistribution and use in source and binary forms, with or without
**   modification, are permitted provided that the following conditions
**  are met:
**  1. Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**  2. Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in the
**  documentation and/or other materials provided with the distribution.
**  3. Neither the name of the copyright holder nor the names of its
**     contributors may be used to endorse or promote products derived from
**     this software without specific prior written permission.
**
**  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
**  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
**  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
**  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
**  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
**  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
**  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
**  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
**  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
**  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
**  POSSIBILITY OF SUCH DAMAGE.
**
**
**  OO_Copyright_END
**
*************************************************************************************
**
** COMPONENT NAME:  Linear Tape File System
**
** FILE NAME:       xml_reader_libltfs.c
**
** DESCRIPTION:     XML parser routines for Indexes and Labels.
**
** AUTHORS:         Brian Biskeborn
**                  IBM Almaden Research Center
**                  bbiskebo@us.ibm.com
**
**                  Lucas C. Villa Real
**                  IBM Almaden Research Center
**                  lucasvr@us.ibm.com
**
**                  Atsushi Abe
**                  IBM Tokyo Lab., Japan
**                  piste@jp.ibm.com
**
*************************************************************************************
*/
```

### X.2. `sizeof()`

Use variable name, not type name, if it is possible. Because the code is able to follow the change of variable type in the future.

```C
int func_foo(void)
{
    int a;
    int s = sizeof(a);    /* GOOD */
    int s1 = sizeof(int); /* BAD */
}
```
