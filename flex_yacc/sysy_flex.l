%{
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include "sy_parser/y.tab.h"
    #include "sy_parser/utils.h"

    extern int fileno(FILE *stream);
    extern int yylineno;
%}

LETTER              [a-zA-Z]
DEC_DIGIT           [0-9]
NZ_DEC_DIGIT        [1-9]
OCT_DIGIT           [0-7]
HEX_DIGIT           [0-9a-fA-F]

WHITE_SPACE         [ \t]*
LINE_END            \n|\r\n
SINGLE_MARK         [-+*/%;,:?!<>()[\]{}.=]

IDENTIFIER          ({LETTER}|_)({LETTER}|_|{DEC_DIGIT})*

INT_DEC_CONST       ({NZ_DEC_DIGIT}{DEC_DIGIT}*)
INT_OCT_CONST       0{OCT_DIGIT}*
INT_HEX_CONST       0[xX]{HEX_DIGIT}+
INT_CONST           {INT_DEC_CONST}|{INT_OCT_CONST}|{INT_HEX_CONST}

DEC_FRAC_CONST      ({DEC_DIGIT}*\.{DEC_DIGIT}+)|{DEC_DIGIT}+\.
HEX_FRAC_CONST      ({HEX_DIGIT}*\.{HEX_DIGIT}+)|{HEX_DIGIT}+\.
DEC_EXP_PART        [eE][-+]?{DEC_DIGIT}+
BIN_EXP_PART        [pP][-+]?{DEC_DIGIT}+
DEC_FLOAT_CONST     {DEC_FRAC_CONST}{DEC_EXP_PART}?|{DEC_DIGIT}+{DEC_EXP_PART}
HEX_FLOAT_CONST     0[xX]({HEX_FRAC_CONST}|{HEX_DIGIT}+){BIN_EXP_PART}
FLOAT_CONST         {DEC_FLOAT_CONST}|{HEX_FLOAT_CONST}

STRING_CONST        \"([^\\\"]|\\.)*\"

%x ONE_LINE_COMMENT_MODE MULT_LINE_COMMENT_MODE

%%

<INITIAL>{
    {SINGLE_MARK}   { return yytext[0]; }
    "<="            { return LEQUAL; }
    ">="            { return GEQUAL; }
    "=="            { return EQUAL; }
    "!="            { return NEQUAL; }
    "&&"            { return AND; }
    "||"            { return OR; }

    "int"           { return INT; }
    "float"         { return FLOAT; }
    "void"          { return VOID; }
    "const"         { return CONST; }
    "if"            { return IF; }
    "else"          { return ELSE; }
    "while"         { return WHILE; }
    "break"         { return BREAK; }
    "continue"      { return CONTINUE; }
    "return"        { return RETURN; }

    {IDENTIFIER}    {
        yylval.str = my_strdup(yytext);
        return IDENTIFIER;
    }

    {INT_CONST}     {
        yylval.str = my_strdup(yytext);
        return INT_CONST;
    }

    {FLOAT_CONST}   {
        yylval.str = my_strdup(yytext);
        return FLOAT_CONST;
    }

    {STRING_CONST}  {
        yylval.str = my_strdup(yytext + 1);
        yylval.str[strlen(yylval.str) - 1] = '\0';
        return STRING_CONST;
    }

    "//"            { BEGIN(ONE_LINE_COMMENT_MODE); }
    "/*"            { BEGIN(MULT_LINE_COMMENT_MODE); }

    {WHITE_SPACE}   ;
    {LINE_END}      { yylineno++; }
    <<EOF>>         { return ENDMARKER; }
    .               ;
}

<ONE_LINE_COMMENT_MODE>{
    {LINE_END}      { yylineno++; BEGIN(INITIAL); }
    <<EOF>>         { return ENDMARKER; }
    .               ;
}

<MULT_LINE_COMMENT_MODE>{
    {LINE_END}      { yylineno++; }
    "*/"            { BEGIN(INITIAL); }
    <<EOF>>         { return ENDMARKER; }
    .               ;
}

%%

int yywrap() {
    return 1;
}
